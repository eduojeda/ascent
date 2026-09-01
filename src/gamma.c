#include "gamma.h"
#include "mySAT.h"

Boolean IsGammaAvailable(void) { return true; }
OSErr SetupGammaTools(void) { return noErr; }
OSErr DisposeGammaTools(void) { return noErr; }

OSErr DoGammaFade(short percent)
{
	SATSetGammaLevel(percent);
	SATPresent();
	SDL_Delay(4);
	return noErr;
}
