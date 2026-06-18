#ifndef COMPILER_H
#define COMPILER_H

#define FUNC(rettype, memclass)              rettype
#define P2VAR(ptrtype, memclass, ptrclass)   ptrtype *
#define P2CONST(ptrtype, memclass, ptrclass) const ptrtype *
#define CONST(type, memclass)                const type
#define VAR(type, memclass)                  type
#define CONSTP2CONST(t, mc, pc)              const t * const

#define LOCAL_INLINE static inline

/* AUTOSAR memory-class placeholders. On a target build these are
 * remapped via MemMap.h to assign sections; on the host build they
 * resolve to nothing. They are deliberately defined here (and not
 * required to exist before FUNC(...) usage) so that headers compile
 * standalone. */
#define PDUR_CODE
#define DEM_CODE
#define E2E_CODE
#define SAFETY_CODE
#define SECOC_CODE

/* BodyRouting memory classes. */
#define BODYROUTING_CODE
#define BODYROUTING_VAR
#define BODYROUTING_CONST
#define BODYROUTING_APPL_DATA

/* Pointer memory-class keyword used as second argument to P2VAR /
 * P2CONST when the pointer itself has no special placement. */
#define AUTOMATIC

#endif
