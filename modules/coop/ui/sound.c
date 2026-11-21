#include "sound.h"
#include <librac4/sound.h>

/*
 * NAME : coopUiPlaySound
 * DESCRIPTION : Play the keypad's native menu sound.
 * NOTES : Independent of Mod Menu.
 * ARGS : UI sound event.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopUiPlaySound(coopUiSound_e sound)
{
	soundPlayMenu(sound);
}
