/* NTSC-U multiplayer rendering and input, using libdl's verified interfaces. */
#include "module.h"
#include "backend.h"
#include <libdl/graphics.h>
#include <libdl/game.h>
#include <libdl/pad.h>

#define WORD(address) (*(volatile u32*)(address))
#define PAUSE_CALL (0x5605d4)
#define PAUSE_ORIGINAL (0x0c15803e)

typedef void (*rawPadCallback_t)(void*, u8*, int, void*);

typedef struct capturedPad {
	/* 0x00 */ struct PAD* pad;
	/* 0x04 */ rawPadCallback_t original;
	/* 0x08 */ u8 sample[32];
} capturedPad_t;

static capturedPad_t captured[4];
static void (*openPauseMenu)(void);
static int ready, pauseOwned;

/* libdl implements this helper but does not declare it in pad.h. */
extern struct PAD* padGetPad(int localPlayerIdx);

static void flush(void)
{
	((void (*)(int))0x126e20)(0);
	((void (*)(int))0x126e20)(2);
}

static void captureRaw(void* controller, u8* sample, int size, void* context)
{
	int i, j;
	for (i = 0; i < 4; ++i) {
		capturedPad_t* slot = &captured[i];
		if (slot->pad != controller)
			continue;
		for (j = 0; j < 32; ++j)
			slot->sample[j] = j < size ? sample[j] : 0;
		for (j = 2; j < size; ++j)
			sample[j] = j < 4 ? 255 : j < 8 ? 127 : 0;
		if (slot->original)
			slot->original(controller, sample, size, context);
		return;
	}
}

int menuBackendReady(u32 mode)
{
	ready = mode == DL_MODE_MULTIPLAYER && WORD(0x22026c) == 1 &&
	        (hasGameCodeSeg() || hasMenuCodeSeg());
	return ready;
}

int menuBackendLevel(void)
{
	/* Menu visibility uses the common API's main-menu sentinel. */
	return WORD(0x21de10) == 0 ? 39 : WORD(0x21de10);
}

int menuBackendPopupReady(u32 mode)
{
	return menuBackendReady(mode);
}

u32 menuBackendButtons(u32 mode)
{
	struct PAD* pad = padGetPad(0);
	const u8* sample;
	(void)mode;
	if (!ready || !pad)
		return 0;
	sample = captured[0].pad == pad ? captured[0].sample : pad->rdata;
	return (~(sample[2] | (sample[3] << 8))) & 0xffff;
}

void menuBackendCapture(u32 mode)
{
	int i, j;
	(void)mode;
	for (i = 0; i < 4; ++i) {
		struct PAD* pad = padGetPad(i);
		u8* bytes = (u8*)pad;
		if (!pad)
			continue;
		if (!captured[i].pad) {
			captured[i].pad = pad;
			captured[i].original = pad->RawPadInputCallback;
			for (j = 0; j < 32; ++j)
				captured[i].sample[j] = pad->rdata[j];
			pad->RawPadInputCallback = captureRaw;
		}
		for (j = 0x100; j < 0x180; j += 4)
			*(u32*)(bytes + j) = 0;
		for (j = 0x1a0; j < 0x1cc; j += 4)
			*(u32*)(bytes + j) = 0;
		for (j = 0x1e0; j < 0x34c; j += 4)
			*(u32*)(bytes + j) = 0;
		for (j = 0x354; j < 0x54c; j += 4)
			*(u32*)(bytes + j) = 0;
		for (j = 2; j < 32; ++j)
			pad->rdata[j] = j < 4 ? 255 : j < 8 ? 127 : 0;
	}
}

void menuBackendRelease(void)
{
	int i;
	for (i = 0; i < 4; ++i) {
		capturedPad_t* slot = &captured[i];
		if (slot->pad && slot->pad->RawPadInputCallback == captureRaw)
			slot->pad->RawPadInputCallback = slot->original;
		slot->pad = 0;
		slot->original = 0;
	}
}

void menuBackendBox(float x, float y, float width, float height, u32 color)
{
	gfxScreenSpaceBox(x, y, width, height, color);
}

void menuBackendText(float x, float y, u32 color, const char* text)
{
	gfxScreenSpaceText(x * SCREEN_WIDTH, y * SCREEN_HEIGHT, 1, 1, color, text, -1, 0);
}

float menuBackendWidth(const char* text)
{
	return (float)gfxGetFontWidth(text, -1, 1) / SCREEN_WIDTH;
}

static void pauseOpen(long menu)
{
	((void (*)(long))0x560e30)(menu);
	if (openPauseMenu)
		openPauseMenu();
}

static void pauseButton(long menu, u64 label, u64 callback, u8 flags)
{
	(void)label;
	(void)callback;
	((void (*)(long, const char*, void*, u8))0x5600f8)(menu, "MOD MENU", pauseOpen, flags);
}

void menuBackendPause(u32 mode, void (*openMenu)(void))
{
	u32 jump = 0x0c000000 | ((u32)pauseButton >> 2);
	openPauseMenu = openMenu;
	if (mode == DL_MODE_MULTIPLAYER && isInGame() && WORD(PAUSE_CALL) == PAUSE_ORIGINAL) {
		WORD(PAUSE_CALL) = jump;
		pauseOwned = 1;
		flush();
	}
}

void menuBackendDeactivate(void)
{
	u32 jump = 0x0c000000 | ((u32)pauseButton >> 2);
	menuBackendRelease();
	if (pauseOwned && WORD(PAUSE_CALL) == jump) {
		WORD(PAUSE_CALL) = PAUSE_ORIGINAL;
		flush();
	}
	pauseOwned = ready = 0;
	openPauseMenu = 0;
}
