#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

namespace
{
void yieldMovieWorker(uint8_t *, R5900Context *context, PS2Runtime *runtime)
{
    // The PS2 player spins around RotateThreadReadyQueue(1) while waiting
    // for its producer or decoder. Yield until vblank so those tasks can run.
    const uint32_t returnAddress = GPR_U32(context, 31);
    SET_GPR_S32(context, 2, 0);
    runtime->eeWaitVSyncTicks(1, returnAddress);
}
}

bool installMovieCompatibility(PS2Runtime &runtime)
{
    return runtime.replaceFunction(0x00176468u, yieldMovieWorker);
}
