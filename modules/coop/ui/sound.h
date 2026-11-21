#ifndef DL_COOP_UI_SOUND_H
#define DL_COOP_UI_SOUND_H

typedef enum coopUiSound {
	COOP_UI_OPEN = 0x0b,
	COOP_UI_CLOSE = 0x0c,
	COOP_UI_MOVE = 0x10,
	COOP_UI_SELECT = 0x05
} coopUiSound_e;

void coopUiPlaySound(coopUiSound_e sound);

#endif
