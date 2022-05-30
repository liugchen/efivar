
#ifndef _UEFI_H_
#define _UEFI_H_

#define IN
#define OUT
#define EFIAPI

#define VOID      void
#define CONST     const

  //
  // Assume standard MIPS alignment. 
  //
  typedef unsigned long long  UINT64;
  typedef long long           INT64;
  typedef unsigned int        UINT32;
  typedef int                 INT32;
  typedef unsigned short      UINT16;
  typedef unsigned short      CHAR16;
  typedef short               INT16;
  typedef unsigned char       BOOLEAN;
  typedef unsigned char       UINT8;
  typedef char                CHAR8;
  typedef signed char         INT8;
  typedef UINT64              UINTN;
  typedef INT64               INTN;

typedef UINTN               RETURN_STATUS;
typedef RETURN_STATUS       EFI_STATUS;

#define EFI_SUCCESS                  0
#define RETURN_SUCCESS               0

#ifdef NULL
#undef NULL
#endif
#define NULL                         (VOID *)0

#define MAX_BIT                      0x8000000000000000ULL
#define ENCODE_ERROR(StatusCode)     ((RETURN_STATUS)(MAX_BIT | (StatusCode)))
#define RETURN_UNSUPPORTED           ENCODE_ERROR (3)

	///
	/// Maximum legal x64 INTN and UINTN values.
	///
#define MAX_INTN   ((INTN)0x7FFFFFFFFFFFFFFFULL)
#define MAX_UINTN  ((UINTN)0xFFFFFFFFFFFFFFFFULL)

typedef UINTN RETURN_STATUS;

#define FALSE      ((BOOLEAN)(0==1))

#define ASSERT(Expression)      \
  do {                          \
    if (!(Expression)) {        \
      while(1);                 \
	}                           \
  } while (FALSE)

//
// Modifiers to abstract standard types to aid in debug of problems
//
#define CONST     const
#define STATIC    static
#define VOID      void

///
/// Boolean true value.  UEFI Specification defines this value to be 1,
/// but this form is more portable.
///
#define TRUE  ((BOOLEAN)(1==1))

///
/// Boolean false value.  UEFI Specification defines this value to be 0,
/// but this form is more portable.
///
#define FALSE ((BOOLEAN)(0==1))

#define EFI_D_WARN         0


#define SIGNATURE_16(A, B)        ((A) | (B << 8))
#define SIGNATURE_32(A, B, C, D)  (SIGNATURE_16 (A, B) | (SIGNATURE_16 (C, D) << 16))
#define SIGNATURE_64(A, B, C, D, E, F, G, H) \
    (SIGNATURE_32 (A, B, C, D) | ((UINT64) (SIGNATURE_32 (E, F, G, H)) << 32))

#define _INTSIZEOF(n)            ((n+sizeof(int)-1)& ~(sizeof(int)-1))
#endif // _UEFI_H_
