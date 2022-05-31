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
#include "uefivar.h"
#include "list.h"
#include "guid.h"

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
 
#define EISA_ID_TO_NUM(_Id)       ((_Id) >> 16)
///
/// Load Option Attributes
///
#define LOAD_OPTION_ACTIVE              0x00000001
#define LOAD_OPTION_FORCE_RECONNECT     0x00000002

#define LOAD_OPTION_HIDDEN              0x00000008
#define LOAD_OPTION_CATEGORY            0x00001F00

#define LOAD_OPTION_CATEGORY_BOOT       0x00000000
#define LOAD_OPTION_CATEGORY_APP        0x00000100

#define EFI_BOOT_OPTION_SUPPORT_KEY     0x00000001
#define EFI_BOOT_OPTION_SUPPORT_APP     0x00000002

UINT8
DevicePathType (
   CONST VOID  *Node
  )
{
  ASSERT (Node != NULL);
  return ((EFI_DEVICE_PATH_PROTOCOL *)(Node))->Type;
}

UINT8
DevicePathSubType (
   CONST VOID  *Node
  )
{
  ASSERT (Node != NULL);
  return ((EFI_DEVICE_PATH_PROTOCOL *)(Node))->SubType;
}

typedef struct{
  CHAR8*  DString;
  UINT16   DeviceType;
}BootDevice;

BootDevice  BootDevices[] = {
  {
    "HDD",
    BDS_EFI_MESSAGE_SATA_BOOT,
  },
  {
    "NVME",
    BDS_EFI_MESSAGE_NVME_BOOT,
  },
  {
  "SATA-CDROM",
    BDS_EFI_MEDIA_CDROM_BOOT,
  },
  {
    "NETWORK",
    BDS_EFI_MESSAGE_MAC_BOOT,
  },
  {
    "USB-CDROM",
    BDS_EFI_MESSAGE_USB_CDROM_BOOT,
  },
  {
   "USB",
    BDS_EFI_MESSAGE_USB_DEVICE_BOOT,
  },
};

  

UINT16
ReadUnaligned16 (
  CONST UINT16              *Buffer
  )
{
  ASSERT (Buffer != NULL);

  return *Buffer;
}

UINTN
DevicePathNodeLength (
   CONST VOID  *Node
  )
{
  ASSERT (Node != NULL);
  return ReadUnaligned16 ((UINT16 *)&((EFI_DEVICE_PATH_PROTOCOL *)(Node))->Length[0]);
}

BOOLEAN
IsDevicePathEndType (
   CONST VOID  *Node
  )
{
  ASSERT (Node != NULL);
  return (BOOLEAN) (DevicePathType (Node) == END_DEVICE_PATH_TYPE);
}


EFI_DEVICE_PATH_PROTOCOL *
NextDevicePathNode (
   CONST VOID  *Node
  )
{
  ASSERT (Node != NULL);
  return (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)(Node) + DevicePathNodeLength(Node));
}


UINT16
GetBootDeviceTypeFromDevicePath (
    EFI_DEVICE_PATH_PROTOCOL     *DevicePath
)
{
  ACPI_HID_DEVICE_PATH          *Acpi;
  EFI_DEVICE_PATH_PROTOCOL      *TempDevicePath;
  EFI_DEVICE_PATH_PROTOCOL      *LastDeviceNode;
  UINT16                        BootType;
  UINTN                         NodeLen = 0;
  UINTN                         PathLen = 0;
  // EFI_STATUS                    Status;
  // EFI_HANDLE                    Handle;
  // EFI_BLOCK_IO_PROTOCOL         *BlkIo;
  UINT8   temp[2]={0};
  
  if (NULL == DevicePath) {
    return 1;
  }

  TempDevicePath = DevicePath;

  while (!IsDevicePathEndType (TempDevicePath)) {
    NodeLen = DevicePathNodeLength(TempDevicePath);
    PathLen += NodeLen;
    //*DevicePathLen = PathLen;

    switch (DevicePathType (TempDevicePath)) {
    case BBS_DEVICE_PATH:
      return BDS_LEGACY_BBS_BOOT;
    case MEDIA_DEVICE_PATH:

      if (DevicePathSubType (TempDevicePath) == MEDIA_HARDDRIVE_DP) {
        return BDS_EFI_MEDIA_HD_BOOT;

      } else if (DevicePathSubType (TempDevicePath) == MEDIA_CDROM_DP) {
        return BDS_EFI_MEDIA_CDROM_BOOT;
//add-klk-lyang-P000A-start//
      } else if (DevicePathSubType (TempDevicePath) == MEDIA_PIWG_FW_FILE_DP) {
        return BDS_EFI_MEDIA_FW_FILE_BOOT;
      }      
//add-klk-lyang-P000A-end//
      break;
    case ACPI_DEVICE_PATH:
      Acpi = (ACPI_HID_DEVICE_PATH *) TempDevicePath;

      if (EISA_ID_TO_NUM (Acpi->HID) == 0x0604) {
        return BDS_EFI_ACPI_FLOPPY_BOOT;
      }

      break;
    case MESSAGING_DEVICE_PATH:
      //
      // Get the last device path node
      //
      LastDeviceNode = NextDevicePathNode (TempDevicePath);

      if (DevicePathSubType(LastDeviceNode) == MSG_DEVICE_LOGICAL_UNIT_DP) {
        //
        // if the next node type is Device Logical Unit, which specify the Logical Unit Number (LUN),
        // skip it
        //
        LastDeviceNode = NextDevicePathNode (LastDeviceNode);
      }

      //
      // if the device path not only point to driver device, it is not a messaging device path,
      //
      if (!IsDevicePathEndType (LastDeviceNode)) {
        break;
      }

      switch (DevicePathSubType (TempDevicePath)) {
      case MSG_ATAPI_DP:
        BootType = BDS_EFI_MESSAGE_ATAPI_BOOT;
        break;
      case MSG_USB_DP:
        //DevicePathSubType (TempDevicePath);
        /*if(IsUsbCdromBootOption(DevicePath)){
          BootType = BDS_EFI_MESSAGE_USB_CDROM_BOOT;//NONSTANDARD
        }else{
          BootType = BDS_EFI_MESSAGE_USB_DEVICE_BOOT;
        }*/
 		temp[0]=*((UINT8*)TempDevicePath);
		temp[1]=*(((UINT8*)TempDevicePath+1));
		BootType =(temp[0]<<8)|temp[1];
        //BootType =*((UINT16*)TempDevicePath);
//printf("BootType=%04x\n",BootType);
        break;
      case MSG_SCSI_DP:
        BootType = BDS_EFI_MESSAGE_SCSI_BOOT;
        break;
      case MSG_SATA_DP:
//add-klk-lyang-P000A-start//  hd cd-rom 
        /*Status = gBS->LocateDevicePath (&gEfiBlockIoProtocolGuid, &DevicePath, &Handle);
        if(!EFI_ERROR(Status)){
           Status = gBS->HandleProtocol (
                 Handle,
                 &gEfiBlockIoProtocolGuid,
                 (VOID **) &BlkIo
                 );
          if (BlkIo->Media->RemovableMedia){
             BootType = BDS_EFI_MEDIA_CDROM_BOOT;
          }else{
             BootType = BDS_EFI_MESSAGE_SATA_BOOT;
          } 
        }else{
           BootType = BDS_EFI_MESSAGE_SATA_BOOT;
        }*/
 		temp[0]=*((UINT8*)TempDevicePath);
		temp[1]=*(((UINT8*)TempDevicePath+1));
		BootType =(temp[0]<<8)|temp[1];
//add-klk-lyang-P000A-end//
        break;
      case MSG_NVME_NAMESPACE_DP:  
        BootType = BDS_EFI_MESSAGE_NVME_BOOT;
        break;
      case MSG_MAC_ADDR_DP:
      case MSG_VLAN_DP:
      case MSG_IPv4_DP:
      case MSG_IPv6_DP:
        BootType = BDS_EFI_MESSAGE_MAC_BOOT;
        break;
      default:
        BootType = BDS_EFI_MESSAGE_MISC_BOOT;
        break;
      }

      return BootType;
    default:
      break;
    }

    TempDevicePath = NextDevicePathNode (TempDevicePath);
  }

  return BDS_EFI_UNSUPPORT;
}


/*
UINT16
GetBootDeviceTypeFromDevicePath (
    EFI_DEVICE_PATH_PROTOCOL     *DevicePath
)
{
  ACPI_HID_DEVICE_PATH          *Acpi;
  EFI_DEVICE_PATH_PROTOCOL      *TempDevicePath;
  EFI_DEVICE_PATH_PROTOCOL      *LastDeviceNode;
  UINT16                        BootType;
  UINTN                         NodeLen = 0;
  UINTN                         PathLen = 0;
  EFI_STATUS                    Status;
  EFI_HANDLE                    Handle;
  EFI_BLOCK_IO_PROTOCOL         *BlkIo;
  
  if (NULL == DevicePath) {
    return 1;
  }

  TempDevicePath = DevicePath;

  while (!IsDevicePathEndType (TempDevicePath)) {
    NodeLen = DevicePathNodeLength(TempDevicePath);
    PathLen += NodeLen;
    //DevicePathLen = PathLen;

    switch (DevicePathType (TempDevicePath)) {
    case BBS_DEVICE_PATH:
      return BDS_LEGACY_BBS_BOOT;
    case MEDIA_DEVICE_PATH:

      if (DevicePathSubType (TempDevicePath) == MEDIA_HARDDRIVE_DP) {
        return BDS_EFI_MEDIA_HD_BOOT;

      } else if (DevicePathSubType (TempDevicePath) == MEDIA_CDROM_DP) {
        return BDS_EFI_MEDIA_CDROM_BOOT;
//add-klk-lyang-P000A-start//
      } else if (DevicePathSubType (TempDevicePath) == MEDIA_PIWG_FW_FILE_DP) {
        return BDS_EFI_MEDIA_FW_FILE_BOOT;
      }      
//add-klk-lyang-P000A-end//
      break;
    case ACPI_DEVICE_PATH:
      Acpi = (ACPI_HID_DEVICE_PATH *) TempDevicePath;

      if (EISA_ID_TO_NUM (Acpi->HID) == 0x0604) {
        return BDS_EFI_ACPI_FLOPPY_BOOT;
      }

      break;
    case MESSAGING_DEVICE_PATH:
      //
      // Get the last device path node
      //
      LastDeviceNode = NextDevicePathNode (TempDevicePath);

      if (DevicePathSubType(LastDeviceNode) == MSG_DEVICE_LOGICAL_UNIT_DP) {
        //
        // if the next node type is Device Logical Unit, which specify the Logical Unit Number (LUN),
        // skip it
        //
        LastDeviceNode = NextDevicePathNode (LastDeviceNode);
      }

      //
      // if the device path not only point to driver device, it is not a messaging device path,
      //
      if (!IsDevicePathEndType (LastDeviceNode)) {
        break;
      }

      switch (DevicePathSubType (TempDevicePath)) {
      case MSG_ATAPI_DP:
        BootType = BDS_EFI_MESSAGE_ATAPI_BOOT;
        break;
      case MSG_USB_DP:
        //DevicePathSubType (TempDevicePath);
        if(IsUsbCdromBootOption(DevicePath)){
          BootType = BDS_EFI_MESSAGE_USB_CDROM_BOOT;//NONSTANDARD
        }else{
          BootType = BDS_EFI_MESSAGE_USB_DEVICE_BOOT;
        }
        break;
      case MSG_SCSI_DP:
        BootType = BDS_EFI_MESSAGE_SCSI_BOOT;
        break;
      case MSG_SATA_DP:
//add-klk-lyang-P000A-start//  hd cd-rom 
        Status = gBS->LocateDevicePath (&gEfiBlockIoProtocolGuid, &DevicePath, &Handle);
        if(!EFI_ERROR(Status)){
           Status = gBS->HandleProtocol (
                 Handle,
                 &gEfiBlockIoProtocolGuid,
                 (VOID **) &BlkIo
                 );
          if (BlkIo->Media->RemovableMedia){
             BootType = BDS_EFI_MEDIA_CDROM_BOOT;
          }else{
             BootType = BDS_EFI_MESSAGE_SATA_BOOT;
          } 
        }else{
           BootType = BDS_EFI_MESSAGE_SATA_BOOT;
        }         
//add-klk-lyang-P000A-end//
        break;
      case MSG_NVME_NAMESPACE_DP:  
        BootType = BDS_EFI_MESSAGE_NVME_BOOT;
        break;
      case MSG_MAC_ADDR_DP:
      case MSG_VLAN_DP:
      case MSG_IPv4_DP:
      case MSG_IPv6_DP:
        BootType = BDS_EFI_MESSAGE_MAC_BOOT;
        break;
      default:
        BootType = BDS_EFI_MESSAGE_MISC_BOOT;
        break;
      }

      return BootType;
    default:
      break;
    }

    TempDevicePath = NextDevicePathNode (TempDevicePath);
  }

  return BDS_EFI_UNSUPPORT;
}

*/

/**
  Returns the length of a Null-terminated Unicode string.

  This function returns the number of Unicode characters in the Null-terminated
  Unicode string specified by String.

  If String is NULL, then ASSERT().
  If String is not aligned on a 16-bit boundary, then ASSERT().
  If PcdMaximumUnicodeStringLength is not zero, and String contains more than
  PcdMaximumUnicodeStringLength Unicode characters, not including the
  Null-terminator, then ASSERT().

  @param  String  A pointer to a Null-terminated Unicode string.

  @return The length of String.

**/
UINTN
StrLen (
  CONST CHAR16              *String
  )
{
  UINTN   Length;

  ASSERT (String != NULL);
  ASSERT (((UINTN) String & BIT0) == 0);

  for (Length = 0; *String != L'\0'; String++, Length++) {
    //
    // If PcdMaximumUnicodeStringLength is not zero,
    // length should not more than PcdMaximumUnicodeStringLength
    //
  }
  return Length;
}
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

/*
Routine Description:
  This function The structure of the parse Variable data length and Variable Name pointer and Name size

Arguments:
  nv_fv_buff   : fvHeader Pointer
  NamePtr      : expect Variable Name 
  NameSize     : expect Variable Name size
  VariableSize : expect Variable size 
Returns:
  procce result .
*/

static int GetVariabeInf(IN  uint8_t      *   Variable_buff ,
                             OUT uint     *   VariableSize , 
                             OUT uint8_t  **  NamePtr ,
                             OUT UINT32   *   NameSize,
                             OUT uint8_t  **  DataPtr)
{
  int Ret = 0 ;
  AUTHENTICATED_VARIABLE_HEADER * aut_var_hdr;
  DEFAULT_VARIABLE_HEADER       * def_var_hdr;
//  int                             offset;
  
  if ((!Variable_buff)) {
    return 1;
  }

  aut_var_hdr = (AUTHENTICATED_VARIABLE_HEADER *)Variable_buff;
  def_var_hdr = (DEFAULT_VARIABLE_HEADER *)Variable_buff;
  
  switch(var_type)
  {
    case DEFAULT_VARIABLE_TYPE:
	  if(VariableSize)
        * VariableSize = sizeof(DEFAULT_VARIABLE_HEADER) + def_var_hdr->NameSize + def_var_hdr->DataSize;
	  if(NamePtr)
	    * NamePtr      = (uint8_t*)((UINTN)def_var_hdr + sizeof(DEFAULT_VARIABLE_HEADER));
	  if(NameSize)
	    * NameSize     = def_var_hdr->NameSize;
	  if(DataPtr)
	  	* DataPtr      = (uint8_t*)((UINTN)def_var_hdr + sizeof(DEFAULT_VARIABLE_HEADER) + def_var_hdr->NameSize); 
	break;
	case AUTHENTICATED_VARIABLE_TYPE:
	  if(VariableSize)
        * VariableSize = sizeof(AUTHENTICATED_VARIABLE_HEADER) + aut_var_hdr->NameSize + aut_var_hdr->DataSize;
	  if(NamePtr)
	    * NamePtr      = (uint8_t*)((UINTN)aut_var_hdr + sizeof(AUTHENTICATED_VARIABLE_HEADER));
	  if(NameSize)
	    * NameSize     = aut_var_hdr->NameSize;
	  if(DataPtr)
	  	* DataPtr      = (uint8_t*)((UINTN)aut_var_hdr + sizeof(AUTHENTICATED_VARIABLE_HEADER) + aut_var_hdr->NameSize); 
	break;
	default :
	  if(VariableSize)
        * VariableSize = sizeof(DEFAULT_VARIABLE_HEADER) + def_var_hdr->NameSize + def_var_hdr->DataSize;
	  if(NamePtr)
	    * NamePtr      = (uint8_t*)((UINTN)def_var_hdr + sizeof(DEFAULT_VARIABLE_HEADER));
	  if(NameSize)
	    * NameSize     = def_var_hdr->NameSize;
	  if(DataPtr)
	  	* DataPtr      = (uint8_t*)((UINTN)def_var_hdr + sizeof(DEFAULT_VARIABLE_HEADER) + def_var_hdr->NameSize);  
	break;
  }
  return Ret;
}



/*
Routine Description:
  This function The structure of the parse Variable data length and Variable Name pointer and Name size

Arguments:
  nv_fv_buff   : fvHeader Pointer
  NamePtr      : expect Variable Name 
  NameSize     : expect Variable Name size
  VariableSize : expect Variable size 
Returns:
  procce result .
*/

static int GetVariabeInf1(IN  uint8_t      *   Variable_buff ,
                             OUT uint     *   VariableSize , 
                             OUT uint8_t  **  NamePtr ,
                             OUT UINT32   *   NameSize,
                             OUT uint8_t  **  DataPtr,
                             OUT UINT32   *   DataSize)
{
  int Ret = 0 ;
  AUTHENTICATED_VARIABLE_HEADER * aut_var_hdr;
  DEFAULT_VARIABLE_HEADER       * def_var_hdr;
//  int                             offset;
  
  if ((!Variable_buff)) {
    return 1;
  }

  aut_var_hdr = (AUTHENTICATED_VARIABLE_HEADER *)Variable_buff;
  def_var_hdr = (DEFAULT_VARIABLE_HEADER *)Variable_buff;
  
  switch(var_type)
  {
    case DEFAULT_VARIABLE_TYPE:
	  if(VariableSize)
        * VariableSize = sizeof(DEFAULT_VARIABLE_HEADER) + def_var_hdr->NameSize + def_var_hdr->DataSize;
	  if(NamePtr)
	    * NamePtr      = (uint8_t*)((UINTN)def_var_hdr + sizeof(DEFAULT_VARIABLE_HEADER));
	  if(NameSize)
	    * NameSize     = def_var_hdr->NameSize;
	  if(DataPtr)
	  	* DataPtr      = (uint8_t*)((UINTN)def_var_hdr + sizeof(DEFAULT_VARIABLE_HEADER) + def_var_hdr->NameSize); 
    if(DataSize)
	  	* DataSize     = def_var_hdr->DataSize;
	break;
	case AUTHENTICATED_VARIABLE_TYPE:
	  if(VariableSize)
        * VariableSize = sizeof(AUTHENTICATED_VARIABLE_HEADER) + aut_var_hdr->NameSize + aut_var_hdr->DataSize;
	  if(NamePtr)
	    * NamePtr      = (uint8_t*)((UINTN)aut_var_hdr + sizeof(AUTHENTICATED_VARIABLE_HEADER));
	  if(NameSize)
	    * NameSize     = aut_var_hdr->NameSize;
	  if(DataPtr)
	  	* DataPtr      = (uint8_t*)((UINTN)aut_var_hdr + sizeof(AUTHENTICATED_VARIABLE_HEADER) + aut_var_hdr->NameSize);
    if(DataSize)
	  	* DataSize     = aut_var_hdr->DataSize;
	break;
	default :
	  if(VariableSize)
        * VariableSize = sizeof(DEFAULT_VARIABLE_HEADER) + def_var_hdr->NameSize + def_var_hdr->DataSize;
	  if(NamePtr)
	    * NamePtr      = (uint8_t*)((UINTN)def_var_hdr + sizeof(DEFAULT_VARIABLE_HEADER));
	  if(NameSize)
	    * NameSize     = def_var_hdr->NameSize;
	  if(DataPtr)
	  	* DataPtr      = (uint8_t*)((UINTN)def_var_hdr + sizeof(DEFAULT_VARIABLE_HEADER) + def_var_hdr->NameSize);
    if(DataSize)
	  	* DataSize     = def_var_hdr->DataSize;
	break;
  }
  return Ret;
}


// compare two wchar strings 

/*++

Routine Description:
  This function compares the Unicode string String to the Unicode
  string String2 for len characters.  If the first len characters
  of String is identical to the first len characters of String2,
  then 0 is returned.  If substring of String sorts lexicographically
  after String2, the function returns a number greater than 0. If
  substring of String sorts lexicographically before String2, the
  function returns a number less than 0.

Arguments:
  String  - Compare to String2
  String2 - Compare to String
  Length  - Number of Unicode characters to compare

Returns:
  0     - The substring of String and String2 is identical.
  > 0   - The substring of String sorts lexicographically after String2
  < 0   - The substring of String sorts lexicographically before String2

--*/
static int wstrncmp (
  wchar_t   *String,
  wchar_t   *String2,
  uint       Length
)
{
  while (*String && Length != 0) {
    if (*String != *String2) {
      break;
    }

    String  += 1;
    String2 += 1;
    Length  -= 1;
  }

  return Length > 0 ? *String - *String2 : 0;
}

CHAR8 *
EFIAPI
UnicodeStrToAsciiStr (
  IN      CONST CHAR16              *Source,
  OUT     CHAR8                     *Destination
  )
{
  CHAR8                               *ReturnValue;

  // dump_buffer ((uint8_t *)Source, 0x10);

  ReturnValue = Destination;
  while (*Source != '\0') {
    *(Destination++) = (CHAR8) *(Source++);
  }

  *Destination = '\0';

  return ReturnValue;
}

int locate_first_var (uint8_t *nv_fv_buff, uint32_t buff_len, int *offset)
{
  EFI_FV_BLOCK_MAP_ENTRY  *blk_map_entry;
  uint8_t *ptr;

  if (!nv_fv_buff || !offset) {
    return 1;
  }
  ptr = NULL;

  blk_map_entry = ((EFI_FIRMWARE_VOLUME_HEADER *)nv_fv_buff)->FvBlockMap;
  do {
    //DBG_INFO ("NumBlocks: %x, BlockLength: %x\n", blk_map_entry->NumBlocks, blk_map_entry->BlockLength);

    if (blk_map_entry->NumBlocks == 0x00 &&
        blk_map_entry->BlockLength == 0x00)
      break;
    blk_map_entry++;
  } while ((uint8_t *)blk_map_entry - nv_fv_buff < buff_len);

  if ((uint8_t *)blk_map_entry - nv_fv_buff >= buff_len) {
    DBG_ERR ("Failed to find UEFI Variable in NV_FV!\n");
    return 1;
  }

  ptr = (uint8_t *)blk_map_entry + sizeof(EFI_FV_BLOCK_MAP_ENTRY) + sizeof(VARIABLE_STORE_HEADER);
  *offset = ptr - nv_fv_buff;

  return 0;
}

static int chk_nvfv (uint8_t *nv_fv_buff, uint32_t buff_len)
{
    if (!nv_fv_buff || !buff_len) {
    return 1;
  }
  return 0;
}
#if 0
// parse the data of raw variable data, i.e. data from the VARIABLE area of the BIOS.
// pick out data of variables named "UserInfoLog" and "SecurityInfoLog" and store 
// them into a temp file.
int get_klsetup_var (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info)
{
  int                             ret;
  uint                            var_size;
  int                             offset;
  uint8_t                       * ptr;
  wchar_t                       * var_name;
  UINT32                          namesize;
    
  //SETUP_DATA      *var_setup = NULL;

  if (!nv_fv_buff || !var_info) {
    return 1;
  }

  if (chk_nvfv (nv_fv_buff, buff_len) != 0) {
    DBG_ERR ("Inalid NV_FV!\n");
    return 1;
  }

  ret       = -1;
  ptr       = NULL;
  var_name  = NULL;
  offset    = 0;

  //DBG_PROG ("[%s] Parsing the uefi non-volatile variable...\n", BOOTMGR_TOOL_NAME);
  DBG_INFO ("Parsing the uefi non-volatile variable...\n");
  ptr = nv_fv_buff;
  
  locate_first_var (nv_fv_buff, buff_len, &offset);
  if (offset > 0) {
    ptr += offset;
  }

  while ((uint32_t)(ptr - nv_fv_buff) < buff_len) {
    if (ptr[0] == 0xAA && ptr[1] == 0x55) {
		GetVariabeInf(ptr , &var_size , (uint8_t**)&var_name , &namesize , NULL);     
#if defined(__aarch64__) || defined(__mips__) || defined(__loongarch__)
      var_size = _INTSIZEOF(var_size);
#endif
      if (ptr[2] == VAR_ADDED) { 
	  	GetVariabeInf(ptr , NULL , (uint8_t**)&var_name , &namesize , NULL);
        //wprintf (L"Var Name: %s\n", var_name);
        if (wstrncmp (var_name, klsetup_var_name, namesize) == 0) {
          DBG_INFO ("KlSetup variable found.\n");
          //var_setup = (SETUP_DATA *)((uint8_t *)var_hdr + sizeof(VARIABLE_HEADER(var_type)) + var_hdr->NameSize);
          ret = 0;
          var_info->IsValid = 1;
          var_info->Offset  = (uint32_t)(ptr - nv_fv_buff);
          DBG_INFO ("Var Offset: %#x\n", var_info->Offset);
          //break;
        }
      }
      ptr = ((uint8_t *)ptr + var_size);

    } else {
      //ptr += 1;
      break;
    }
  }

  return ret;
}
#endif

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
  dump_buffer(&gVolumeHeader, sizeof(gVolumeHeader));
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
  dump_buffer(&gStoreHeader, sizeof(gStoreHeader));
  
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
  DBG_INFO ("[%s] varType=%d !\n", __func__, varType);
  if (varType == NORMAL_VAR_TYPE) {
    hdrsize = sizeof(DEFAULT_VARIABLE_HEADER);
  } else if ( varType == AUTH_VAR_TYPE ) {
    hdrsize = sizeof(AUTHENTICATED_VARIABLE_HEADER);
  } else {
    DBG_ERR ("[%s] varType=%d ERR!\n", __func__, varType);
    return 4;
  }
  DBG_INFO ("[%s]AUTHENTICATED offset=0x%x !\n", __func__, offset);
  dump_buffer(nv_fv_buff+offset, 0x100);
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
      DBG_INFO ("[%s]DEFAULT_VARIABLE offset=0x%x !\n", __func__, offset); 
      memcpy( entry->header, nv_fv_buff+offset, hdrsize);
      offset += hdrsize;
      dump_buffer(entry->header, hdrsize);
      if (((DEFAULT_VARIABLE_HEADER *)entry->header)->StartId == INVALID_START_ID) {
        break;
      }
      if (((DEFAULT_VARIABLE_HEADER *)entry->header)->StartId != VALID_START_ID) {
        DBG_INFO ("[%s]START_ID ERR, StartId=0x%x !\n", __func__, ((DEFAULT_VARIABLE_HEADER *)entry->header)->StartId);
        return 5;
      }
      entry->nameSize = ((DEFAULT_VARIABLE_HEADER *)entry->header)->NameSize;
      entry->dataSize = ((DEFAULT_VARIABLE_HEADER *)entry->header)->DataSize;
    } else if ( entry->type == AUTH_VAR_TYPE ) {
      entry->offset = offset;
      entry->headerSize = sizeof(AUTHENTICATED_VARIABLE_HEADER);
      entry->header = (char *)malloc (sizeof(AUTHENTICATED_VARIABLE_HEADER));
      DBG_INFO ("[%s]AUTHENTICATED_VARIABLE offset=0x%x !\n", __func__, offset); 
      memcpy( entry->header, nv_fv_buff+offset, hdrsize);
      offset += hdrsize;
      dump_buffer(entry->header, hdrsize);
      if (((AUTHENTICATED_VARIABLE_HEADER *)entry->header)->StartId == INVALID_START_ID) {
        break;
      }
      if (((AUTHENTICATED_VARIABLE_HEADER *)entry->header)->StartId != VALID_START_ID) {
        DBG_INFO ("[%s]START_ID ERR, StartId=0x%x !\n", __func__, ((AUTHENTICATED_VARIABLE_HEADER *)entry->header)->StartId);
        return 5;
      }
      entry->nameSize = ((AUTHENTICATED_VARIABLE_HEADER *)entry->header)->NameSize;
      entry->dataSize = ((AUTHENTICATED_VARIABLE_HEADER *)entry->header)->DataSize;
    }
    entry->name = (char *)malloc( entry->nameSize);
    memcpy( entry->name, nv_fv_buff+offset, entry->nameSize);
    offset += entry->nameSize;
    dump_buffer(entry->name, entry->nameSize);

    entry->data = (char *)malloc( entry->dataSize);
    memcpy( entry->data, nv_fv_buff+offset, entry->dataSize);
    offset += entry->dataSize;
    dump_buffer(entry->data, entry->dataSize);
    
    offset = _INTSIZEOF(offset);
    entry->totalSize = offset - entry->offset;

    //add to list
    list_add_tail(&(entry->list), head);
  }
  DBG_INFO ("[%s] end !\n", __func__);

  list_t *pos;
  klvar_entry_t *var;
  list_for_each(pos, head) {
    var = list_entry(pos, klvar_entry_t, list);
    DBG_INFO ("[%s] var->offset= 0x%x !\n", __func__, var->offset);
  }
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
    DBG_INFO ("[%s] var_type=%d !\n", __func__, var_type);
    getVariableInfo(nv_fv_buff, buff_len, &var_list);
    DBG_INFO ("[%s] list size =%zd !\n", __func__, list_size(&var_list));

  } else if (nvtype == STORE_HEADER) {
    DBG_ERR ("[%s]Not support nv type(STORE_HEADER)!\n",__func__);
    return 2;
  } else {
    DBG_ERR ("[%s]Not support nv type !\n",__func__);
    return 3;
  }
  return 0;
}

int get_variable_var (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info,wchar_t *seek_var_name)
{
  int                             ret;
  uint                            var_size;
  int                             offset;
  uint8_t                       * ptr;
  wchar_t                       * var_name;
  UINT32                          namesize;
    
  //SETUP_DATA      *var_setup = NULL;

  if (!nv_fv_buff || !var_info) {
    return 1;
  }

  if (chk_nvfv (nv_fv_buff, buff_len) != 0) {
    DBG_ERR ("Inalid NV_FV!\n");
    return 1;
  }

  ret       = -1;
  ptr       = NULL;
  var_name  = NULL;
  offset    = 0;

  //DBG_PROG ("[%s] Parsing the uefi non-volatile variable...\n", BOOTMGR_TOOL_NAME);
  DBG_INFO ("Parsing the uefi non-volatile variable...\n");
  ptr = nv_fv_buff;


  locate_first_var (nv_fv_buff, buff_len, &offset);
  if (offset > 0) {
    ptr += offset;
  }

  while ((uint32_t)(ptr - nv_fv_buff) < buff_len) {
    if (ptr[0] == 0xAA && ptr[1] == 0x55) {
		GetVariabeInf(ptr , &var_size , (uint8_t**)&var_name , &namesize , NULL);     
#if defined(__aarch64__) || defined(__mips__) || defined(__loongarch__)
      var_size = _INTSIZEOF(var_size);
#endif
      if (ptr[2] == VAR_ADDED) { 
	  	GetVariabeInf(ptr , NULL , (uint8_t**)&var_name , &namesize , NULL);
        //wprintf (L"Var Name: %s\n", var_name);
        if (wstrncmp (var_name, seek_var_name, namesize) == 0) {
          DBG_INFO ("KlSetup variable found.\n");
          //var_setup = (SETUP_DATA *)((uint8_t *)var_hdr + sizeof(VARIABLE_HEADER(var_type)) + var_hdr->NameSize);
          ret = 0;
          var_info->IsValid = 1;
          var_info->Offset  = (uint32_t)(ptr - nv_fv_buff);
          DBG_INFO ("Var Offset: %#x\n", var_info->Offset);
          //break;
        }
      }
      ptr = ((uint8_t *)ptr + var_size);

    } else {
      //ptr += 1;
      break;
    }
  }

  return ret;
}

#if 0 
int get_network_stack_var (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info)
{
  int             ret;
  uint            var_size;
  int             offset;
  uint8_t         *ptr;
  wchar_t         *var_name;
  UINT32                          namesize;
  //VARIABLE_HEADER *var_hdr;
  //SETUP_DATA      *var_setup = NULL;

  if (!nv_fv_buff || !var_info) {
    return 1;
  }

  if (chk_nvfv (nv_fv_buff, buff_len) != 0) {
    DBG_ERR ("Inalid NV_FV!\n");
    return 1;
  }

  ret       = -1;
  ptr       = NULL;
  var_name  = NULL;
  //var_hdr   = NULL;
  offset    = 0;

  //DBG_PROG ("[%s] Parsing the uefi non-volatile variable...\n", BOOTMGR_TOOL_NAME);
  DBG_INFO ("Parsing the uefi non-volatile variable...\n");
  ptr = nv_fv_buff;
  locate_first_var (nv_fv_buff, buff_len, &offset);
  if (offset > 0) {
    ptr += offset;
  }

  while ((uint32_t)(ptr - nv_fv_buff) < buff_len) {
    if (ptr[0] == 0xAA && ptr[1] == 0x55) {
      //var_hdr = (VARIABLE_HEADER *)ptr;

      //var_size = sizeof(VARIABLE_HEADER) + var_hdr->NameSize + var_hdr->DataSize; 
	  GetVariabeInf(ptr , &var_size , (uint8_t**)&var_name , &namesize , NULL);      
#if defined(__aarch64__) || defined(__mips__) || defined(__loongarch__)
      var_size = _INTSIZEOF(var_size);
#endif
      
      if (ptr[2] == VAR_ADDED) {        
        GetVariabeInf(ptr , NULL , (uint8_t**)&var_name , &namesize , NULL);
        //wprintf (L"Var Name: %s\n", var_name);
        if (wstrncmp (var_name, network_stack_var_name, namesize) == 0) {
          DBG_INFO ("NetworkStack variable found.\n");
          //var_setup = (SETUP_DATA *)((uint8_t *)var_hdr + sizeof(VARIABLE_HEADER) + var_hdr->NameSize);
          ret = 0;
          var_info->IsValid = 1;
          var_info->Offset  = (uint32_t)(ptr - nv_fv_buff);
          DBG_INFO ("Var Offset: %#x\n", var_info->Offset);
          //break;
        }
      }
      //var_hdr = (VARIABLE_HEADER *)((uint8_t *)var_hdr + var_size);
      ptr = ((uint8_t *)ptr + var_size);

    } else {
      //ptr += 1;
      break;
    }
  }

  return ret;
}

#endif

//
// BUG: This fuction should print value of "BootOrder" variable
//
int display_boot_order (uint8_t *nv_fv_buff, KLSETUP_VAR_INFO *var_info)
{
  SETUP_DATA                    * data;
  // int                             offset;
  // short                         * boot_options;
  // uint8_t                       * ptr;
  // wchar_t                       * var_name;
  // UINT32                          namesize;
  
  if (!nv_fv_buff || !var_info || !var_info->IsValid) {
    return 1;
  }
  
  data          = NULL;
  // boot_options  = NULL;

  
  
  DBG_INFO ("Offset of UefiPriorities: %#x\n", ((SETUP_DATA *)0)->UefiPriorities[0]);

  GetVariabeInf(nv_fv_buff + var_info->Offset , NULL , NULL , NULL , (uint8_t**)&data);

  // boot_options = (short *)data->UefiPriorities;
#if 0
  DBG_PROG ("[%s] Current BootOrder: ", BOOTMGR_TOOL_NAME);
  for (index = 0; index < sizeof(data->UefiPriorities)/sizeof(typeof(data->UefiPriorities[0])); index++) {
    if (index)
      DBG_PROG (", ");
    //DBG_PROG ("%04X", *boot_options++);
    DBG_PROG ("%s", boot_dev[*boot_options++].dev_name);
  }
#else

#endif
  DBG_PROG ("\n");

  return 0;
}



//
// BUG: This fuction should print value of "BootOrder" variable
//
int display1_boot_order (uint8_t *nv_fv_buff, uint32_t buff_len,KLSETUP_VAR_INFO *var_info,BootOrder  *BootOrdervar,UINT16 *data)
{
  //SETUP_DATA                    * data;
  EFI_LOAD_OPTION               *pBootOption = NULL;
  CHAR16                        *DescriptionStr = NULL;
  EFI_DEVICE_PATH_PROTOCOL      *DevicePathNode = NULL;
  UINT16                        DeviceType;
  
  //UINT16                        *data;
  UINT16                        *data1;
  // int                             offset;
  // short                         * boot_options;
  // uint8_t                       * ptr;
  // wchar_t                       * var_name;
  // UINT32                          namesize;
  UINT32                          datasize;
  UINT32                          datasize1;
  UINT16                          index,index1;
  int                             ret = 0;
  // UINT16                             *BootOrder = NULL;
  UINT16                    NUM;

  KLSETUP_VAR_INFO var_info1={0};
  
  if (!nv_fv_buff || !var_info || !var_info->IsValid) {
    return 1;
  }
  
  //data          = NULL;
  // boot_options  = NULL;

  //GetVariabeInf(nv_fv_buff + var_info->Offset , NULL , NULL , NULL , (uint8_t**)&data);
  GetVariabeInf1(nv_fv_buff + var_info->Offset , NULL , NULL , NULL , (uint8_t**)&data,&datasize);
  //printf("datasize=%d\n",datasize);
  //index=datasize/sizeof(UINT16);

  for(index=0;index<datasize/sizeof(UINT16);index++)
  {
	NUM=*(((UINT16 *)data)+index);
    //printf("data=%08x\n",NUM);
    if(NUM==0x00)
      {
        ret = get_variable_var (nv_fv_buff, buff_len, &var_info1,L"Boot0000");
      }
    else if(NUM==0x01)
      {
        ret = get_variable_var (nv_fv_buff, buff_len,& var_info1,L"Boot0001");
      }
    else if(NUM==0x02)
      {
        ret = get_variable_var (nv_fv_buff, buff_len, &var_info1,L"Boot0002");
      }
    else if(NUM==0x03)
      {
        ret = get_variable_var (nv_fv_buff, buff_len,& var_info1,L"Boot0003");
      }
    else if(NUM==0x04)
      {
        ret = get_variable_var (nv_fv_buff, buff_len, &var_info1,L"Boot0004");
      }
    else if(NUM==0x05)
      {
        ret = get_variable_var (nv_fv_buff, buff_len, &var_info1,L"Boot0005");
      }
    else if(NUM==0x06)
      {
        ret = get_variable_var (nv_fv_buff, buff_len, &var_info1,L"Boot0006");
      }
    else if(NUM==0x07)
      {
        ret = get_variable_var (nv_fv_buff, buff_len, &var_info1,L"Boot0007");
      }
    else if(NUM==0x08)
      {
        ret = get_variable_var (nv_fv_buff, buff_len, &var_info1,L"Boot0008");
      }
    else if(NUM==0x09)
      {
        ret = get_variable_var (nv_fv_buff, buff_len, &var_info1,L"Boot0009");
      }
      if (ret)
      {
        ;
      }
      
else
	{
		return 1;
	}
        GetVariabeInf1(nv_fv_buff + var_info1.Offset , NULL , NULL , NULL , (uint8_t**)&data1,&datasize1);
        //printf("value:%08x\n",*(UINT32*)(data));
        pBootOption = (EFI_LOAD_OPTION *)data1;
        //printf("pBootOption->Attributes:%08x\n",pBootOption->Attributes);
        if (!(pBootOption->Attributes & LOAD_OPTION_ACTIVE) )
        //if(*(data+1)!=0x01)
          continue;
        DescriptionStr = (CHAR16*)(pBootOption + 1);
        DevicePathNode = (EFI_DEVICE_PATH_PROTOCOL*)( DescriptionStr + StrLen(DescriptionStr) + 1 );
        DeviceType = GetBootDeviceTypeFromDevicePath(DevicePathNode);
        //printf("!!DeviceType=%04x\n",DeviceType);

        //for(index1 = 0; index1 < ARRAY_SIZE(BootDevices); index1++){
		for(index1 = 0; index1 < sizeof(BootDevices)/sizeof(BootDevices[0]); index1++)
		{
//printf("!!DeviceType=%04x\n",DeviceType);
 //printf("!!BootDevices[%d].DeviceType=%04x\n",index1,BootDevices[index1].DeviceType);
		    if(DeviceType == BootDevices[index1].DeviceType)
		    {
				//printf("!!Right!!\n");
		      //printf("Boot%04x %s %s\n\r",BootOrder[index],BootDevices[index1].DString,DescriptionStr);
		    BootOrdervar[index].bootnum=NUM;
        BootOrdervar[index].type=BootDevices[index1].DString;
			  printf("index %d Boot%04x %s \n\r",index,NUM,BootDevices[index1].DString);
		    }
		    /*for(int j=0;j<datasize/sizeof(UINT16);j++)
		    {
		      printf("data:%04x\n",*(data+j));
		      printf("j:%d\n",j);			
		      printf("datasize/szieof(UINT16):%d\n",datasize/sizeof(UINT16));			
		      printf("i:%d\n",i);
		    }*/
       }
    }
  

  return 0;
}


int get_curr_boot_dev (uint8_t *nv_fv_buff, KLSETUP_VAR_INFO *var_info, short *curr_dev)
{
  SETUP_DATA        *data;
  // int               offset;
    
  if (!nv_fv_buff || !var_info || !var_info->IsValid || !curr_dev) {
    return 1;
  }
  
  data    = NULL;
  GetVariabeInf(nv_fv_buff + var_info->Offset , NULL , NULL , NULL , (uint8_t**)&data);
  
  *curr_dev = data->UefiPriorities[0];

  return 0;
}

int get_curr_pxe_state (uint8_t *nv_fv_buff, KLSETUP_VAR_INFO *var_info, short *curr_state)
{
  //VARIABLE_HEADER   *var_hdr;
  // UINT32                          namesize;
  NETWORK_STACK      *data;
  // int               offset;

  if (!nv_fv_buff || !var_info || !var_info->IsValid) {
    return 1;
  }
  
  data    = NULL;
  //var_hdr = (VARIABLE_HEADER *)(nv_fv_buff + var_info->Offset);  
  //DBG_INFO ("Offset of UefiPriorities: %#x\n", ((SETUP_DATA *)0)->UefiPriorities);
  //offset = sizeof(VARIABLE_HEADER) + var_hdr->NameSize;
  //data = (NETWORK_STACK *)(nv_fv_buff + var_info->Offset + offset);
  
  GetVariabeInf(nv_fv_buff + var_info->Offset , NULL , NULL , NULL , (uint8_t**)&data);
 
  *curr_state= data->Enable;

  return 0;
}
int get_curr_NETWORKSTACK_state (uint8_t *nv_fv_buff, KLSETUP_VAR_INFO *var_info, NETWORK_STACK *curr_state)
{
  //VARIABLE_HEADER   *var_hdr;
  NETWORK_STACK        *data;
  // int               offset;

  if (!nv_fv_buff || !var_info || !var_info->IsValid) {
    return 1;
  }
  
  data    = NULL;
  //var_hdr = (VARIABLE_HEADER *)(nv_fv_buff + var_info->Offset);

  //DBG_INFO ("Offset of UefiPriorities: %#x\n", ((SETUP_DATA *)0)->UefiPriorities);
  //offset = sizeof(VARIABLE_HEADER) + var_hdr->NameSize;
  //data = (NETWORK_STACK *)(nv_fv_buff + var_info->Offset + offset);
 GetVariabeInf(nv_fv_buff + var_info->Offset , NULL , NULL , NULL , (uint8_t**)&data);
  *curr_state= *data;
printf("!!get_curr_NETWORKSTACK_state=%d\n",curr_state->Enable);

  return 0;
}


int get_curr_KlSetup_state (uint8_t *nv_fv_buff, KLSETUP_VAR_INFO *var_info, SETUP_DATA *curr_state)
{
  //VARIABLE_HEADER   *var_hdr;
  SETUP_DATA        *data;
  // int               offset;

  if (!nv_fv_buff || !var_info || !var_info->IsValid) {
    return 1;
  }
  
  data    = NULL;
  //var_hdr = (VARIABLE_HEADER *)(nv_fv_buff + var_info->Offset);

  //DBG_INFO ("Offset of UefiPriorities: %#x\n", ((SETUP_DATA *)0)->UefiPriorities);
  //offset = sizeof(VARIABLE_HEADER) + var_hdr->NameSize;
  //data = (SETUP_DATA *)(nv_fv_buff + var_info->Offset + offset);
 GetVariabeInf(nv_fv_buff + var_info->Offset , NULL , NULL , NULL , (uint8_t**)&data);
 
  *curr_state= *data;
//printf("!!get_curr_KlSetup_NumaEn_state=%d\n",curr_state->NumaEn);

  return 0;
}


int locate_blank_of_nv (uint8_t *nv_fv_buff, uint32_t buff_len, int *offset)
{
  int             ret;
  uint            var_size;
  uint8_t         *ptr;
  // SETUP_DATA      *var_setup;
  
  if (!nv_fv_buff || !offset) {
    return 1;
  }

  ret       = -1;
  ptr       = NULL;
  //var_setup = NULL;

  DBG_INFO ("Parsing the uefi non-volatile variable...\n");
  *offset = 0;
  ptr = nv_fv_buff;
  locate_first_var (nv_fv_buff, buff_len, offset);
  if (*offset > 0) {
    ptr += *offset;
  }
  while ((uint32_t)(ptr - nv_fv_buff) < buff_len) {
    if (ptr[0] == 0xAA && ptr[1] == 0x55) {
	  GetVariabeInf(ptr , &var_size , NULL , NULL , NULL);
    
#if defined(__aarch64__) || defined(__mips__) || defined(__loongarch__)
      var_size = _INTSIZEOF(var_size);
#endif
      ptr = ((uint8_t *)ptr + var_size);
      //DBG_INFO ("Offset of next var: %#x\n", ptr - nv_fv_buff);

    } else {
      //ptr += 1;
      break;
    }
  }
  *offset = ptr - nv_fv_buff;
  //DBG_INFO ("Start of the blank in NV_FV: %#x\n", *offset);

  return ret;
}

int set_var_state (int var_off_in_nv, uint8_t state)
{
  uint8_t        data[4] = {0xAA, 0X55, 0x00, 0x00};
  uint32_t       offset;
  int            ret = 0;

  //
  // var_hdr must be 4 bytes aligned.
  // Here, the case of address misalignment will not be considered.

  data[2] = state;
  dump_buffer(data, sizeof(data));
  offset = var_off_in_nv;
  DBG_INFO ("[set_var_state] offset: %x\n", offset);
  // ret = flash_write (data, offset, sizeof(data));

  return ret;
}

int write_variable (int old_offset, int new_offset, uint8_t *data, int data_size)
{
  uint32_t       offset = 0;
  int            ret;

  DBG_INFO ("old_offset = %#x, new_offset = %#x\n", old_offset, new_offset);
//printf ("old_offset = %#x, new_offset = %#x\n", old_offset, new_offset);

  // Change state of variable from 0x3F to 0x3E
  DBG_INFO ("Changing state of old variable to 0x3E...\n");
  //printf ("Changing state of old variable to 0x3E...\n");
  ret = set_var_state (old_offset, VAR_IN_DELETED_TRANSITION);
  if (ret != 0) {
    return ret;
  }  
  DBG_INFO ("The state of old variable changed.\n");
  //printf ("The state of old variable changed.\n");
//   offset = get_nv_fv_offset () + new_offset;
  dump_buffer(data,data_size);
  DBG_INFO ("data_size: %#x, offset: %#x\n", offset, data_size);
//  //printf("data_size: %#x, offset: %#x\n", offset, data_size);
//   ret = flash_write (data, offset, data_size);
//   if (ret != 0) {
//     return ret;
//   }

  // ret = verify_data_written (offset, data, data_size);
  // if (ret != 0) {
  //   return 1;
  // }

  // Change state of variable from 0x3E to 0x3C
  DBG_INFO ("Changing state of old variable to 0x3C\n");
//printf ("Changing state of old variable to 0x3C\n");
  ret = set_var_state (old_offset, VAR_DELETED);
  DBG_INFO ("The state of old variable changed.\n");
  //printf ("The state of old variable changed.\n");

  return ret;
}
#if 0
int change_first_boot_dev (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info, short new_first_dev)
{
  SETUP_DATA        * data;
  int                 offset;
  short             * boot_options;
  int                 var_size;
  int                 index;
  int                 max_dev_no;
  uint8_t           * new_var;
  int                 blank_pos;
  int                 ret;
  uint8_t           * ptr;  
  if (!nv_fv_buff || !var_info) {
    return 1;
  }

  //
  // Check the new dev number to set as the first boot dev
  // 
  max_dev_no = sizeof(data->UefiPriorities)/sizeof(typeof(data->UefiPriorities[0])) - 1;
  if (new_first_dev > max_dev_no || new_first_dev < 0) {
    return 1;
  }

  new_var      = NULL;
  data         = NULL;
  boot_options = NULL;
  ptr          = (nv_fv_buff + var_info->Offset);
  GetVariabeInf(nv_fv_buff + var_info->Offset , &var_size , NULL , NULL , (uint8_t**)&data);
#if defined(__aarch64__) || defined(__mips__) || defined(__loongarch__)
  var_size = _INTSIZEOF(var_size);
#endif

  //
  // Locate the position of the new boot dev in BootOrder
  //
  //DBG_INFO ("Offset of UefiPriorities: %#x\n", ((SETUP_DATA *)0)->UefiPriorities);

  boot_options = data->UefiPriorities;
  for (index = 0; index < sizeof(data->UefiPriorities)/sizeof(typeof(data->UefiPriorities[0])); index++) {
    INFO ("%04x, ", *boot_options);
    if (new_first_dev == *boot_options++)
      break;
  }
  INFO ("\n");
  if (index > max_dev_no) {
    DBG_ERR ("No. %d device not found!\n", new_first_dev);
    return 1;
  }

  new_var = (uint8_t *)malloc(var_size);
  if (!new_var) {
    DBG_ERR ("Failed to allocate buffer!\n");
    return 1;
  }
  memset (new_var, 0, var_size);
  
  //
  // Create new KlSetup variable with new UefiPriorities
  //
  memcpy (new_var, (void *)ptr, var_size);

  GetVariabeInf(new_var , &var_size , NULL , NULL , (uint8_t**)&data);
  data->UefiPriorities[index] = data->UefiPriorities[0];
  data->UefiPriorities[0]     = new_first_dev;
  data->FBO_Init              = 0x01;

  //
  // Locate the blank area in the NV_FV for write a new KlSetup variable
  //
  blank_pos = 0;
  locate_blank_of_nv (nv_fv_buff, buff_len, &blank_pos);
  if (blank_pos <= 0) {
    DBG_ERR ("Failed to get the offset of the last variable in NV_FV!\n");
    return 1;
  }
  DBG_INFO ("Start of blank of NV: %#x\n", blank_pos);

  //
  // Write the new KlSetup variable into NV_FV
  //
  DBG_INFO ("Writing new KlSetup variable...\n");
  //dump_buffer (new_var, var_size);
  ret = write_variable (var_info->Offset, blank_pos, new_var, var_size);
  if (ret != 0) {
    DBG_ERR ("Failed write variable data into flash.\n");
  } else {
    DBG_INFO ("New KlSetup variable has been written into flash.\n");
  }

  free (new_var);

  return ret;  
}


int change1_first_boot_dev (uint8_t *nv_fv_buff, uint32_t buff_len,KLSETUP_VAR_INFO *var_info,BootOrder  *BootOrdervar,UINT16 *data1,CHAR8 *type,CHAR8 *typeanother)

{
  //SETUP_DATA        * data;
  int                 offset;
  short             * boot_options;
  int                 var_size;
  int                 index;
  int                 max_dev_no;
  //UINT16           * new_var;
  uint8_t           * new_var=NULL;
  uint8_t           * data;
  int                 blank_pos;
  int                 ret;
  uint8_t           * ptr; 
  UINT16                    NUM;
UINT16          temp ;
 uint8_t                 i=0;
UINT16           A,B;

int x=0,y=0;
  if (!nv_fv_buff || !var_info) {
    return 1;
  }
ptr          = (nv_fv_buff + var_info->Offset);
GetVariabeInf(nv_fv_buff + var_info->Offset , &var_size , NULL , NULL , (uint8_t**)&data);
#if defined(__aarch64__) || defined(__mips__) || defined(__loongarch__)
  var_size = _INTSIZEOF(var_size);
#endif
//printf("var_size=%d\n",var_size);
  new_var = (uint8_t *)malloc(var_size);
  if (!new_var) {
    DBG_ERR ("Failed to allocate buffer!\n");
    return 1;
  }

  memset (new_var, 0, var_size);
  memcpy (new_var, (void *)ptr, var_size);
  GetVariabeInf(new_var , &var_size , NULL , NULL , (uint8_t**)&data);
//new_var=data;
  //
  // Check the new dev number to set as the first boot dev
  // 
  //max_dev_no = sizeof(data->UefiPriorities)/sizeof(typeof(data->UefiPriorities[0])) - 1;
/*  if (new_first_dev > max_dev_no || new_first_dev < 0) {
    return 1;
  }

  new_var      = NULL;
  data         = NULL;
  boot_options = NULL;
  ptr          = (nv_fv_buff + var_info->Offset);
  

  //
  // Locate the position of the new boot dev in BootOrder
  //
  //DBG_INFO ("Offset of UefiPriorities: %#x\n", ((SETUP_DATA *)0)->UefiPriorities);

  boot_options = data->UefiPriorities;
  for (index = 0; index < sizeof(data->UefiPriorities)/sizeof(typeof(data->UefiPriorities[0])); index++) {
    INFO ("%04x, ", *boot_options);
    if (new_first_dev == *boot_options++)
      break;
  }
  INFO ("\n");
  if (index > max_dev_no) {
    DBG_ERR ("No. %d device not found!\n", new_first_dev);
    return 1;
  }

*/

//printf("BootOrdervar[0].type=%s,type=%s,typeanother=%s\n",BootOrdervar[1].type,type,typeanother);
x=strcmp(BootOrdervar[0].type,type);
y=strcmp(BootOrdervar[0].type,typeanother);
//printf("x=%d,y=%d",x,y);
  //if((strcmp(BootOrdervar[0].type,type)) || (strcmp(BootOrdervar[0].type,typeanother)))
if((x==0)||(y==0))
  {
    printf("Already In this boot order!\n\r");
	return 1;
  }
  else
  {
    //NUM=*(((UINT16 *)new_var)+index);

   temp =*(((UINT16 *)data));
//printf("!!temp=%x\n",temp);
    //BootOrder temp=BootOrdervar[0];
    //BootOrdervar[0]=type;
    for(i=0;i<10;i++)
      {
         
        if(BootOrdervar[i].type==NULL)
       continue;
//printf("!!i=%d!!\n",i);
		x=strcmp(BootOrdervar[i].type,type);
		y=strcmp(BootOrdervar[i].type,typeanother);
        if((x==0)||(y==0))
           {//printf("same\n");
            A=*((UINT16 *)data);
			B=*((((UINT16 *)data)+i));
			//printf("!!A=%x\n",A);
			//printf("!!B=%x\n",B);
			*((UINT16 *)data)=B;
            *((((UINT16 *)data)+i))=temp;
            //BootOrdervar[i]=temp;
            break;
           }
      }
    
   }



  /*new_var = (uint8_t *)malloc(var_size);
  if (!new_var) {
    DBG_ERR ("Failed to allocate buffer!\n");
    return 1;
  }
  memset (new_var, 0, var_size);
  
  //
  // Create new KlSetup variable with new UefiPriorities
  //
  memcpy (new_var, (void *)ptr, var_size);

  GetVariabeInf(new_var , &var_size , NULL , NULL , (uint8_t**)&data);
  data->UefiPriorities[index] = data->UefiPriorities[0];
  data->UefiPriorities[0]     = new_first_dev;
  data->FBO_Init              = 0x01;
*/

  //
  // Locate the blank area in the NV_FV for write a new KlSetup variable
  //
//printf("!!LOL Noproblem!!\n");
  blank_pos = 0;
  locate_blank_of_nv (nv_fv_buff, buff_len, &blank_pos);
  if (blank_pos <= 0) {
    DBG_ERR ("Failed to get the offset of the last variable in NV_FV!\n");
    return 1;
  }
  DBG_INFO ("Start of blank of NV: %#x\n", blank_pos);

  //
  // Write the new KlSetup variable into NV_FV
  //
  DBG_INFO ("Writing new KlSetup variable...\n");
  //dump_buffer (new_var, var_size);
//printf("!!LOL Noproblem write_variable in!!\n");
  ret = write_variable (var_info->Offset, blank_pos, new_var, var_size);
//printf("!!LOL Noproblem write_variable out!!\n");
  if (ret != 0) {
    DBG_ERR ("Failed write variable data into flash.\n");
    printf ("Failed write variable data into flash.\n");
  } else {
    DBG_INFO ("New KlSetup variable has been written into flash.\n");
    printf ("New  variable has been written into flash.\n");
  }

  //free (new_var);

  return ret;  
}


int change_NETWORKSTACK_state (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info, NETWORK_STACK new_state)
{
  //VARIABLE_HEADER   *var_hdr;
  NETWORK_STACK        *data;
  int               offset;
  short             numa;
  int               var_size;
  int               index;
  int               max_dev_no;
  uint8_t           *new_var;
  int               blank_pos;
  int               ret;
  uint8_t           * ptr;

  if (!nv_fv_buff || !var_info) {
    return 1;
  }
  new_var      = NULL;
  data         = NULL;

  //var_hdr  = (VARIABLE_HEADER *)(nv_fv_buff + var_info->Offset);

  //var_size = sizeof(VARIABLE_HEADER) + var_hdr->NameSize + var_hdr->DataSize;
  ptr          = (nv_fv_buff + var_info->Offset);
  GetVariabeInf(nv_fv_buff + var_info->Offset , &var_size , NULL , NULL , (uint8_t**)&data);
#if defined(__aarch64__) || defined(__mips__) || defined(__loongarch__)
  var_size = _INTSIZEOF(var_size);
#endif

  //
  // Locate the position of the new boot dev in BootOrder
  //
  //DBG_INFO ("Offset of UefiPriorities: %#x\n", ((SETUP_DATA *)0)->UefiPriorities);
  //offset = sizeof(VARIABLE_HEADER) + var_hdr->NameSize;
  //data = (NETWORK_STACK *)(nv_fv_buff + var_info->Offset + offset);

  new_var = (uint8_t *)malloc(var_size);
  if (!new_var) {
    DBG_ERR ("Failed to allocate buffer!\n");
    return 1;
  }
  memset (new_var, 0, var_size);
  
  //
  // Create new KlSetup variable with new UefiPriorities
  //
  //memcpy (new_var, (void *)var_hdr, var_size);
  //offset = sizeof(VARIABLE_HEADER) + var_hdr->NameSize;
  //data = (NETWORK_STACK *)(new_var + offset);
  memcpy (new_var, (void *)ptr, var_size);

  GetVariabeInf(new_var , &var_size , NULL , NULL , (uint8_t**)&data);
  *data= new_state;
printf("!!new_NETWORKSTACK_state=%d\n",data->Enable);
  //
  // Locate the blank area in the NV_FV for write a new KlSetup variable
  //
  blank_pos = 0;
  locate_blank_of_nv (nv_fv_buff, buff_len, &blank_pos);
  if (blank_pos <= 0) {
    DBG_ERR ("Failed to get the offset of the last variable in NV_FV!\n");
    return 1;
  }
  DBG_INFO ("Start of blank of NV: %#x\n", blank_pos);

  //
  // Write the new KlSetup variable into NV_FV
  //
  DBG_INFO ("Writing new NETWORK_STACK variable...\n");
  //dump_buffer (new_var, var_size);
  ret = write_variable (var_info->Offset, blank_pos, new_var, var_size);
  if (ret != 0) {
    DBG_ERR ("Failed write variable data into flash.\n");
  } else {
    DBG_INFO ("New NETWORKSTACK variable has been written into flash.\n");
  }

  free (new_var);

  return ret;  
}






int change_KlSetup_state (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info, SETUP_DATA new_state)
{
  //VARIABLE_HEADER   *var_hdr;
  SETUP_DATA        *data;
  int               offset;
  short             numa;
  int               var_size;
  int               index;
  int               max_dev_no;
  uint8_t           *new_var;
  int               blank_pos;
  int               ret;
  uint8_t           * ptr;

  if (!nv_fv_buff || !var_info) {
    return 1;
  }
  new_var      = NULL;
  data         = NULL;

  //var_hdr  = (VARIABLE_HEADER *)(nv_fv_buff + var_info->Offset);

  //var_size = sizeof(VARIABLE_HEADER) + var_hdr->NameSize + var_hdr->DataSize;  
   ptr          = (nv_fv_buff + var_info->Offset);
  GetVariabeInf(nv_fv_buff + var_info->Offset , &var_size , NULL , NULL , (uint8_t**)&data);    
#if defined(__aarch64__) || defined(__mips__) || defined(__loongarch__)
  var_size = _INTSIZEOF(var_size);
#endif

  //
  // Locate the position of the new boot dev in BootOrder
  //
  //DBG_INFO ("Offset of UefiPriorities: %#x\n", ((SETUP_DATA *)0)->UefiPriorities);
  //offset = sizeof(VARIABLE_HEADER) + var_hdr->NameSize;
  //data = (SETUP_DATA *)(nv_fv_buff + var_info->Offset + offset);

  new_var = (uint8_t *)malloc(var_size);
  if (!new_var) {
    DBG_ERR ("Failed to allocate buffer!\n");
    return 1;
  }
  memset (new_var, 0, var_size);
  
  //
  // Create new KlSetup variable with new UefiPriorities
  //
  //memcpy (new_var, (void *)var_hdr, var_size);
  //offset = sizeof(VARIABLE_HEADER) + var_hdr->NameSize;
  //data = (SETUP_DATA *)(new_var + offset);
    memcpy (new_var, (void *)ptr, var_size);

  GetVariabeInf(new_var , &var_size , NULL , NULL , (uint8_t**)&data);
  *data= new_state;
//printf("!!new_KlSetup_NumaEn_state=%d\n",data->NumaEn);
  //
  // Locate the blank area in the NV_FV for write a new KlSetup variable
  //
  blank_pos = 0;
  locate_blank_of_nv (nv_fv_buff, buff_len, &blank_pos);
  if (blank_pos <= 0) {
    DBG_ERR ("Failed to get the offset of the last variable in NV_FV!\n");
    return 1;
  }
  DBG_INFO ("Start of blank of NV: %#x\n", blank_pos);

  //
  // Write the new KlSetup variable into NV_FV
  //
  DBG_INFO ("Writing new KlSetup variable...\n");
  //dump_buffer (new_var, var_size);
  ret = write_variable (var_info->Offset, blank_pos, new_var, var_size);
  if (ret != 0) {
    DBG_ERR ("Failed write variable data into flash.\n");
  } else {
    DBG_INFO ("New KlSetup variable has been written into flash.\n");
  }

  free (new_var);

  return ret;  
}


int change_setup_default1 (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info)
{
  //VARIABLE_HEADER   *var_hdr;
  SETUP_DATA        *data;
  int               offset;
  short             numa;
  int               var_size;
  int               index;
  int               max_dev_no;
  uint8_t           *new_var;
  int               blank_pos;
  int               ret;
  uint8_t           * ptr;

  if (!nv_fv_buff || !var_info) {
    return 1;
  }

  new_var      = NULL;
  data         = NULL;

  //var_hdr  = (VARIABLE_HEADER *)(nv_fv_buff + var_info->Offset);

  //var_size = sizeof(VARIABLE_HEADER) + var_hdr->NameSize + var_hdr->DataSize;  
      ptr          = (nv_fv_buff + var_info->Offset);
  GetVariabeInf(nv_fv_buff + var_info->Offset , &var_size , NULL , NULL , (uint8_t**)&data);
#if defined(__aarch64__) || defined(__mips__) || defined(__loongarch__)
  var_size = _INTSIZEOF(var_size);
#endif

  //
  // Locate the position of the new boot dev in BootOrder
  //
  //DBG_INFO ("Offset of UefiPriorities: %#x\n", ((SETUP_DATA *)0)->UefiPriorities);
  //offset = sizeof(VARIABLE_HEADER) + var_hdr->NameSize;
  //data = (SETUP_DATA *)(nv_fv_buff + var_info->Offset + offset);

  new_var = (uint8_t *)malloc(var_size);
  if (!new_var) {
    DBG_ERR ("Failed to allocate buffer!\n");
    return 1;
  }
  memset (new_var, 0, var_size);
  
  //
  // Create new KlSetup variable with new UefiPriorities
  //
  //memcpy (new_var, (void *)var_hdr, var_size);
  //offset = sizeof(VARIABLE_HEADER) + var_hdr->NameSize;
  //data = (SETUP_DATA *)(new_var + offset);
  memcpy (new_var, (void *)ptr, var_size);

  GetVariabeInf(new_var , &var_size , NULL , NULL , (uint8_t**)&data);
  data->UefiPriorities[0]= 0;
  data->UefiPriorities[1]= 3;
  data->UefiPriorities[2]= 2;
  data->UefiPriorities[3]= 4;
  data->UefiPriorities[4]= 1;
  data->UefiPriorities[5]= 6;
  data->UefiPriorities[6]= 5;
  data->FBO_Init= 0x01;
  //
  // Locate the blank area in the NV_FV for write a new KlSetup variable
  //
  blank_pos = 0;
  locate_blank_of_nv (nv_fv_buff, buff_len, &blank_pos);
  if (blank_pos <= 0) {
    DBG_ERR ("Failed to get the offset of the last variable in NV_FV!\n");
    return 1;
  }
  DBG_INFO ("Start of blank of NV: %#x\n", blank_pos);

  //
  // Write the new KlSetup variable into NV_FV
  //
  DBG_INFO ("Writing new KlSetup variable...\n");
  //dump_buffer (new_var, var_size);
  ret = write_variable (var_info->Offset, blank_pos, new_var, var_size);
  if (ret != 0) {
    DBG_ERR ("Failed write variable data into flash.\n");
  } else {
    DBG_INFO ("New KlSetup variable has been written into flash.\n");
  }

  free (new_var);

  return ret;  
}
#endif
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

  // DBG_INFO ("[%s] name= %s begin!\n", __func__, name);
  list_for_each(pos, &var_list) {
    var = list_entry(pos, klvar_entry_t, list);
    UnicodeStrToAsciiStr ((CONST CHAR16 *)var->name, ret_name);
    // DBG_INFO ("[%s] ret_name= %s !\n", __func__, ret_name);
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
      DBG_INFO ("[%s] state= 0x%x !\n", __func__, state);
      continue;
    }
    // DBG_INFO ("[%s] *attributes= 0x%x !\n", __func__, *attributes);
    // dump_buffer(&guid,sizeof(efi_guid_t));
    // dump_buffer(&ret_guid,sizeof(efi_guid_t));
    // DBG_INFO ("[%s] sizeof(efi_guid_t)= %ld !\n", __func__, sizeof(efi_guid_t));
    if (memcmp(&ret_guid, &guid, sizeof(efi_guid_t)) != 0) {
      continue;
    }
    // dump_buffer((void *)name,strlen(name));
    // dump_buffer(ret_name,strlen(ret_name));
    if (strcmp(name, ret_name) != 0) {
      continue;
    }

    DBG_INFO ("[%s] attr= 0x%x to 0x%x !\n", __func__, attr, attributes);
    if (attr != attributes) {
      DBG_INFO ("[%s] change attr= 0x%x to 0x%x !\n", __func__, attr, attributes);
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
      DBG_INFO("[%s] > buf_len !\n", __func__);
      return -1;
    }
    
    memcpy( nv_fv_buff + var->offset + var->headerSize + var->nameSize, var->data, var->dataSize);
    dump_buffer(nv_fv_buff + var->offset + var->headerSize + var->nameSize, var->dataSize);
    ret = 1;
    return ret;
  }
  DBG_INFO("[%s] no match !\n", __func__);

  return -1;
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
  // DBG_INFO ("[%s] name= %s begin!\n", __func__, name);
  list_for_each(pos, &var_list) {
    var = list_entry(pos, klvar_entry_t, list);
    UnicodeStrToAsciiStr ((CONST CHAR16 *)var->name, ret_name);
    // DBG_INFO ("[%s] ret_name= %s !\n", __func__, ret_name);
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
      // DBG_INFO ("[%s] state= 0x%x !\n", __func__, state);
      continue;
    }
    // DBG_INFO ("[%s] *attributes= 0x%x !\n", __func__, *attributes);
    // dump_buffer(&guid,sizeof(efi_guid_t));
    // dump_buffer(&ret_guid,sizeof(efi_guid_t));
    // DBG_INFO ("[%s] sizeof(efi_guid_t)= %ld !\n", __func__, sizeof(efi_guid_t));
    if (memcmp(&ret_guid, &guid, sizeof(efi_guid_t)) != 0) {
      continue;
    }
    // dump_buffer((void *)name,strlen(name));
    // dump_buffer(ret_name,strlen(ret_name));
    if (strcmp(name, ret_name) != 0) {
      continue;
    }
    // DBG_INFO ("[%s] name= %s !\n", __func__, name);
    if (!(newbuf = calloc(var->dataSize, sizeof (uint8_t)))) {
      DBG_INFO("could not allocate memory");
      *data = newbuf = NULL;
      *data_size = 0;
      return -1;
    }
    memcpy(newbuf, var->data, var->dataSize);
    *data = newbuf;
    *data_size = var->dataSize;
    DBG_INFO ("[%s] find var name= %s !\n", __func__, name);
    // dump_buffer(*data, *data_size);
    ret = 1;
    return ret;
  }
  DBG_INFO("[%s] no match !\n", __func__);
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
    DBG_INFO ("[%s] npos= %zd !\n", __func__, npos);
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
      DBG_INFO ("[%s] npos= %zd !\n", __func__, npos);
      npos++;

      DBG_INFO ("[%s] ret_name= %s !\n", __func__, ret_name);
      *guid = &ret_guid;
		  *name = ret_name;
      //ret = 1;
      break;
    }
    //DBG_INFO ("[%s] i= 0x%zd !\n", __func__, i);
    i++;
  }
end:
  return (int)npos;
}
