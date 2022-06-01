
#ifndef _UEFI_VAR_H_
#define _UEFI_VAR_H_


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "Uefi.h"
#include "guid.h"

#pragma pack(1)
typedef struct {
  UINT32  Data1;
  UINT16  Data2;
  UINT16  Data3;
  UINT8   Data4[8];
} EFI_GUID;

//
// Firmware Volume Block Attributes definition
//
typedef UINT32  EFI_FVB_ATTRIBUTES;

//
// Firmware Volume Header Block Map Entry definition
//
typedef struct {
  UINT32  NumBlocks;
  UINT32  BlockLength;
} EFI_FV_BLOCK_MAP_ENTRY;

//
// Firmware Volume Header definition
//
typedef struct {
  UINT8                   ZeroVector[16];
  EFI_GUID                FileSystemGuid;
  UINT64                  FvLength;
  UINT32                  Signature;
  EFI_FVB_ATTRIBUTES      Attributes;
  UINT16                  HeaderLength;
  UINT16                  Checksum;
  UINT8                   Reserved[3];
  UINT8                   Revision;
  EFI_FV_BLOCK_MAP_ENTRY  FvBlockMap[1];
} EFI_FIRMWARE_VOLUME_HEADER;

//
// Variable State flags
//
#define VAR_IN_DELETED_TRANSITION     0xfe  // Variable is in obsolete transistion
#define VAR_DELETED                   0xfd  // Variable is obsolete
#define VAR_HEADER_VALID_ONLY         0x7f  // Variable header has been valid.
#define VAR_ADDED                     0x3f  // Variable has been completely added
#define IS_VARIABLE_STATE(_c, _Mask)  (BOOLEAN) (((~_c) & (~_Mask)) != 0)

///
/// Variable Store region header.
///
typedef struct {
  ///
  /// Variable store region signature.
  ///
  EFI_GUID  Signature;
  ///
  /// Size of entire variable store,
  /// including size of variable store header but not including the size of FvHeader.
  ///
  UINT32  Size;
  ///
  /// Variable region format state.
  ///
  UINT8   Format;
  ///
  /// Variable region healthy state.
  ///
  UINT8   State;
  UINT16  Reserved;
  UINT32  Reserved1;
} VARIABLE_STORE_HEADER;


typedef struct {
  UINT16    StartId;
  UINT8     State;
  UINT8     Reserved;
  UINT32    Attributes;
  UINT32    NameSize;
  UINT32    DataSize;
  EFI_GUID  VendorGuid;
} DEFAULT_VARIABLE_HEADER;

typedef struct {
  UINT16  Year;
  UINT8   Month;
  UINT8   Day;
  UINT8   Hour;
  UINT8   Minute;
  UINT8   Second;
  UINT8   Pad1;
  UINT32  Nanosecond;
  INT16   TimeZone;
  UINT8   Daylight;
  UINT8   Pad2;
} EFI_TIME;

typedef struct {
  ///
  /// Variable Data Start Flag.
  ///
  UINT16      StartId;
  ///
  /// Variable State defined above.
  ///
  UINT8       State;
  UINT8       Reserved;
  ///
  /// Attributes of variable defined in UEFI specification.
  ///
  UINT32      Attributes;
  ///
  /// Associated monotonic count value against replay attack.
  ///
  UINT64      MonotonicCount;
  ///
  /// Associated TimeStamp value against replay attack.
  ///
  EFI_TIME    TimeStamp;
  ///
  /// Index of associated public key in database.
  ///
  UINT32      PubKeyIndex;
  ///
  /// Size of variable null-terminated Unicode string name.
  ///
  UINT32      NameSize;
  ///
  /// Size of the variable data without this header.
  ///
  UINT32      DataSize;
  ///
  /// A unique identifier for the vendor that produces and consumes this varaible.
  ///
  EFI_GUID    VendorGuid;
} AUTHENTICATED_VARIABLE_HEADER;

#define GET_VARIABLE_NAME_PTR(a)  (CHAR16 *) ((UINTN) (a) + sizeof (VARIABLE_HEADER))

//#if defined(__mips__) || defined(__loongarch__) || defined(__amd64__) || defined(__sw_64__)
#ifndef __aarch64__
typedef struct {
  UINT8 Numlock;
  UINT8 MemSlotTotal;
  UINT8 BaudRate[ 1 ];
  UINT8 DataBits[ 1 ];
  UINT8 Parity[ 1 ];
  UINT8 StopBits[ 1 ];
  UINT8 FlowControl[ 1 ];
  UINT8 ConsoleRedirectionEnable[ 1 ];
  UINT8 TerminalType[ 1 ];
  UINT8 UsbHsPort[6];
  UINT8 UsbSsPort[4];
  UINT8 SataEnable;
  UINT8 SataPort[8];
  UINT8 PcieRootPortEn[20];
  UINT8 PcieRootPortSpeed[20];
  UINT8 StateAfterG3;
  UINT8 PxeOpRom;
  UINT8 NetCardController;
  UINT8 DeinfoUsbPort[30];
  UINT8 DeinfoSataHD[20];
  UINT8 DeinfoSataCD[5];
  UINT8 FBO_Init;
  UINT8 BootMode;
  UINT16 UefiPriorities[16];
} SETUP_DATA;

//#elif defined(__aarch64__)
#else
typedef struct {
  UINT16 UefiPriorities[16];
  UINT8 FBO_Init;
  UINT8 Numlock;
  UINT8 MemSlotTotal;
  UINT8 BaudRate[ 1 ];
  UINT8 DataBits[ 1 ];
  UINT8 Parity[ 1 ];
  UINT8 StopBits[ 1 ];
  UINT8 FlowControl[ 1 ];
  UINT8 ConsoleRedirectionEnable[ 1 ];
  UINT8 TerminalType[ 1 ];
  UINT8 SolControl;
  UINT8 UsbHsPort[6];
  UINT8 UsbSsPort[8];
  UINT8 SataEnable;
  UINT8 SataPort[8];
  UINT8 PcieRootPortEn[20];
  UINT8 PcieRootPortSpeed[20];
  UINT8 StateAfterG3;
  UINT8 PciEnable;
  UINT8 SerialEnable;
  UINT8 DeinfoUsbPort[30];
  UINT8 DeinfoNicDevice[30];
  UINT8 DeinfoSataHD[20];
  UINT8 DeinfoSataCD[5];
  UINT8 BootMode;
  UINT8 VideoCardSetting;
  UINT8 FindMultiTypeVideo;
  UINT8 EnableSmmu;
  UINT8 SocketNumber;
  UINT8 CompatibilityMode;
  UINT8 SrIovSupport;
  UINT8 NumaEn;
  UINT8 OnboardLan;
  UINT8 AddinLanCard;
  UINT8 Sata[2];
  UINT8 PxeOpRom;
} SETUP_DATA;

#endif

typedef struct{
  UINT8 Enable;
  UINT8 Ipv4Pxe;
  UINT8 Ipv6Pxe;
  UINT8 IpsecCertificate;
  UINT8 PxeBootWaitTime;
  UINT8 MediaDetectCount;
  UINT8 PxeRtry;
  UINT8 PxeBoot;
} NETWORK_STACK;                                                                                                                                                                                                                                         

#pragma pack()

typedef struct _KLSETUP_VAR_INFO {
  UINT8   IsValid;
  UINT32  Offset;     // Offset of variable "KlSetup" in the NV_FV
} KLSETUP_VAR_INFO;


#pragma pack(1)
typedef struct _EFI_LOAD_OPTION {
  ///
  /// The attributes for this load option entry. All unused bits must be zero
  /// and are reserved by the UEFI specification for future growth.
  ///
  UINT32                           Attributes;
  ///
  /// Length in bytes of the FilePathList. OptionalData starts at offset
  /// sizeof(UINT32) + sizeof(UINT16) + StrSize(Description) + FilePathListLength
  /// of the EFI_LOAD_OPTION descriptor.
  ///
  UINT16                           FilePathListLength;
  ///
  /// The user readable description for the load option.
  /// This field ends with a Null character.
  ///
  // CHAR16                        Description[];
  ///
  /// A packed array of UEFI device paths. The first element of the array is a
  /// device path that describes the device and location of the Image for this
  /// load option. The FilePathList[0] is specific to the device type. Other
  /// device paths may optionally exist in the FilePathList, but their usage is
  /// OSV specific. Each element in the array is variable length, and ends at
  /// the device path end structure. Because the size of Description is
  /// arbitrary, this data structure is not guaranteed to be aligned on a
  /// natural boundary. This data structure may have to be copied to an aligned
  /// natural boundary before it is used.
  ///
  // EFI_DEVICE_PATH_PROTOCOL      FilePathList[];
  ///
  /// The remaining bytes in the load option descriptor are a binary data buffer
  /// that is passed to the loaded image. If the field is zero bytes long, a
  /// NULL pointer is passed to the loaded image. The number of bytes in
  /// OptionalData can be computed by subtracting the starting offset of
  /// OptionalData from total size in bytes of the EFI_LOAD_OPTION.
  ///
  // UINT8                         OptionalData[];
} EFI_LOAD_OPTION;


#pragma pack(1)
/**
  This protocol can be used on any device handle to obtain generic path/location
  information concerning the physical device or logical device. If the handle does
  not logically map to a physical device, the handle may not necessarily support
  the device path protocol. The device path describes the location of the device
  the handle is for. The size of the Device Path can be determined from the structures
  that make up the Device Path.
**/
typedef struct {
  UINT8 Type;       ///< 0x01 Hardware Device Path.
                    ///< 0x02 ACPI Device Path.
                    ///< 0x03 Messaging Device Path.
                    ///< 0x04 Media Device Path.
                    ///< 0x05 BIOS Boot Specification Device Path.
                    ///< 0x7F End of Hardware Device Path.

  UINT8 SubType;    ///< Varies by Type
                    ///< 0xFF End Entire Device Path, or
                    ///< 0x01 End This Instance of a Device Path and start a new
                    ///< Device Path.

  UINT8 Length[2];  ///< Specific Device Path data. Type and Sub-Type define
                    ///< type of data. Size of data is included in Length.

} EFI_DEVICE_PATH_PROTOCOL;

typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Device's PnP hardware ID stored in a numeric 32-bit
  /// compressed EISA-type ID. This value must match the
  /// corresponding _HID in the ACPI name space.
  ///
  UINT32                          HID;
  ///
  /// Unique ID that is required by ACPI if two devices have the
  /// same _HID. This value must also match the corresponding
  /// _UID/_HID pair in the ACPI name space. Only the 32-bit
  /// numeric value type of _UID is supported. Thus, strings must
  /// not be used for the _UID in the ACPI name space.
  ///
  UINT32                          UID;
} ACPI_HID_DEVICE_PATH;

typedef VOID                      *EFI_HANDLE;

/*struct _EFI_BLOCK_IO_PROTOCOL {
  ///
  /// The revision to which the block IO interface adheres. All future
  /// revisions must be backwards compatible. If a future version is not
  /// back wards compatible, it is not the same GUID.
  ///
  UINT64              Revision;
  ///
  /// Pointer to the EFI_BLOCK_IO_MEDIA data for this device.
  ///
  EFI_BLOCK_IO_MEDIA  *Media;

  EFI_BLOCK_RESET     Reset;
  EFI_BLOCK_READ      ReadBlocks;
  EFI_BLOCK_WRITE     WriteBlocks;
  EFI_BLOCK_FLUSH     FlushBlocks;

};
*/
#define  BDS_EFI_MESSAGE_ATAPI_BOOT       0x0301 // Type 03; Sub-Type 01
#define  BDS_EFI_MESSAGE_SCSI_BOOT        0x0302 // Type 03; Sub-Type 02
#define  BDS_EFI_MESSAGE_USB_DEVICE_BOOT  0x0305 // Type 03; Sub-Type 05
#define  BDS_EFI_MESSAGE_SATA_BOOT        0x0312 // Type 03; Sub-Type 18
#define  BDS_EFI_MESSAGE_MAC_BOOT         0x030b // Type 03; Sub-Type 11
#define  BDS_EFI_MESSAGE_MISC_BOOT        0x03FF
//add-klk-lyang-P000A-start//
#define  BDS_EFI_MESSAGE_USB_CDROM_BOOT   0x03FE //not spec
//add-klk-lyang-P000A-end//
#define  BDS_EFI_ACPI_FLOPPY_BOOT         0x0201
#define  BDS_EFI_MEDIA_HD_BOOT            0x0401 // Type 04; Sub-Type 01
#define  BDS_EFI_MEDIA_CDROM_BOOT         0x0402 // Type 04; Sub-Type 02
//add-klk-lyang-P000A-start//
#define  BDS_EFI_MEDIA_FW_FILE_BOOT       0x0406 // Type 04; Sub-Type 06
//add-klk-lyang-P000A-end//
#define  BDS_LEGACY_BBS_BOOT              0x0501 //  Type 05; Sub-Type 01
#define  BDS_EFI_MESSAGE_NVME_BOOT        0x0317 // Type 03; Sub-Type 23
#define  BDS_EFI_UNSUPPORT                0xFFFF

///
/// BIOS Boot Specification Device Path.
///
#define HARDWARE_DEVICE_PATH      0x01
#define ACPI_DEVICE_PATH          0x02
#define MESSAGING_DEVICE_PATH     0x03
#define MEDIA_DEVICE_PATH         0x04
#define BBS_DEVICE_PATH           0x05



#define MSG_ATAPI_DP                0x01
#define MSG_SCSI_DP                 0x02
#define MSG_FIBRECHANNEL_DP         0x03
#define MSG_1394_DP                 0x04
#define MSG_USB_DP                  0x05
#define MSG_I2O_DP                  0x06
#define MSG_INFINIBAND_DP           0x09
#define MSG_VENDOR_DP               0x0a
#define MSG_MAC_ADDR_DP             0x0b
#define MSG_IPv4_DP                 0x0c
#define MSG_IPv6_DP                 0x0d
#define MSG_UART_DP                 0x0e
#define MSG_USB_CLASS_DP            0x0f
#define MSG_USB_WWID_DP             0x10
#define MSG_DEVICE_LOGICAL_UNIT_DP  0x11
#define MSG_SATA_DP                 0x12
#define MSG_ISCSI_DP                0x13
#define MSG_VLAN_DP                 0x14
#define MSG_FIBRECHANNELEX_DP       0x15
#define MSG_SASEX_DP                0x16
#define MSG_NVME_NAMESPACE_DP       0x17
#define MSG_URI_DP                  0x18
#define MSG_UFS_DP                  0x19
#define MSG_SD_DP                   0x1A
#define MSG_BLUETOOTH_DP            0x1b
#define MSG_WIFI_DP                 0x1C
#define MSG_EMMC_DP                 0x1D
#define MSG_BLUETOOTH_LE_DP         0x1E
#define MSG_DNS_DP                  0x1F

#define MEDIA_HARDDRIVE_DP          0x01
#define MEDIA_CDROM_DP              0x02
#define MEDIA_VENDOR_DP             0x03  ///< Media vendor device path subtype.
#define MEDIA_FILEPATH_DP           0x04
#define MEDIA_PROTOCOL_DP           0x05
#define MEDIA_PIWG_FW_FILE_DP       0x06
#define MEDIA_PIWG_FW_VOL_DP      0x07
#define MEDIA_RELATIVE_OFFSET_RANGE_DP 0x08
#define MEDIA_RAM_DISK_DP         0x09

#define END_DEVICE_PATH_TYPE                 0x7f

typedef struct{  
  UINT16   bootnum;
  CHAR8*  type;  
}BootOrder;

typedef enum  { VOLUME_HEADER, STORE_HEADER, UN_KNOWN} nvType;

typedef struct _EFI_BLOCK_IO_PROTOCOL  EFI_BLOCK_IO_PROTOCOL;
void dump_buffer (void *buffer, int len);
int get_klsetup_var (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info);
int get_network_stack_var (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info);
int display_boot_order (uint8_t *nv_fv_buff, KLSETUP_VAR_INFO *var_info);
int change_first_boot_dev (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info, short new_first_dev);
int get_curr_boot_dev (uint8_t *nv_fv_buff, KLSETUP_VAR_INFO *var_info, short *curr_dev);
UINT8 GetAuthenticatedFlag(void * FvHeader );
int change_setup_default1 (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info);
int change_setup_default3 (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info);

int get_curr_KlSetup_state (uint8_t *nv_fv_buff, KLSETUP_VAR_INFO *var_info, SETUP_DATA *curr_state);
int change_KlSetup_state (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info, SETUP_DATA new_state);

int get_curr_NETWORKSTACK_state (uint8_t *nv_fv_buff, KLSETUP_VAR_INFO *var_info, NETWORK_STACK *curr_state);
int change_NETWORKSTACK_state (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info, NETWORK_STACK new_state);


//int display1_boot_order (uint8_t *nv_fv_buff, uint32_t buff_len,KLSETUP_VAR_INFO *var_info);
int display1_boot_order (uint8_t *nv_fv_buff, uint32_t buff_len,KLSETUP_VAR_INFO *var_info,BootOrder  *BootOrdervar,UINT16 *data);

int change1_first_boot_dev (uint8_t *nv_fv_buff, uint32_t buff_len,KLSETUP_VAR_INFO *var_info,BootOrder  *BootOrdervar,UINT16 *data,CHAR8 *type,CHAR8 *typeanother);
int get_variable_var (uint8_t *nv_fv_buff, uint32_t buff_len, KLSETUP_VAR_INFO *var_info,wchar_t *seek_var_name);

int parse_nv_frame(uint8_t *nv_fv_buff, uint32_t buff_len);
int uefivar_get_next_variable_name(efi_guid_t **guid, char **name);
int uefivar_get_variable(efi_guid_t guid, const char *name, uint8_t **data,
		  size_t *data_size, uint32_t *attributes);
int uefivar_set_variable(efi_guid_t guid, const char *name, uint8_t *data,
		  size_t data_size, uint32_t attributes, mode_t mode, uint8_t *nv_fv_buff, uint32_t buff_len); 
int uefivar_del_variable(efi_guid_t guid, const char *name, uint8_t *buffer, int len, uint8_t *nv_fv_buff, uint32_t buff_len);
void clear_list(void); 
int is_have_name(const char *name);
int new_bootnext(uint8_t *nv_fv_buff, uint32_t buff_len);
#endif






