#ifndef DL_COOP_UI_SOUND_H
#define DL_COOP_UI_SOUND_H

#include <librac4/sound.h>

typedef enum coopUiSound {
	COOP_UI_OPEN = SOUND_KEYBOARD_OPEN,
	COOP_UI_CLOSE = SOUND_KEYBOARD_CLOSE,
	COOP_UI_MOVE = SOUND_KEYBOARD_MOVE,
	COOP_UI_SELECT = SOUND_KEYBOARD_INSERT,
	COOP_UI_DELETE = SOUND_KEYBOARD_DELETE
} coopUiSound_e;

void coopUiPlaySound(coopUiSound_e sound);

#endif
