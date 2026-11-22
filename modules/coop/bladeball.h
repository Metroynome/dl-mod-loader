#ifndef DL_COOP_BLADEBALL_H
#define DL_COOP_BLADEBALL_H

#include "module.h"

typedef struct bladeballState {
	/* 0x00 */ float position[3];
	/* 0x0c */ float rotation[3];
	/* 0x18 */ float health;
	/* 0x1c */ float animTime;
	/* 0x20 */ float animSpeed;
	/* 0x24 */ u16 mode;
	/* 0x26 */ s8 state;
	/* 0x27 */ u8 collision;
	/* 0x28 */ u8 animId;
	/* 0x29 */ u8 animFlags;
	/* 0x2a */ u8 removed;
	/* 0x2b */ u8 reserved;
} bladeballState_t;

int bladeballCapture(u32 moby, bladeballState_t* state);
void bladeballPresent(u32 moby, const bladeballState_t* state, int animate);
int bladeballStateValid(const bladeballState_t* state);

#endif
