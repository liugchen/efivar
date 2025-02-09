// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libefivar - library for the manipulation of EFI variables
 * Copyright 2012-2013 Red Hat, Inc.
 */

#include "fix_coverity.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include "list.h"
#include "efivar.h"
#include "uefivar.h"

#define DEBUG 1
#define msg_gerr printf //efi_error
#define DBG_ERR printf //efi_error
#define msg_gdbg printf //efi_error
#define DBG_INFO printf //efi_error
#define DBG_PROG printf //efi_error

#define KLVARS_TOOL_NAME "klvars"
#define BIOS_BACKUP_PATH "./rom.bin"
#define VARS_LAYOUT_NAME "NVRAM"
#define BIOS_GENERATE_PATH            "./rom.bin"

static	LIST_HEAD(klvars_list);

static const char layoutfile[] = "./Layout";
typedef uint32_t chipoff_t; /* Able to store any addressable offset within a supported flash memory. */
typedef uint32_t chipsize_t; /* Able to store the number of bytes of any supported flash memory. */
struct romentry {
	struct romentry *next;

	chipoff_t start;
	chipoff_t end;
	bool included;
	char *name;
	char *file;
};

struct flashrom_layout {
	struct romentry *head;
};
struct flashrom_layout *layout = NULL;

typedef struct _KLUTIL_CONTEXT {
  char  *generate_file;
  char  *backup_file;
  char  *backup_data;
  char  *update_file;
  char  *update_data;

  char  *nvram_file;
  char  *nvram_data;        // Needed by all three utilities, for check bios
  int   nvram_size;         // Needed by all three utilities, for check bios
  int   updfile_size;
  int   bkfile_size;

} KLUTIL_CONTEXT;

KLUTIL_CONTEXT  klutil_ctx;

typedef struct efi_kernel_variable_32_t {
	uint16_t	VariableName[1024/sizeof(uint16_t)];
	efi_guid_t	VendorGuid;
	uint32_t	DataSize;
	uint8_t		Data[1024];
	uint32_t	Status;
	uint32_t	Attributes;
} PACKED efi_kernel_variable_32_t;

typedef struct efi_kernel_variable_64_t {
	uint16_t	VariableName[1024/sizeof(uint16_t)];
	efi_guid_t	VendorGuid;
	uint64_t	DataSize;
	uint8_t		Data[1024];
	uint64_t	Status;
	uint32_t	Attributes;
} PACKED efi_kernel_variable_64_t;

static struct romentry *mutable_layout_next(
		const struct flashrom_layout *const layout, struct romentry *iterator)
{
	return iterator ? iterator->next : layout->head;
}

static struct romentry *_layout_entry_by_name(
		const struct flashrom_layout *const layout, const char *name)
{
	struct romentry *entry = NULL;
	while ((entry = mutable_layout_next(layout, entry))) {
		if (!strcmp(entry->name, name))
			return entry;
	}
	return NULL;
}

int get_region_range(struct flashrom_layout *const l, const char *name,
		     unsigned int *start, unsigned int *len)
{
	const struct romentry *const entry = _layout_entry_by_name(l, name);
	if (entry) {
		*start = entry->start;
		*len = entry->end - entry->start + 1;
		return 0;
	}
	return 1;
}

/**
 * @brief Create a new, empty layout.
 *
 * @param layout Pointer to returned layout reference.
 *
 * @return 0 on success,
 *         1 if out of memory.
 */
int flashrom_layout_new(struct flashrom_layout **const layout)
{
	*layout = malloc(sizeof(**layout));
	if (!*layout) {
		msg_gerr("Error creating layout: %s\n", strerror(errno));
		return 1;
	}

	const struct flashrom_layout tmp = { 0 };
	**layout = tmp;
	return 0;
}

/**
 * @brief Add another region to an existing layout.
 *
 * @param layout The existing layout.
 * @param start  Start address of the region.
 * @param end    End address (inclusive) of the region.
 * @param name   Name of the region.
 *
 * @return 0 on success,
 *         1 if out of memory.
 */
int flashrom_layout_add_region(
		struct flashrom_layout *const layout,
		const size_t start, const size_t end, const char *const name)
{
	struct romentry *const entry = malloc(sizeof(*entry));
	if (!entry)
		goto _err_ret;

	const struct romentry tmp = {
		.next		= layout->head,
		.start		= start,
		.end		= end,
		.included	= false,
		.name		= strdup(name),
		.file		= NULL,
	};
	*entry = tmp;
	if (!entry->name)
		goto _err_ret;

	msg_gdbg("Added layout entry %08zx - %08zx named %s\n", start, end, name);
	layout->head = entry;
	return 0;

_err_ret:
	msg_gerr("Error adding layout entry: %s\n", strerror(errno));
	free(entry);
	return 1;
}

static int layout_from_file(struct flashrom_layout **layout, const char *name)
{
	FILE *romlayout;
	char tempstr[256], tempname[256];
	int ret = 1;

	if (flashrom_layout_new(layout))
		return 1;

	romlayout = fopen(name, "r");

	if (!romlayout) {
		msg_gerr("ERROR: Could not open layout file (%s).\n",
			name);
		return -1;
	}

	while (!feof(romlayout)) {
		char *tstr1, *tstr2;

		if (2 != fscanf(romlayout, "%255s %255s\n", tempstr, tempname))
			continue;
    if (strcmp(strdup(tempname), VARS_LAYOUT_NAME))
    {
      continue;
    }
    
#if 0
		// fscanf does not like arbitrary comments like that :( later
		if (tempstr[0] == '#') {
			continue;
		}
#endif
		tstr1 = strtok(tempstr, ":");
		tstr2 = strtok(NULL, ":");
		if (!tstr1 || !tstr2) {
			msg_gerr("Error parsing layout file. Offending string: \"%s\"\n", tempstr);
			goto _close_ret;
		}
		if (flashrom_layout_add_region(*layout,
				strtol(tstr1, NULL, 16), strtol(tstr2, NULL, 16), tempname))
			goto _close_ret;
	}
	ret = 0;

_close_ret:
	(void)fclose(romlayout);
	return ret;
}

/**
 * @description: 
 *    Initialize the struct KLUTIL_CONTEXT.
 * @param {int} argc
 * @param {char} *
 * @return {*}
 */
int init_context (void)
{
  klutil_ctx.backup_file = (char *)malloc (PATH_MAX);
  if (!klutil_ctx.backup_file) {
    DBG_ERR ("Failed to allocate buffer!\n");
    return 1;
  }
#if 0  
  memset (klutil_ctx.backup_file, 0, PATH_MAX);
  memcpy (klutil_ctx.backup_file, BIOS_BACKUP_PATH, strlen(BIOS_BACKUP_PATH));
#else
  char src_str[PATH_MAX] = BIOS_BACKUP_PATH;
  strncpy (klutil_ctx.backup_file, src_str, strlen(src_str));
  klutil_ctx.backup_file[strlen(src_str)] = 0;
#endif
  klutil_ctx.generate_file = NULL;
  klutil_ctx.update_file   = NULL;
  klutil_ctx.backup_data   = NULL;
  klutil_ctx.update_data   = NULL;
  klutil_ctx.nvram_data      = NULL;
  klutil_ctx.nvram_size      = 0;
  klutil_ctx.updfile_size  = 0;

  return 0;
}

int cleanup_context (void)
{
  if (klutil_ctx.update_file) {
    free (klutil_ctx.update_file);
  }

  if (klutil_ctx.update_data) {
    free (klutil_ctx.update_data);
  }

  if (klutil_ctx.backup_data) {
    free (klutil_ctx.backup_data);
  }

  if (klutil_ctx.backup_file) {
    free (klutil_ctx.backup_file);
  }

  if (klutil_ctx.nvram_file) {
    free (klutil_ctx.nvram_file);
  }

  if (klutil_ctx.nvram_data) {
    free (klutil_ctx.nvram_data);
  }

  return 0;
}

/**
 * @description: 
 *   Read out bios image from the flash, and backup data into a file if klutil_ctx.task_backup = 1 
 * 
 * @param {type} 
 * @return: 
 */
static int read_flash_bios_and_backup ()
{
  FILE     *fp = NULL;
  int      file_size = 0;
  char     *buffer;
  ssize_t  bytes;
  int      buff_size = 0;
  int      sec_size = 0;

  //memset (buffer, 0, flash_size);
  // DBG_PROG("[%s] Reading spi flash...\n", KLVARS_TOOL_NAME);

  if (!klutil_ctx.backup_file) {
      DBG_ERR ("ERR: klutil_ctx.backup_file null!\n");
      return 1;
  }
  // DBG_INFO ("Update file: %s\n", klutil_ctx.backup_file);

  fp = fopen (klutil_ctx.backup_file, "rb");
  if (!fp) {
    DBG_ERR ("Failed to read bios image.\n");
    return 1;
  }

  fseek (fp, 0, SEEK_END);
  file_size = ftell (fp);
  if (file_size <= 0) {
    DBG_ERR ("Invalid bios image.\n");
    return 1;
  }
    
  //
  // Round up the start address to the boundary of sector size
  //
  buff_size = file_size;

  buffer = (char *)malloc (buff_size + sec_size);
  if (!buffer) {
    DBG_ERR ("Failed to allocate buffer!\n");
    return 1;
  }
  memset (buffer, 0xFF, buff_size);

  // DBG_INFO ("file_size: %x\n", file_size);

  fseek (fp, 0, SEEK_SET);
  bytes = fread (buffer, 1, file_size, fp);
  if (bytes == file_size) {
    //
    // BUG: How to deal with the situation that the update file size doesn't equal to flash size.
    //klutil_ctx.bkfile_size = file_size;
    klutil_ctx.bkfile_size = buff_size;
    klutil_ctx.backup_data  = buffer;
    // DBG_INFO ("Read bios image file OK! image_size = 0x%x\n",klutil_ctx.bkfile_size);
  } else {
    // DBG_INFO ("Failed to read bios image file.\n");
  }
  fclose (fp);  

  return 0;
}

static int get_nvram_data_from_flash (int nvram_offset, char **nvram_data, int size, int *nvram_size)
{  
  *nvram_data = (char *)malloc (size + 1);
  if (!nvram_data) {
    DBG_ERR ("Failed to allocate buffer!\n");
    return 1;
  }
  memset (*nvram_data, 0xFF, size + 1);
  memcpy(*nvram_data, klutil_ctx.backup_data + nvram_offset, size);
  *nvram_size = size;
  return 0;
}

static int
klvars_probe(void)
{
  int   ret;
  unsigned int start = 0;
  unsigned int len = 0;
  //read layout
  layout_from_file(&layout, layoutfile);

  init_context();
  //read nvram
  ret = read_flash_bios_and_backup ();
  if (ret != 0) {
    DBG_ERR ("[%s] Read bios file FAIILED.\n", KLVARS_TOOL_NAME);
    goto err;
  } else {
    DBG_PROG ("[%s] Read bios file OK.\n", KLVARS_TOOL_NAME);
  }
  ret = get_region_range(layout,VARS_LAYOUT_NAME, &start, &len);
  if (ret != 0) {
    DBG_ERR ("[%s] get_region_range FAIILED.\n", KLVARS_TOOL_NAME);
    goto err;
  }
  DBG_PROG ("[%s] NVRAM start=0x%x len=0x%x.\n", KLVARS_TOOL_NAME, start, len);
  // dump_buffer(klutil_ctx.backup_data, 0x100);
  ret = get_nvram_data_from_flash(start, &klutil_ctx.nvram_data, len, &klutil_ctx.nvram_size);
  if (ret != 0) {
    DBG_ERR ("[%s] get_nvram_data_from_flash FAIILED.\n", KLVARS_TOOL_NAME);
    goto err;
  }
  // DBG_PROG ("[%s] klutil_ctx.nvram_size =0x%x.\n", KLVARS_TOOL_NAME, klutil_ctx.nvram_size);
  // dump_buffer(klutil_ctx.nvram_data, 0x100);
  ret = GetAuthenticatedFlag((void*)klutil_ctx.nvram_data);
  ret = parse_nv_frame ((uint8_t *)klutil_ctx.nvram_data, klutil_ctx.nvram_size);
  if (ret != 0) {
    DBG_ERR ("ERROR: parse_nv_frame FAIILED!\n");
    goto err;
  }
  return 1;
err:
  return 0;  
}

static int
klvars_get_variable_size(efi_guid_t guid, const char *name, size_t *size)
{
	int ret = -1;

	uint8_t *data;
	size_t data_size;
	uint32_t attribs;

	ret = uefivar_get_variable(guid, name, &data, &data_size, &attribs);
	if (ret < 0) {
		efi_error("efi_get_variable() failed");
		return ret;
	}

	*size = data_size  + sizeof(attribs);
	if (data)
		free(data);
	return ret;
}

static int
klvars_get_variable_attributes(efi_guid_t guid, const char *name,
			    uint32_t *attributes)
{
	int ret = -1;

	uint8_t *data;
	size_t data_size;
	uint32_t attribs;

	ret = uefivar_get_variable(guid, name, &data, &data_size, &attribs);
	if (ret < 0) {
		efi_error("efi_get_variable() failed");
		return ret;
	}

	*attributes = attribs;
	if (data)
		free(data);
	return ret;
}

static int
klvars_get_variable(efi_guid_t guid, const char *name, uint8_t **data,
		  size_t *data_size, uint32_t *attributes)
{
	int ret = -1;
  ret = uefivar_get_variable(guid, name, data,
		  data_size, attributes);
	return ret;
}

static int store_bios_to_file (uint8_t *buffer, int len);
static int
klvars_del_variable(efi_guid_t guid, const char *name)
{
	int ret = -1;
	uint8_t *buf;
  if (strlen(name) > 1024) {
		efi_error("variable name size is too large (%zd of 1024)",
			  strlen(name));
		errno = EINVAL;
		return -1;
	}
  if (!(buf = calloc(sizeof(uint8_t), klutil_ctx.nvram_size))) {
    DBG_ERR("could not allocate memory");
    return -1;
  }
  memset(buf, 0xFF, klutil_ctx.nvram_size);
	ret = uefivar_del_variable(guid, name, buf, klutil_ctx.nvram_size, (uint8_t *)klutil_ctx.nvram_data, klutil_ctx.nvram_size);

  // DBG_INFO ("[%s] layout->head.start=0x%x !\n", __func__, layout->head->start);
  memcpy( (uint8_t *)klutil_ctx.backup_data + layout->head->start, buf, klutil_ctx.nvram_size);
  if (buf) {
    free(buf);
  }
  
  if(store_bios_to_file ((uint8_t *)klutil_ctx.backup_data, klutil_ctx.bkfile_size)) {
    DBG_ERR ("Generate update file fail.\n");
    return -1;
  } else {
    DBG_PROG ("Generate update file success.\n");
  }
  cleanup_context ();
  clear_list();
  klvars_probe();
	return ret;
}

static int
klvars_chmod_variable(efi_guid_t guid, const char *name, mode_t mode)
{
	int rc = 1;
  efi_guid_t ret_guid = EFI_GLOBAL_GUID;
  if (memcmp(&ret_guid, &guid, sizeof(efi_guid_t)) != 0) {
      
  }
  
  name = name;
  mode =mode;
	return rc;
}

/**
 * @description: 
 *   Save bios image data read from flash chip into a file.
 * 
 * @param {type} 
 * @return: 
 */
static int store_bios_to_file (uint8_t *buffer, int len)
{
  FILE   *fp;

  if (!buffer || len <= 0) {
    DBG_ERR ("Invalid parameters.\n");
    return 1;
  }
  if (!klutil_ctx.generate_file) {
    //
    // Update file path not set, use the default update file.
    klutil_ctx.generate_file = (char *)malloc (PATH_MAX);
    if (!klutil_ctx.generate_file) {
      DBG_ERR ("Failed to allocate buffer!\n");
      return 1;
    }
#if 0    
    memset (klutil_ctx.generate_file, 0, PATH_MAX);
    memcpy (klutil_ctx.generate_file, DEFAULT_BIOS_IMAGE, strlen(DEFAULT_BIOS_IMAGE));
#else
    char src_str[PATH_MAX] = BIOS_GENERATE_PATH;
    strncpy (klutil_ctx.generate_file, src_str, strlen(src_str));
    klutil_ctx.generate_file[strlen(src_str)] = 0;
#endif
  }
  // DBG_INFO ("Generate file: %s\n", klutil_ctx.generate_file);
  
  fp = fopen (klutil_ctx.generate_file, "wb");
  if (!fp) {
    DBG_ERR ("Failed to open flash backup file.\n");
    return 1;
  }
  fwrite (buffer, 1, len, fp);

  fclose (fp);

  return 0;
}

static int
klvars_set_variable(efi_guid_t guid, const char *name, uint8_t *data,
		 size_t data_size, uint32_t attributes, mode_t mode)
{
	// int errno_value;
	// size_t len;
	int ret = -1;
	// int fd = -1;
  uint8_t *old_data;
  size_t old_data_size;
  uint32_t old_attributes;

	if (strlen(name) > 1024) {
		efi_error("variable name size is too large (%zd of 1024)",
			  strlen(name));
		errno = EINVAL;
		return -1;
	}
	if (data_size > 1024) {
		efi_error("variable data size is too large (%zd of 1024)",
			  data_size);
		errno = ENOSPC;
		return -1;
	}
  // DBG_INFO ("[%s] name= %s begin!\n", __func__, name);
  if (strcmp(name, "BootNext") == 0) {
    if(!is_have_name(name)) {
      DBG_ERR("[%s] new_bootnext \n", __func__);
      ret = new_bootnext((uint8_t *)klutil_ctx.nvram_data, klutil_ctx.nvram_size);
      if (ret < 0)
      {
        DBG_ERR("[%s] new_bootnext failed \n", __func__);
        return ret;
      } else {
        DBG_ERR("[%s] renew all \n", __func__);
        // DBG_INFO ("[%s] layout->head.start=0x%x !\n", __func__, layout->head->start);
        memcpy( (uint8_t *)klutil_ctx.backup_data + layout->head->start, (uint8_t *)klutil_ctx.nvram_data, klutil_ctx.nvram_size);
        if(store_bios_to_file ((uint8_t *)klutil_ctx.backup_data, klutil_ctx.bkfile_size)) {
          DBG_ERR ("Generate renew file fail.\n");
          return -1;
        } else {
          DBG_PROG ("Generate renew file success.\n");
        }
        cleanup_context ();
        clear_list();
        klvars_probe();
      }
    }
  }
  //// DBG_INFO ("[%s] name= %s data_size=%zd attributes=0x%x mode=0x%x!\n", __func__, name, data_size, attributes, (int)mode);
  // dump_buffer(data, data_size);
  ret = uefivar_get_variable(guid, name, &old_data,
		      &old_data_size, &old_attributes);
  // dump_buffer(old_data, old_data_size);
  // DBG_INFO ("[%s] data_size=%zd attributes=0x%x !\n", __func__, old_data_size, old_attributes);
	ret = uefivar_set_variable(guid, name, data,
		 data_size, attributes, mode, (uint8_t *)klutil_ctx.nvram_data, klutil_ctx.nvram_size);

  // DBG_INFO ("[%s] layout->head.start=0x%x !\n", __func__, layout->head->start);
  memcpy( (uint8_t *)klutil_ctx.backup_data + layout->head->start, (uint8_t *)klutil_ctx.nvram_data, klutil_ctx.nvram_size);
  if(store_bios_to_file ((uint8_t *)klutil_ctx.backup_data, klutil_ctx.bkfile_size)) {
    DBG_ERR ("Generate update file fail.\n");
    return -1;
  } else {
    DBG_PROG ("Generate update file success.\n");
  }
  cleanup_context ();
  clear_list();
  klvars_probe();
	return ret;
}

static int
klvars_get_next_variable_name(efi_guid_t **guid, char **name)
{
	int rc;
	rc = uefivar_get_next_variable_name(guid, name);
	if (rc < 0)
		efi_error("generic_get_next_variable_name() failed");
	return rc;
}

struct efi_var_operations klvars_ops = {
	.name = "klvars",
	.probe = klvars_probe,
	.set_variable = klvars_set_variable,
	.del_variable = klvars_del_variable,
	.get_variable = klvars_get_variable,
	.get_variable_attributes = klvars_get_variable_attributes,
	.get_variable_size = klvars_get_variable_size,
	.get_next_variable_name = klvars_get_next_variable_name,
	.chmod_variable = klvars_chmod_variable,
};

// vim:fenc=utf-8:tw=75:noet
