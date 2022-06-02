/*++ 

Copyright (c) 2005-2021, Kunlun BIOS, ZD Technology (Beijing) Co., Ltd.. All 
Rights Reserved.                                                             

You may not reproduce, distribute, publish, display, perform, modify, adapt, 
transmit, broadcast, present, recite, release, license or otherwise exploit  
any part of this publication in any form, by any means, without the prior    
written permission of ZD Technology (Beijing) Co., Ltd..                     

Module Name:

  uefivar.c

Abstract: 


Revision History:


--*/

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
#include <stddef.h>
#include <ctype.h>
#include "uefivar.h"
#include "list.h"
#include "guid.h"

#define DEBUG 0
#define msg_gerr printf //efi_error
#define DBG_ERR printf //efi_error

#define msg_gdbg printf //efi_error
#define DBG_INFO printf //efi_error
#define DBG_PROG printf //efi_error

#define BIT0 0x00000001
#define BYTES_PER_LINE           16

void dump_buffer (void *buffer, int len)
{
  int   index;

  for (index = 0; index < len; index++) {
    DBG_INFO ("%02x ", ((uint8_t *)buffer)[index]);
    if (0 == (index + 1) % BYTES_PER_LINE) {
      DBG_INFO ("\n");
    }
  }
  DBG_INFO ("\n");
}

#define VAR_TYPE_MAX                             2

/* global variables */
static	LIST_HEAD(var_list);

typedef struct _klvar_entry {
  uint32_t  type;
  uint32_t  offset;
  uint32_t  totalSize;
  uint32_t  headerSize;
  uint32_t  nameSize;
  uint32_t  dataSize;
  char      *header;
  char      *name;
  char      *data;
  list_t    list;
} klvar_entry_t;
klvar_entry_t klvar_entry;

UINT8   var_type                               = 0;

const EFI_GUID VariableGuidGroup[VAR_TYPE_MAX] = {
{0xDDCF3616 , 0x3275 , 0x4164 , {0x98 , 0xB6 , 0xFE , 0x85 , 0x70 , 0x7F , 0xFE , 0x7D} },
{0xAAF32C78 , 0x947B , 0x439A , {0xA1 , 0x80 , 0x2E , 0x14 , 0x4E , 0xC3 , 0x77 , 0x92} }
};

//"FFF12B8D-7696-4C8B-A985-2747075B4F50";
const EFI_GUID var_nv_guid = {0xFFF12B8D , 0x7696 , 0x4C8B , \
                              {0xA9 , 0x85 , 0x27 , 0x47 , 0x07 , 0x5B , 0x4F , 0x50} };
EFI_FIRMWARE_VOLUME_HEADER  gVolumeHeader = {0};
VARIABLE_STORE_HEADER       gStoreHeader  = {0};
#define DEFAULT_VARIABLE_TYPE                      0
#define AUTHENTICATED_VARIABLE_TYPE                1

//The structure of the parse Variable data is determined by EFI_FIRMWARE_VOLUME_HEADER

/*
Routine Description:
  This function The structure of the parse Variable data is determined by EFI_FIRMWARE_VOLUME_HEADER

Arguments:
  GuidPtr - EFI_FIRMWARE_VOLUME_HEADER FileSystemGuid pointer

Returns:
  0     - DEFAULT_VARIABLE_TYPE  use VARIABLE_HEADER Struct defined Data .
  1     - AUTHENTICATED_VARIABLE_TYPE use AUTHENTICATED_VARIABLE_HEADER Struct defined Data .

*/
UINT8 GetAuthenticatedFlag(void * FvHeader )
{
  for(var_type = 0 ; var_type < VAR_TYPE_MAX ; var_type++)
  {
    if(memcmp((void*)((UINTN)FvHeader + ((EFI_FIRMWARE_VOLUME_HEADER*)FvHeader)->HeaderLength) , (void *)&VariableGuidGroup[var_type] , sizeof(EFI_GUID)) == 0)
    {
      break;
    }
  }
  return var_type;
}

CHAR8 *
EFIAPI
UnicodeStrToAsciiStr (
  IN      CONST CHAR16              *Source,
  OUT     CHAR8                     *Destination
  )
{
  CHAR8                               *ReturnValue;

  ReturnValue = Destination;
  while (*Source != '\0') {
    *(Destination++) = (CHAR8) *(Source++);
  }

  *Destination = '\0';

  return ReturnValue;
}

static int chk_nvfv (uint8_t *nv_fv_buff, uint32_t buff_len)
{
    if (!nv_fv_buff || !buff_len) {
    return 1;
  }
  return 0;
}

nvType getNvType(uint8_t *buffer, EFI_GUID guid) {

	nvType currtype = UN_KNOWN;
  EFI_FIRMWARE_VOLUME_HEADER *volumeHeader = (EFI_FIRMWARE_VOLUME_HEADER *)buffer;
  if(memcmp((void*)&(volumeHeader->FileSystemGuid) , (void *)&guid, sizeof(EFI_GUID)) == 0)
  {
    currtype = VOLUME_HEADER;
  } else {
    currtype = STORE_HEADER;
  }

  return currtype;
}

int getVarHeaderInfo( uint8_t *nv_fv_buff, uint32_t buff_len) {
  if (!nv_fv_buff || (buff_len<sizeof(EFI_FIRMWARE_VOLUME_HEADER)))
  {
    DBG_ERR ("[%s]Inalid NV_FV!\n",__func__);
    return 1;
  }
  // EFI_FIRMWARE_VOLUME_HEADER *volumeHeader = (EFI_FIRMWARE_VOLUME_HEADER *)buffer;
  memcpy(&gVolumeHeader, nv_fv_buff, sizeof(EFI_FIRMWARE_VOLUME_HEADER));
  #if DEBUG 
  dump_buffer(&gVolumeHeader, sizeof(gVolumeHeader));
  #endif
  return 0;
}

int getVarStoreHeaderInfo( uint8_t *nv_fv_buff, uint32_t buff_len) {
  if (!nv_fv_buff || (buff_len - sizeof(EFI_FIRMWARE_VOLUME_HEADER) < sizeof(VARIABLE_STORE_HEADER)))
  {
    DBG_ERR ("[%s]Inalid STORE FV!\n",__func__);
    return 1;
  }
  if (!gVolumeHeader.HeaderLength)
  {
    DBG_ERR ("[%s]gVolumeHeader.HeaderLength is Inalid!\n",__func__);
    return 2;
  }
  memcpy(&gStoreHeader, nv_fv_buff + gVolumeHeader.HeaderLength, sizeof(VARIABLE_STORE_HEADER));
  #if DEBUG 
  dump_buffer(&gStoreHeader, sizeof(gStoreHeader));
  #endif
  
  return 0;
}

#define NORMAL_VAR_TYPE 0
#define AUTH_VAR_TYPE   1
#define INVALID_START_ID (0xFFFF)
#define VALID_START_ID (0x55AA)

int getVariableInfo( uint8_t *nv_fv_buff, uint32_t buff_len, list_t *head) {
  nvType nvtype;
  UINT8 varType;
  uint32_t offset = gVolumeHeader.HeaderLength + sizeof(VARIABLE_STORE_HEADER);
  //uint32_t size = 0;
  uint32_t hdrsize;
  
  if (!nv_fv_buff || (buff_len < sizeof(EFI_FIRMWARE_VOLUME_HEADER) + sizeof(VARIABLE_STORE_HEADER)))
  {
    DBG_ERR ("[%s]Inalid STORE FV!\n",__func__);
    return 1;
  }
  if (!gVolumeHeader.HeaderLength)
  {
    DBG_ERR ("[%s]gVolumeHeader.HeaderLength is Inalid!\n",__func__);
    return 2;
  }

  nvtype = getNvType( nv_fv_buff, var_nv_guid);
  if (nvtype != VOLUME_HEADER)
  {
    DBG_ERR ("[%s] nvtype=%d ERR!\n", __func__, nvtype);
    return 3;
  }
  varType = GetAuthenticatedFlag((void *) nv_fv_buff);
  #if DEBUG 
  DBG_INFO ("[%s] varType=%d !\n", __func__, varType); 
  #endif
  if (varType == NORMAL_VAR_TYPE) {
    hdrsize = sizeof(DEFAULT_VARIABLE_HEADER);
  } else if ( varType == AUTH_VAR_TYPE ) {
    hdrsize = sizeof(AUTHENTICATED_VARIABLE_HEADER);
  } else {
    DBG_ERR ("[%s] varType=%d ERR!\n", __func__, varType);
    return 4;
  }
  #if DEBUG 
  DBG_INFO ("[%s]AUTHENTICATED offset=0x%x !\n", __func__, offset); 
  dump_buffer(nv_fv_buff+offset, 0x100);
  #endif
  while (offset < (gStoreHeader.Size + gVolumeHeader.HeaderLength))
  {
    klvar_entry_t *entry;
    entry = calloc(1, sizeof(klvar_entry_t));
    if (!entry) {
      DBG_ERR("calloc(1, %zd) failed", sizeof(klvar_entry_t));
      return 6;
    }
    entry->type = (uint32_t)varType;
    if (entry->type == NORMAL_VAR_TYPE) {
      entry->offset = offset;
      entry->headerSize = sizeof(DEFAULT_VARIABLE_HEADER);
      entry->header = (char *)malloc (sizeof(DEFAULT_VARIABLE_HEADER));
      #if DEBUG 
      DBG_INFO ("[%s]DEFAULT_VARIABLE offset=0x%x !\n", __func__, offset);  
      #endif
      memcpy( entry->header, nv_fv_buff+offset, hdrsize);
      offset += hdrsize;
      #if DEBUG 
      dump_buffer(entry->header, hdrsize); 
      #endif
      if (((DEFAULT_VARIABLE_HEADER *)entry->header)->StartId == INVALID_START_ID) {
        break;
      }
      if (((DEFAULT_VARIABLE_HEADER *)entry->header)->StartId != VALID_START_ID) {
        #if DEBUG 
        DBG_INFO ("[%s]START_ID ERR, StartId=0x%x !\n", __func__, ((DEFAULT_VARIABLE_HEADER *)entry->header)->StartId); 
        #endif
        return 5;
      }
      entry->nameSize = ((DEFAULT_VARIABLE_HEADER *)entry->header)->NameSize;
      entry->dataSize = ((DEFAULT_VARIABLE_HEADER *)entry->header)->DataSize;
    } else if ( entry->type == AUTH_VAR_TYPE ) {
      entry->offset = offset;
      entry->headerSize = sizeof(AUTHENTICATED_VARIABLE_HEADER);
      entry->header = (char *)malloc (sizeof(AUTHENTICATED_VARIABLE_HEADER));
      #if DEBUG 
      DBG_INFO ("[%s]AUTHENTICATED_VARIABLE offset=0x%x !\n", __func__, offset);  
      #endif
      memcpy( entry->header, nv_fv_buff+offset, hdrsize);
      offset += hdrsize;
      #if DEBUG 
      dump_buffer(entry->header, hdrsize);
      #endif
      if (((AUTHENTICATED_VARIABLE_HEADER *)entry->header)->StartId == INVALID_START_ID) {
        break;
      }
      if (((AUTHENTICATED_VARIABLE_HEADER *)entry->header)->StartId != VALID_START_ID) {
        #if DEBUG 
        DBG_INFO ("[%s]START_ID ERR, StartId=0x%x !\n", __func__, ((AUTHENTICATED_VARIABLE_HEADER *)entry->header)->StartId); 
        #endif
        return 5;
      }
      entry->nameSize = ((AUTHENTICATED_VARIABLE_HEADER *)entry->header)->NameSize;
      entry->dataSize = ((AUTHENTICATED_VARIABLE_HEADER *)entry->header)->DataSize;
    }
    entry->name = (char *)malloc( entry->nameSize);
    memcpy( entry->name, nv_fv_buff+offset, entry->nameSize);
    offset += entry->nameSize;
    #if DEBUG
    dump_buffer(entry->name, entry->nameSize);
    #endif

    entry->data = (char *)malloc( entry->dataSize);
    memcpy( entry->data, nv_fv_buff+offset, entry->dataSize);
    offset += entry->dataSize;
    #if DEBUG 
    dump_buffer(entry->data, entry->dataSize);
    #endif
    
    offset = _INTSIZEOF(offset);
    entry->totalSize = offset - entry->offset;

    //add to list
    list_add_tail(&(entry->list), head);
  }
  #if DEBUG 
  DBG_INFO ("[%s] end !\n", __func__); 
  list_t *pos;
  klvar_entry_t *var;
  list_for_each(pos, head) {
    var = list_entry(pos, klvar_entry_t, list);
    
    DBG_INFO ("[%s] var->offset= 0x%x !\n", __func__, var->offset); 
    
  }
  #endif
  return 0;
}
// parse the data of raw variable data, i.e. data from the VARIABLE area of the BIOS.
// pick out data of variables named "UserInfoLog" and "SecurityInfoLog" and store 
// them into a temp file.
int parse_nv_frame(uint8_t *nv_fv_buff, uint32_t buff_len) {
  nvType nvtype;
  int ret;
  if (chk_nvfv (nv_fv_buff, buff_len) != 0) {
    DBG_ERR ("Inalid NV_FV!\n");
    return 1;
  }
  nvtype = getNvType( nv_fv_buff, var_nv_guid);
  if (nvtype == VOLUME_HEADER) {
    ret = getVarHeaderInfo( nv_fv_buff, buff_len);
    if (ret) {
      return ret;
    }
    ret = getVarStoreHeaderInfo( nv_fv_buff, buff_len);
    if (ret) {
      return ret;
    }
    //
    GetAuthenticatedFlag((void *)nv_fv_buff);
    #if DEBUG 
    DBG_INFO ("[%s] var_type=%d !\n", __func__, var_type); 
    #endif
    getVariableInfo(nv_fv_buff, buff_len, &var_list);
    #if DEBUG 
    DBG_INFO ("[%s] list size =%zd !\n", __func__, list_size(&var_list)); 
    #endif

  } else if (nvtype == STORE_HEADER) {
    DBG_ERR ("[%s]Not support nv type(STORE_HEADER)!\n",__func__);
    return 2;
  } else {
    DBG_ERR ("[%s]Not support nv type !\n",__func__);
    return 3;
  }
  return 0;
}

int is_have_name(const char *name) {
  int ret = 0;
  list_t *pos;
  uint8_t state = 0;
  char ret_name[NAME_MAX+1] = {0};
  klvar_entry_t *var;
  
  #if DEBUG 
  DBG_INFO ("[%s] name= %s begin!\n", __func__, name); 
  #endif
  list_for_each(pos, &var_list) {
    var = list_entry(pos, klvar_entry_t, list);
    UnicodeStrToAsciiStr ((CONST CHAR16 *)var->name, ret_name);
    if (var->type == NORMAL_VAR_TYPE) {
        state = ((DEFAULT_VARIABLE_HEADER *)var->header)->State;
    } else if (var->type == AUTH_VAR_TYPE) {
        state = ((DEFAULT_VARIABLE_HEADER *)var->header)->State;
    }
    if (state != 0x3F) {
      #if DEBUG 
      DBG_INFO ("[%s] state= 0x%x !\n", __func__, state); 
      #endif
      continue;
    }
    if (strcmp(name, ret_name) == 0) {
      ret = 1;
      break;
    }
    
  }
  return ret;
}

int new_bootnext(uint8_t *nv_fv_buff, uint32_t buff_len) {
  klvar_entry_t *var;
  uint32_t  offset;
  uint16_t  data = 0;
  char name[] = {0x42, 0x00, 0x6F, 0x00, 0x6F, 0x00, 0x74, 0x00, 0x4E, 0x00, 0x65, 0x00, 0x78, 0x00, 0x74, 0x00, 0x00, 0x00};//L"BootNext"
  uint32_t  type = (uint32_t)GetAuthenticatedFlag((void *) nv_fv_buff);
  var = list_last_entry(&var_list, klvar_entry_t, list);
  offset = var->offset + var->totalSize;

  if(type == NORMAL_VAR_TYPE) {
    DEFAULT_VARIABLE_HEADER header = {
      .StartId = 0x55AA,
      .State = 0x3F,
      .Reserved = 0,
      .Attributes = 0x07,
      .NameSize = sizeof(name),
      .DataSize = sizeof(data),
      .VendorGuid = {0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c}},
    };
    if (offset + sizeof(DEFAULT_VARIABLE_HEADER) + header.NameSize + header.DataSize > gStoreHeader.Size + gVolumeHeader.HeaderLength)
    {
      DBG_ERR ("[%s] DEFAULT_VARIABLE offset=0x%x over !\n", __func__, offset);
      return -1;
    }
        if (offset + sizeof(DEFAULT_VARIABLE_HEADER) + header.NameSize + header.DataSize > buff_len)
    {
      DBG_ERR ("[%s] DEFAULT_VARIABLE offset=0x%x over !\n", __func__, offset);
      return -1;
    }
    memcpy(nv_fv_buff + offset, &header, sizeof(DEFAULT_VARIABLE_HEADER));
    memcpy(nv_fv_buff + offset + sizeof(DEFAULT_VARIABLE_HEADER), name, header.NameSize);
    memcpy(nv_fv_buff + offset + sizeof(DEFAULT_VARIABLE_HEADER) + header.NameSize, name, header.DataSize);
  } else if ( type == AUTH_VAR_TYPE ) {
    AUTHENTICATED_VARIABLE_HEADER header = {
      .StartId = 0x55AA,
      .State = 0x3F,
      .Reserved = 0,
      .Attributes = 0x07,
      .MonotonicCount = 0,
      .TimeStamp ={0},
      .PubKeyIndex = 0,
      .NameSize = sizeof(name),
      .DataSize = sizeof(data),
      .VendorGuid = {0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c}},
    };
    if (offset + sizeof(AUTHENTICATED_VARIABLE_HEADER) + header.NameSize + header.DataSize > gStoreHeader.Size + gVolumeHeader.HeaderLength)
    {
      DBG_ERR ("[%s] DEFAULT_VARIABLE offset=0x%x over !\n", __func__, offset);
      return -1;
    }
    memcpy(nv_fv_buff + offset, &header, sizeof(AUTHENTICATED_VARIABLE_HEADER));
    memcpy(nv_fv_buff + offset + sizeof(AUTHENTICATED_VARIABLE_HEADER), name, header.NameSize);
    memcpy(nv_fv_buff + offset + sizeof(AUTHENTICATED_VARIABLE_HEADER) + header.NameSize, name, header.DataSize);
  }
  return 0;
}

int uefivar_set_variable(efi_guid_t guid, const char *name, uint8_t *data,
		 size_t data_size, uint32_t attributes, mode_t mode, uint8_t *nv_fv_buff, uint32_t buff_len) {
  int ret = -1;
  //static size_t npos = 0;
  list_t *pos;
  uint8_t state = 0;
  char ret_name[NAME_MAX+1] = {0};
	efi_guid_t ret_guid;
  klvar_entry_t *var;
  uint32_t attr;
  char *databk;
  mode = mode;

  list_for_each(pos, &var_list) {
    var = list_entry(pos, klvar_entry_t, list);
    UnicodeStrToAsciiStr ((CONST CHAR16 *)var->name, ret_name);
    // #if DEBUG DBG_INFO ("[%s] ret_name= %s !\n", __func__, ret_name); #endif
    if (var->type == NORMAL_VAR_TYPE) {
        memcpy(&ret_guid, &((DEFAULT_VARIABLE_HEADER *)var->header)->VendorGuid, sizeof(efi_guid_t));
        attr = ((DEFAULT_VARIABLE_HEADER *)var->header)->Attributes;
        state = ((DEFAULT_VARIABLE_HEADER *)var->header)->State;
    } else if (var->type == AUTH_VAR_TYPE) {
        memcpy(&ret_guid, &((AUTHENTICATED_VARIABLE_HEADER *)var->header)->VendorGuid, sizeof(efi_guid_t));
        attr = ((AUTHENTICATED_VARIABLE_HEADER *)var->header)->Attributes;
        state = ((DEFAULT_VARIABLE_HEADER *)var->header)->State;
    }
    if (state != 0x3F) {
      #if DEBUG 
      DBG_INFO ("[%s] state= 0x%x !\n", __func__, state); 
      #endif
      continue;
    }

    if (memcmp(&ret_guid, &guid, sizeof(efi_guid_t)) != 0) {
      continue;
    }

    if (strcmp(name, ret_name) != 0) {
      continue;
    }

    #if DEBUG 
    DBG_INFO ("[%s] attr= 0x%x to 0x%x !\n", __func__, attr, attributes); 
    #endif
    if (attr != attributes) {
      #if DEBUG 
      DBG_INFO ("[%s] change attr= 0x%x to 0x%x !\n", __func__, attr, attributes); 
      #endif
      if (var->type == NORMAL_VAR_TYPE) {
          ((DEFAULT_VARIABLE_HEADER *)var->header)->Attributes = attributes;
      } else if (var->type == AUTH_VAR_TYPE) {
          ((AUTHENTICATED_VARIABLE_HEADER *)var->header)->Attributes = attributes;
      }
      memcpy( nv_fv_buff + var->offset, var->header, var->headerSize);
    }

    if (var->dataSize < data_size)
    {
      DBG_ERR("[%s] var->dataSize is not enough !\n", __func__);
      return -2;
    }
    //change data
    if (strcmp(name, "BootOrder") == 0)
    {
      if (data_size == var->dataSize)
      {
        memcpy(var->data, data, data_size);
      } else if (data_size > var->dataSize) {
        //shang mian yi jing chuli
      } else {
        if (!(databk = calloc(var->dataSize, sizeof (uint8_t)))) {
          DBG_ERR("could not allocate memory");
          return -1;
        }
        memcpy(databk, var->data, var->dataSize);
        memcpy(var->data, data, data_size);
        int skip;
        uint32_t size = (uint32_t)data_size;
        for (uint32_t i = 0; i < var->dataSize/2; i++)
        {
          for (uint32_t j = 0; j < data_size/2; j++)
          {
            if (((uint16_t *)databk)[i] == ((uint16_t *)var->data)[j]) {
              skip = 1;
            }
          }
          if (!skip) {
            memcpy( var->data + size, &((uint16_t *)databk)[i], sizeof(uint16_t));
            size += sizeof(uint16_t);
          }          
          skip = 0;
        }
      }
    } else {
      memcpy(var->data, data, data_size);
    }
    if (var->offset + var->headerSize + var->nameSize + var->dataSize > buff_len)
    {
      DBG_ERR("[%s] > buf_len !\n", __func__);
      return -1;
    }
    
    memcpy( nv_fv_buff + var->offset + var->headerSize + var->nameSize, var->data, var->dataSize);
    #if DEBUG 
    dump_buffer(nv_fv_buff + var->offset + var->headerSize + var->nameSize, var->dataSize);
    #endif
    ret = 1;
    return ret;
  }
  #if DEBUG 
  DBG_INFO("[%s] no match !\n", __func__); 
  #endif

  return -1;
}

static int is_bootx(const char *name, int *order) {
  char boot[] = "Boot";
	size_t plen = strlen(boot);
	const char *num = name + plen;
	if (!strncmp(name, boot, plen) &&
      isxdigit(num[0]) && isxdigit(num[1]) &&
      isxdigit(num[2]) && isxdigit(num[3]) ) {
      *order = atoi(num);
      #if DEBUG 
      DBG_INFO ("[%s] order= %d !\n", __func__, *order); 
      #endif
    return 1;
  }
		return 0;  
}

int uefivar_del_variable(efi_guid_t guid, const char *name, uint8_t *buffer, int len, uint8_t *nv_fv_buff, uint32_t buff_len) {
  int ret = -1;
  list_t *pos;
  // uint8_t state = 0;
  char ret_name[NAME_MAX+1] = {0};
	efi_guid_t ret_guid;
  klvar_entry_t *var;
  // uint32_t attr;
  int buflen = len - len;
  int order;
  #if DEBUG 
  DBG_INFO ("[%s] name= %s begin!\n", __func__, name); 
  #endif
  buflen = gVolumeHeader.HeaderLength + sizeof(VARIABLE_STORE_HEADER);
  memcpy(buffer , nv_fv_buff, buflen);
  list_for_each(pos, &var_list) {
    var = list_entry(pos, klvar_entry_t, list);
    UnicodeStrToAsciiStr ((CONST CHAR16 *)var->name, ret_name);
    #if DEBUG 
    DBG_INFO ("[%s] ret_name= %s !\n", __func__, ret_name); 
    #endif
    if (var->type == NORMAL_VAR_TYPE) {
        memcpy(&ret_guid, &((DEFAULT_VARIABLE_HEADER *)var->header)->VendorGuid, sizeof(efi_guid_t));
    } else if (var->type == AUTH_VAR_TYPE) {
        memcpy(&ret_guid, &((AUTHENTICATED_VARIABLE_HEADER *)var->header)->VendorGuid, sizeof(efi_guid_t));
    }
    
    if ((memcmp(&ret_guid, &guid, sizeof(efi_guid_t)) == 0) && (strcmp(name, ret_name) == 0)) {
      #if DEBUG 
      DBG_INFO ("[%s] name continue = %s ret_name= %s !\n", __func__, name, ret_name); 
      #endif
      continue;
    }

    //change data
    if ((strcmp(ret_name, "BootOrder") == 0) && is_bootx(name, &order))
    {
      int lenhn = 0;
      if (var->type == NORMAL_VAR_TYPE) {
        lenhn = sizeof(DEFAULT_VARIABLE_HEADER) + var->nameSize;
      } else if (var->type == AUTH_VAR_TYPE) {
        lenhn = sizeof(AUTHENTICATED_VARIABLE_HEADER) + var->nameSize;
      }
      memcpy(buffer + buflen, nv_fv_buff + var->offset, lenhn);

      for (size_t i = 0; i < var->dataSize/2; i++)
      {
        if (((uint16_t *)var->data)[i] != (uint16_t)order) {
          memcpy(buffer + buflen + lenhn, var->data + sizeof(uint16_t) * i, sizeof(uint16_t));
          lenhn += sizeof(uint16_t);
        }
      }
      
      //change data size
      if (var->type == NORMAL_VAR_TYPE) {
        ((DEFAULT_VARIABLE_HEADER *)(buffer + buflen))->DataSize = lenhn - sizeof(DEFAULT_VARIABLE_HEADER) - var->nameSize;
      } else if (var->type == AUTH_VAR_TYPE) {
        ((AUTHENTICATED_VARIABLE_HEADER *)(buffer + buflen))->DataSize = lenhn - sizeof(AUTHENTICATED_VARIABLE_HEADER) - var->nameSize;
      }
      buflen += lenhn;
      buflen = _INTSIZEOF(buflen);
    } else {
      memcpy(buffer + buflen, nv_fv_buff + var->offset, var->totalSize);
      buflen += var->totalSize;
    }
    if ((uint32_t)buflen > buff_len)
    {
      #if DEBUG 
      DBG_INFO("[%s] ERR: buflen > buff_len  !\n", __func__); 
      #endif
      ret = -1;
      return ret;
    }
    
  }
  #if DEBUG 
  DBG_INFO("[%s] finish !\n", __func__); 
  #endif
  ret = 1;
  return ret;
}

int uefivar_get_variable(efi_guid_t guid, const char *name, uint8_t **data,
		  size_t *data_size, uint32_t *attributes) {
  int ret = -1;
  //static size_t npos = 0;
  list_t *pos;
  uint8_t *newbuf;
  uint8_t state = 0;
  char ret_name[NAME_MAX+1] = {0};
	efi_guid_t ret_guid;
  klvar_entry_t *var;
  // #if DEBUG DBG_INFO ("[%s] name= %s begin!\n", __func__, name); #endif
  list_for_each(pos, &var_list) {
    var = list_entry(pos, klvar_entry_t, list);
    UnicodeStrToAsciiStr ((CONST CHAR16 *)var->name, ret_name);
    // #if DEBUG DBG_INFO ("[%s] ret_name= %s !\n", __func__, ret_name); #endif
    if (var->type == NORMAL_VAR_TYPE) {
        memcpy(&ret_guid, &((DEFAULT_VARIABLE_HEADER *)var->header)->VendorGuid, sizeof(efi_guid_t));
        *attributes = ((DEFAULT_VARIABLE_HEADER *)var->header)->Attributes;
        state = ((DEFAULT_VARIABLE_HEADER *)var->header)->State;
    } else if (var->type == AUTH_VAR_TYPE) {
        memcpy(&ret_guid, &((AUTHENTICATED_VARIABLE_HEADER *)var->header)->VendorGuid, sizeof(efi_guid_t));
        *attributes = ((AUTHENTICATED_VARIABLE_HEADER *)var->header)->Attributes;
        state = ((DEFAULT_VARIABLE_HEADER *)var->header)->State;
    }
    if (state != 0x3F) {
      // #if DEBUG DBG_INFO ("[%s] state= 0x%x !\n", __func__, state); #endif
      continue;
    }
    if (memcmp(&ret_guid, &guid, sizeof(efi_guid_t)) != 0) {
      continue;
    }
    if (strcmp(name, ret_name) != 0) {
      continue;
    }
    
    if (!(newbuf = calloc(var->dataSize, sizeof (uint8_t)))) {
      #if DEBUG 
      DBG_INFO("could not allocate memory"); 
      #endif
      *data = newbuf = NULL;
      *data_size = 0;
      return -1;
    }
    memcpy(newbuf, var->data, var->dataSize);
    *data = newbuf;
    *data_size = var->dataSize;
    #if DEBUG 
    DBG_INFO ("[%s] find var name= %s !\n", __func__, name); 
    #endif
    ret = 1;
    return ret;
  }
  #if DEBUG 
  DBG_INFO("[%s] no match !\n", __func__); 
  #endif
  *data = newbuf = NULL;
  *data_size = 0;
  return -1;
}

int uefivar_get_next_variable_name(efi_guid_t **guid, char **name) {
  size_t i = 0;
  //int ret = -1;
  static size_t npos = 0;
  list_t *pos;
  static char ret_name[NAME_MAX+1];
	static efi_guid_t ret_guid;
  klvar_entry_t *var;
  if (npos >= list_size(&var_list))
  {
    npos = 0;
    #if DEBUG 
    DBG_INFO ("[%s] npos= %zd !\n", __func__, npos); 
    #endif
    goto end;
  }
  list_for_each(pos, &var_list) {
    if (npos == i)
    {
      var = list_entry(pos, klvar_entry_t, list);
      UnicodeStrToAsciiStr ((CONST CHAR16 *)var->name, ret_name);
      if (var->type == NORMAL_VAR_TYPE)
      {
         memcpy(&ret_guid, &((DEFAULT_VARIABLE_HEADER *)var->header)->VendorGuid, sizeof(efi_guid_t));
      } else if (var->type == AUTH_VAR_TYPE){
         memcpy(&ret_guid, &((AUTHENTICATED_VARIABLE_HEADER *)var->header)->VendorGuid, sizeof(efi_guid_t));
      }
      #if DEBUG 
      DBG_INFO ("[%s] npos= %zd !\n", __func__, npos); 
      #endif
      npos++;

      #if DEBUG 
      DBG_INFO ("[%s] ret_name= %s !\n", __func__, ret_name); 
      #endif
      *guid = &ret_guid;
		  *name = ret_name;
      break;
    }
    i++;
  }
end:
  return (int)npos;
}

void clear_list(void) {
  INIT_LIST_HEAD(&var_list);
  #if DEBUG 
  DBG_INFO ("[%s] list size =%zd !\n", __func__, list_size(&var_list)); 
  #endif
}
