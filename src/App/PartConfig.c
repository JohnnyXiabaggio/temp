/* 102A part-configuration loader (stub).
 * On target the values are read from NvM block NVM_BLOCK_102A,
 * which is populated by the UDS server (service 0x2E / DID 102A).
 * The stub returns a sane default that exercises the LIN path. */

#include "PartConfig.h"

static PartConfig Cfg;

Std_ReturnType PartConfig_Load(void)
{
    /* Default: LIN cruise switch + retarder aux brake + AMT.
     * Real implementation calls NvM_ReadBlock(NVM_BLOCK_102A, &Cfg)
     * and verifies the per-block CRC. */
    Cfg = (PartConfig){
        .cruiseSrc            = CRUISE_SRC_LIN,
        .auxBrake             = AUX_BRAKE_RETARDER,
        .txType               = TX_TYPE_AMT,
        .cruiseSwitchPartNo   = 0x4321u,
        .retarderHandlePartNo = 0x8001u,
        .amtConfigCode        = 0x01u
    };
    return E_OK;
}

const PartConfig *PartConfig_Get(void) { return &Cfg; }
