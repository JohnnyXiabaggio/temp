#ifndef STD_TYPES_H
#define STD_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t   sint8;
typedef int16_t  sint16;
typedef int32_t  sint32;

typedef uint8 boolean;
#define TRUE  ((boolean)1u)
#define FALSE ((boolean)0u)

typedef uint8 Std_ReturnType;
#define E_OK     ((Std_ReturnType)0u)
#define E_NOT_OK ((Std_ReturnType)1u)

typedef uint16 PduIdType;
typedef uint16 PduLengthType;

typedef struct {
    uint8        *SduDataPtr;
    PduLengthType SduLength;
    uint8        *MetaDataPtr;
} PduInfoType;

/* AUTOSAR null-pointer constant (MISRA C:2012 Rule 11.9 compliant). */
#ifndef NULL_PTR
#define NULL_PTR ((void *)0)
#endif

/* AUTOSAR activation switches used in Cfg headers. */
#define STD_ON  (1u)
#define STD_OFF (0u)

#endif
