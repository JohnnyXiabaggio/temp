/* Thin dispatcher around the qualified E2E Library. The library is
 * developed to ASIL-D and is the *independent* safety mechanism that
 * lifts the ASIL-B(D) PduR back to ASIL-D end-to-end.
 *
 * The CRC implementations below are the standard polynomials called
 * for by the AUTOSAR E2E specification. They are reproduced here
 * because the routing project is being shown self-contained; in a
 * real ECU the library functions (E2E_P02Protect, E2E_P05Check, ...)
 * are linked in.
 */

#include "E2E_Wrapper.h"
#include "Dem.h"

#define E2E_DEM_THRESHOLD 5u   /* k consecutive failures -> Dem event */

static uint8 Crc8H2F(const uint8 *d, uint16 len)
{
    uint8 c = 0xFFu;
    for (uint16 i = 0u; i < len; i++) {
        c ^= d[i];
        for (uint8 b = 0u; b < 8u; b++) {
            c = (c & 0x80u) ? (uint8)((c << 1) ^ 0x2Fu) : (uint8)(c << 1);
        }
    }
    return c ^ 0xFFu;
}

static uint16 Crc16Ccitt(const uint8 *d, uint16 len)
{
    uint16 c = 0xFFFFu;
    for (uint16 i = 0u; i < len; i++) {
        c ^= ((uint16)d[i]) << 8;
        for (uint8 b = 0u; b < 8u; b++) {
            c = (c & 0x8000u) ? (uint16)((c << 1) ^ 0x1021u) : (uint16)(c << 1);
        }
    }
    return c;
}

/* -- Profile P02 ---------------------------------------------------- */
/* Layout:  byte0 = CRC8 ; byte1 low nibble = counter (mod 16)         */

static Std_ReturnType P02_Protect(E2E_RuntimeStateType *st, uint16 dataId,
                                  uint8 *sdu, uint16 len)
{
    if (len < 2u) { return E_NOT_OK; }
    sdu[1] = (sdu[1] & 0xF0u) | (st->counter & 0x0Fu);
    /* CRC computed over [dataId, sdu[1..len-1]] */
    uint8 buf[64];
    if (len > sizeof(buf)) { return E_NOT_OK; }
    buf[0] = (uint8)(dataId >> 8);
    buf[1] = (uint8) dataId;
    for (uint16 i = 0u; i < (len - 1u); i++) { buf[2u + i] = sdu[1u + i]; }
    sdu[0] = Crc8H2F(buf, (uint16)(2u + (len - 1u)));
    st->counter = (uint8)((st->counter + 1u) & 0x0Fu);
    return E_OK;
}

static Std_ReturnType P02_Check(E2E_RuntimeStateType *st, uint16 dataId,
                                const uint8 *sdu, uint16 len)
{
    if (len < 2u) { return E_NOT_OK; }
    uint8 buf[64];
    if (len > sizeof(buf)) { return E_NOT_OK; }
    buf[0] = (uint8)(dataId >> 8);
    buf[1] = (uint8) dataId;
    for (uint16 i = 0u; i < (len - 1u); i++) { buf[2u + i] = sdu[1u + i]; }
    const uint8 calc = Crc8H2F(buf, (uint16)(2u + (len - 1u)));
    if (calc != sdu[0]) {
        return E_NOT_OK;
    }
    const uint8 rxCnt   = sdu[1] & 0x0Fu;
    const uint8 expCnt  = (uint8)((st->counter + 1u) & 0x0Fu);
    const uint8 deltaOk = (uint8)((rxCnt - expCnt) & 0x0Fu);
    if (deltaOk > 1u) {                 /* allow at most 1 lost frame  */
        return E_NOT_OK;
    }
    st->counter = rxCnt;
    return E_OK;
}

/* -- Profile P05 ---------------------------------------------------- */

static Std_ReturnType P05_Protect(E2E_RuntimeStateType *st, uint16 dataId,
                                  uint8 *sdu, uint16 len)
{
    if (len < 4u) { return E_NOT_OK; }
    sdu[2] = st->counter;
    sdu[0] = 0u; sdu[1] = 0u;
    /* CRC over [sdu[2..], dataId LE] */
    uint8 buf[256];
    if (len > sizeof(buf)) { return E_NOT_OK; }
    uint16 n = 0u;
    for (uint16 i = 2u; i < len; i++) { buf[n++] = sdu[i]; }
    buf[n++] = (uint8) dataId;
    buf[n++] = (uint8)(dataId >> 8);
    const uint16 c = Crc16Ccitt(buf, n);
    sdu[0] = (uint8) c;
    sdu[1] = (uint8)(c >> 8);
    st->counter = (uint8)(st->counter + 1u);
    return E_OK;
}

static Std_ReturnType P05_Check(E2E_RuntimeStateType *st, uint16 dataId,
                                const uint8 *sdu, uint16 len)
{
    if (len < 4u) { return E_NOT_OK; }
    uint8 buf[256];
    if (len > sizeof(buf)) { return E_NOT_OK; }
    uint16 n = 0u;
    for (uint16 i = 2u; i < len; i++) { buf[n++] = sdu[i]; }
    buf[n++] = (uint8) dataId;
    buf[n++] = (uint8)(dataId >> 8);
    const uint16 calc = Crc16Ccitt(buf, n);
    const uint16 rx   = (uint16)sdu[0] | ((uint16)sdu[1] << 8);
    if (calc != rx) {
        return E_NOT_OK;
    }
    /* counter monotonicity (allow up to 3 lost) */
    const uint8 rxCnt   = sdu[2];
    const uint8 expCnt  = (uint8)(st->counter + 1u);
    const uint8 deltaOk = (uint8)(rxCnt - expCnt);
    if (deltaOk > 3u) {
        return E_NOT_OK;
    }
    st->counter = rxCnt;
    return E_OK;
}

/* -- Public dispatch ------------------------------------------------ */

Std_ReturnType E2E_Protect(PduR_E2EProfileType p, E2E_RuntimeStateType *st,
                           uint16 dataId, uint8 *sdu, uint16 len)
{
    if (st == NULL) { return E_NOT_OK; }
    switch (p) {
        case PDUR_E2E_PROFILE_01:
        case PDUR_E2E_PROFILE_02: return P02_Protect(st, dataId, sdu, len);
        case PDUR_E2E_PROFILE_05:
        case PDUR_E2E_PROFILE_06: return P05_Protect(st, dataId, sdu, len);
        case PDUR_E2E_NONE:       return E_OK;
        default:                  return E_NOT_OK;
    }
}

Std_ReturnType E2E_Check(PduR_E2EProfileType p, E2E_RuntimeStateType *st,
                         uint16 dataId, const uint8 *sdu, uint16 len)
{
    if (st == NULL) { return E_NOT_OK; }

    Std_ReturnType r;
    switch (p) {
        case PDUR_E2E_PROFILE_01:
        case PDUR_E2E_PROFILE_02: r = P02_Check(st, dataId, sdu, len); break;
        case PDUR_E2E_PROFILE_05:
        case PDUR_E2E_PROFILE_06: r = P05_Check(st, dataId, sdu, len); break;
        case PDUR_E2E_NONE:       return E_OK;
        default:                  return E_NOT_OK;
    }

    if (r == E_OK) {
        st->errorCount = 0u;
        if (st->okCount < 0xFFu) { st->okCount++; }
    } else {
        st->okCount = 0u;
        if (st->errorCount < 0xFFu) { st->errorCount++; }
        if (st->errorCount >= E2E_DEM_THRESHOLD) {
            Dem_ReportErrorStatus(DEM_EVT_E2E_CHECK_FAIL, DEM_EVENT_STATUS_FAILED);
        }
    }
    return r;
}
