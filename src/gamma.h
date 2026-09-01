/* Gamma-fade API shim. The original faded the CRT via the video driver's
   gamma table; here it dims the presented frame. Original file preserved at
   the repository root. */

#ifndef GAMMA_H
#define GAMMA_H

#include "compat/mac_types.h"

Boolean IsGammaAvailable(void);
OSErr SetupGammaTools(void);
OSErr DisposeGammaTools(void);
OSErr DoGammaFade(short percent);

#endif
