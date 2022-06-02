
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




typedef enum  { VOLUME_HEADER, STORE_HEADER, UN_KNOWN} nvType;

UINT8 GetAuthenticatedFlag(void * FvHeader );

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






