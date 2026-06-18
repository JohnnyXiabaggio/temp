/* AUTOSAR Development Error Tracer (DET) — stub header.
 * On target this is replaced by the qualified DET BSW module. */

#ifndef DET_H
#define DET_H

#include "Std_Types.h"

typedef uint16 Det_ModuleIdType;
typedef uint8  Det_InstanceIdType;
typedef uint8  Det_ApiIdType;
typedef uint8  Det_ErrorIdType;

extern Std_ReturnType Det_ReportError(
    Det_ModuleIdType   ModuleId,
    Det_InstanceIdType InstanceId,
    Det_ApiIdType      ApiId,
    Det_ErrorIdType    ErrorId);

#endif
