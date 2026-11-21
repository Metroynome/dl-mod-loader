#ifndef DL_COOP_H
#define DL_COOP_H

#include "module.h"

#define COOP_MAGIC (0x504f4f43)
#define COOP_INPUT_CHANNEL (0x444c4350)
#define COOP_SAMPLE_BYTES (20)

typedef enum coopState {
	COOP_OFF,
	COOP_WAITING,
	COOP_UNSUPPORTED,
	COOP_ACTIVE
} coopState_e;

/* Experimental NTSC-U Station input adapter. Only enabled is user-editable. */
typedef struct coopControl {
	/* 0x00 */ u32 magic;
	/* 0x04 */ u32 size;
	/* 0x08 */ int enabled;
	/* 0x0c */ u32 state;
	/* 0x10 */ u32 sent;
	/* 0x14 */ u32 received;
	/* 0x18 */ u32 stale;
	/* 0x1c */ u8 sourceSample[COOP_SAMPLE_BYTES];
	/* 0x30 */ u8 remoteSample[COOP_SAMPLE_BYTES];
} coopControl_t;

#endif
