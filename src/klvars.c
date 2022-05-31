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
#if DEBUG
#define msg_gdbg printf //efi_error
#define DBG_INFO printf //efi_error
#define DBG_PROG printf //efi_error
#else
#define msg_gdbg
#define DBG_INFO
#define DBG_PROG
#endif
#define KLVARS_TOOL_NAME "klvars"
#define BIOS_BACKUP_PATH "./rom.bin"
#define VARS_LAYOUT_NAME "NVRAM"

static	LIST_HEAD(klvars_list);

KLSETUP_VAR_INFO var_info = {0};

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

static const char default_klvars_path[] = "/sys/firmware/efi/vars/";

static const char *
get_klvars_path(void)
{
	static const char *path;
	if (path)
		return path;

	path = getenv("klvars_PATH");
	if (!path)
		path = default_klvars_path;
	return path;
}


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
#if 0
static ssize_t
get_file_data_size(int dfd, char *name)
{
	char raw_var[NAME_MAX + 9];

	memset(raw_var, '\0', sizeof (raw_var));
	strncpy(raw_var, name, NAME_MAX);
	strcat(raw_var, "/raw_var");

	int fd = openat(dfd, raw_var, O_RDONLY);
	if (fd < 0) {
		efi_error("openat failed");
		return -1;
	}

	char buf[4096];
	ssize_t sz, total = 0;
	int tries = 5;
	while (1) {
		sz = read(fd, buf, 4096);
		if (sz < 0 && (errno == EAGAIN || errno == EINTR)) {
			if (tries--)
				continue;
			total = -1;
			break;
		}

		if (sz < 0) {
			int saved_errno = errno;
			close(fd);
			errno = saved_errno;
			return -1;
		}

		if (sz == 0)
			break;
		total += sz;
	}
	close(fd);
	return total;
}

/*
 * Determine which ABI the kernel has given us.
 *
 * We have two situations - before and after kernel's commit e33655a38.
 * Before hand, the situation is like:
 * 64-on-64 - 64-bit DataSize and status
 * 32-on-32 - 32-bit DataSize and status
 * 32-on-64 - 64-bit DataSize and status
 *
 * After it's like this if CONFIG_COMPAT is enabled:
 * 64-on-64 - 64-bit DataSize and status
 * 32-on-64 - 32-bit DataSize and status
 * 32-on-32 - 32-bit DataSize and status
 *
 * Is there a better way to figure this out?
 * Submit your patch here today!
 */
static int
is_64bit(void)
{
	static int sixtyfour_bit = -1;
	DIR *dir = NULL;
	int dfd = -1;
	int saved_errno;

	if (sixtyfour_bit != -1)
		return sixtyfour_bit;

	dir = opendir(get_klvars_path());
	if (!dir)
		goto err;

	dfd = dirfd(dir);
	if (dfd < 0)
		goto err;

	while (1) {
		struct dirent *entry = readdir(dir);
		if (entry == NULL)
			break;

		if (!strcmp(entry->d_name, "..") || !strcmp(entry->d_name, "."))
			continue;

		ssize_t size = get_file_data_size(dfd, entry->d_name);
		if (size < 0) {
			continue;
		} else if (size == 2084) {
			sixtyfour_bit = 1;
		} else {
			sixtyfour_bit = 0;
		}

		errno = 0;
		break;
	}
	if (sixtyfour_bit == -1)
		sixtyfour_bit = __SIZEOF_POINTER__ == 4 ? 0 : 1;
err:
	saved_errno = errno;

	if (dir)
		closedir(dir);

	errno = saved_errno;
	return sixtyfour_bit;
}
#endif
static int
get_size_from_file(const char *filename, size_t *retsize)
{
	uint8_t *buf = NULL;
	size_t bufsize = -1;
	int errno_value;
	int ret = -1;
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		efi_error("open(%s, O_RDONLY) failed", filename);
		goto err;
	}

	int rc = read_file(fd, &buf, &bufsize);
	if (rc < 0) {
		efi_error("read_file(%s) failed", filename);
		goto err;
	}

	long long size = strtoll((char *)buf, NULL, 0);
	if ((size == LLONG_MIN || size == LLONG_MAX) && errno == ERANGE) {
		*retsize = -1;
	} else if (size < 0) {
		*retsize = -1;
	} else {
		*retsize = (size_t)size;
		ret = 0;
	}
err:
	errno_value = errno;

	if (fd >= 0)
		close(fd);

	if (buf != NULL)
		free(buf);

	errno = errno_value;
	return ret;
}

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
  DBG_PROG("[%s] Reading spi flash...\n", KLVARS_TOOL_NAME);

  if (!klutil_ctx.backup_file) {
      DBG_ERR ("ERR: klutil_ctx.backup_file null!\n");
      return 1;
  }
  DBG_INFO ("Update file: %s\n", klutil_ctx.backup_file);

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

  DBG_INFO ("file_size: %x\n", file_size);

  fseek (fp, 0, SEEK_SET);
  bytes = fread (buffer, 1, file_size, fp);
  if (bytes == file_size) {
    //
    // BUG: How to deal with the situation that the update file size doesn't equal to flash size.
    //klutil_ctx.bkfile_size = file_size;
    klutil_ctx.bkfile_size = buff_size;
    klutil_ctx.backup_data  = buffer;
    DBG_INFO ("Read bios image file OK! image_size = 0x%x\n",klutil_ctx.bkfile_size);
  } else {
    DBG_INFO ("Failed to read bios image file.\n");
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
  dump_buffer(klutil_ctx.backup_data, 0x100);
  ret = get_nvram_data_from_flash(start, &klutil_ctx.nvram_data, len, &klutil_ctx.nvram_size);
  if (ret != 0) {
    DBG_ERR ("[%s] get_nvram_data_from_flash FAIILED.\n", KLVARS_TOOL_NAME);
    goto err;
  }
  DBG_PROG ("[%s] klutil_ctx.nvram_size =0x%x.\n", KLVARS_TOOL_NAME, klutil_ctx.nvram_size);
  dump_buffer(klutil_ctx.nvram_data, 0x100);
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
	int errno_value;
	int ret = -1;

	char *path = NULL;
	int rc = asprintf(&path, "%s%s-"GUID_FORMAT"/size", get_klvars_path(),
			  name, GUID_FORMAT_ARGS(&guid));
	if (rc < 0) {
		efi_error("asprintf failed");
		goto err;
	}

	size_t retsize = 0;
	rc = get_size_from_file(path, &retsize);
	if (rc >= 0) {
		ret = 0;
		*size = retsize;
	} else if (rc < 0) {
		efi_error("get_size_from_file(%s) failed", path);
	}
err:
	errno_value = errno;

	if (path)
		free(path);

	errno = errno_value;
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

static int
klvars_del_variable(efi_guid_t guid, const char *name)
{
	int errno_value;
	int ret = -1;
	char *path = NULL;
	int rc;
	int fd = -1;
	uint8_t *buf = NULL;
	size_t buf_size = 0;
	char *delvar;

	rc = asprintf(&path, "%s%s-" GUID_FORMAT "/raw_var", get_klvars_path(),
		      name, GUID_FORMAT_ARGS(&guid));
	if (rc < 0) {
		efi_error("asprintf failed");
		goto err;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		efi_error("open(%s, O_RDONLY) failed", path);
		goto err;
	}

	rc = read_file(fd, &buf, &buf_size);
	buf_size -= 1; /* read_file pads out 1 extra byte to NUL it */
	if (rc < 0) {
		efi_error("read_file(%s) failed", path);
		goto err;
	}

	if (buf_size != sizeof(efi_kernel_variable_64_t) &&
		       buf_size != sizeof(efi_kernel_variable_32_t)) {
		efi_error("variable size %zd is not 32-bit (%zd) or 64-bit (%zd)",
			  buf_size, sizeof(efi_kernel_variable_32_t),
			  sizeof(efi_kernel_variable_64_t));

		errno = EFBIG;
		goto err;
	}

	if (asprintfa(&delvar, "%s%s", get_klvars_path(), "del_var") < 0) {
		efi_error("asprintfa() failed");
		goto err;
	}

	close(fd);
	fd = open(delvar, O_WRONLY);
	if (fd < 0) {
		efi_error("open(%s, O_WRONLY) failed", delvar);
		goto err;
	}

	rc = write(fd, buf, buf_size);
	if (rc >= 0)
		ret = 0;
	else
		efi_error("write() failed");
err:
	errno_value = errno;

	if (buf)
		free(buf);

	if (fd >= 0)
		close(fd);

	if (path)
		free(path);

	errno = errno_value;
	return ret;
}

static int
_klvars_chmod_variable(char *path, mode_t mode)
{
	mode_t mask = umask(umask(0));
	char *files[] = {
		"", "attributes", "data", "guid", "raw_var", "size", NULL
		};

	int saved_errno = 0;
	int ret = 0;
	for (int i = 0; files[i] != NULL; i++) {
		char *new_path = NULL;
		int rc = asprintf(&new_path, "%s/%s", path, files[i]);
		if (rc > 0) {
			rc = chmod(new_path, mode & ~mask);
			if (rc < 0) {
				if (saved_errno == 0)
					saved_errno = errno;
				ret = -1;
			}
			free(new_path);
		} else if (rc < 0) {
			if (saved_errno == 0)
				saved_errno = errno;
			ret = -1;
		}
	}
	errno = saved_errno;
	return ret;
}

static int
klvars_chmod_variable(efi_guid_t guid, const char *name, mode_t mode)
{
	if (strlen(name) > 1024) {
		errno = EINVAL;
		return -1;
	}

	char *path;
	int rc = asprintf(&path, "%s%s-" GUID_FORMAT, get_klvars_path(),
			  name, GUID_FORMAT_ARGS(&guid));
	if (rc < 0) {
		efi_error("asprintf failed");
		return -1;
	}

	rc = _klvars_chmod_variable(path, mode);
	int saved_errno = errno;
	efi_error("_klvars_chmod_variable() failed");
	free(path);
	errno = saved_errno;
	return rc;
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
  DBG_INFO ("[%s] name= %s data_size=%zd attributes=0x%x mode=0x%x!\n", __func__, name, data_size, attributes, (int)mode);
  dump_buffer(data, data_size);
  ret = uefivar_get_variable(guid, name, &old_data,
		      &old_data_size, &old_attributes);
  DBG_INFO ("[%s] data_size=%zd attributes=0x%x !\n", __func__, old_data_size, old_attributes);
	
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
