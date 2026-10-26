/* Campaign rendering uses librac4 tables; NTSC-U multiplayer retains its native bindings. */
#include "module.h"
#include "backend.h"
#include <librac4/graphics.h>
#include <librac4/game.h>
#include <librac4/pad.h>
#include "campaign.h"

#define WORD(address) (*(volatile u32*)(address))
#define PAUSE_CALL (0x5605d4)

typedef void (*rawPadCallback_t)(void*, u8*, int, void*);

typedef struct capturedPad {
	/* 0x00 */ struct PAD* pad;
	/* 0x04 */ rawPadCallback_t original;
	/* 0x08 */ u8 sample[32];
} capturedPad_t;

static capturedPad_t captured[4];
static void (*openPauseMenu)(void);
static int ready;
static u32 currentMode, pauseCall, pauseOriginal, pauseAdd, pauseClose;
static u32 frameSubmit, frameOriginal[2];
static u32 frameTrampoline[4] __attribute__((aligned(16)));
static void (*updateMenu)(void);
static int updating;

static void flush(void)
{
	((void (*)(int))0x126e20)(0);
	((void (*)(int))0x126e20)(2);
}

static void submitFrame(void)
{
	/* Campaign VU packets must be appended before submission. Drawing from
	 * sceGsSyncV can modify a chain which the VU is already processing. */
	if (!updating && updateMenu) {
		updating = 1;
		updateMenu();
		updating = 0;
	}
	((void (*)(void))frameTrampoline)();
}

static void restoreFrame(void)
{
	u32 jump = 0x08000000 | ((u32)submitFrame >> 2);
	if (frameSubmit && WORD(frameSubmit) == jump) {
		WORD(frameSubmit) = frameOriginal[0];
		WORD(frameSubmit + 4) = frameOriginal[1];
		flush();
	}
	frameSubmit = 0;
}

void menuBackendUpdate(u32 mode, void (*update)(void))
{
	u32 address;
	if (!menuBackendReady(mode)) {
		restoreFrame();
		update();
		return;
	}
	if (mode == DL_MODE_MULTIPLAYER) {
		update();
		return;
	}
	address = GetAddressImmediate(&vaFrameSubmit);
	if (frameSubmit && frameSubmit != address)
		restoreFrame();
	updateMenu = update;
	if (!address || frameSubmit == address)
		return;
	/* These two displaced instructions contain no PC-relative operations. */
	if (WORD(address) != 0x27bdffe0 || (WORD(address + 4) & 0xffff0000) != 0x3c020000)
		return;
	frameOriginal[0] = frameTrampoline[0] = WORD(address);
	frameOriginal[1] = frameTrampoline[1] = WORD(address + 4);
	frameTrampoline[2] = 0x08000000 | ((address + 8) >> 2);
	frameTrampoline[3] = 0;
	frameSubmit = address;
	WORD(address) = 0x08000000 | ((u32)submitFrame >> 2);
	WORD(address + 4) = 0;
	flush();
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

static struct PAD* currentPad(int index)
{
	struct PAD* pad;
	if (index >= PAD_PORT_MAX)
		return 0;
	if (currentMode == DL_MODE_CAMPAIGN)
		return padGetPad(index);
	pad = ((struct PAD**)0x21ddd8)[index];
	return pad || index ? pad : (struct PAD*)0x1ee600;
}

static int multiplayerGameCode(void)
{
	/* Same loaded-scene choice as libdl's hasGameCodeSeg(). */
	return WORD(0x21e1ec) || (WORD(0x220268) == 1 && Level != 0);
}

int menuBackendReady(u32 mode)
{
	u32 text, width, box;
	if (currentMode != mode) {
		menuBackendDeactivate();
		currentMode = mode;
	}
	if (mode == DL_MODE_CAMPAIGN) {
		text = GetAddressImmediate(&vaDrawTextFunc);
		width = GetAddressImmediate(&vaGetTextWidthFunc);
		box = GetAddressImmediate(&vaDrawBoxFunc);
		/* Overlay contents must be present before calling its renderer. */
		ready = text && width && box && WORD(text) == 0x27bdff20 &&
		        WORD(width) == 0x27bdff80 && WORD(box) == 0x27bdff90;
	} else {
		ready = DL_MODULE_REGION == 1 && WORD(0x22026c) == 1 &&
		        (Level == 0 || multiplayerGameCode());
	}
	return ready;
}

int menuBackendLevel(void)
{
	if (currentMode == DL_MODE_CAMPAIGN && GetAddressImmediate(&vaPauseCall) == 0)
		return -1; /* Includes Battledome's mission -1 boot-menu sentinel. */
	return currentMode == DL_MODE_MULTIPLAYER && Level == 0 ? 39 : Level;
}

int menuBackendPopupReady(u32 mode)
{
	return menuBackendReady(mode);
}

u32 menuBackendButtons(u32 mode)
{
	struct PAD* pad = currentPad(0);
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
		struct PAD* pad = currentPad(i);
		u8* bytes = (u8*)pad;
		if (!pad)
			continue;
		if (!captured[i].pad) {
			captured[i].pad = pad;
			captured[i].original = (rawPadCallback_t)pad->RawPadInputCallback;
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
	if (currentMode == DL_MODE_CAMPAIGN) {
		gfxScreenSpaceBox(x, y, width, height, color);
	} else {
		float rect[8] __attribute__((aligned(16))) = {
			x, y, x + width, y, x, y + height, x + width, y + height
		};
		u32 data[11] __attribute__((aligned(16))) = {
			8, 0, 0x5c97ac, 0, 0, 0xbe130000, color, color, color, color, 2
		};
		u32 draw = multiplayerGameCode() ? 0x5be610 : 0x699c08;
		((void (*)(const float*, const u32*))draw)(rect, data);
	}
}

void menuBackendText(float x, float y, u32 color, const char* text)
{
	if (currentMode == DL_MODE_CAMPAIGN) {
		gfxScreenSpaceText(x * SCREEN_WIDTH, y * SCREEN_HEIGHT, 1, 1, color, text, -1, 0);
	} else {
		u32 draw = multiplayerGameCode() ? 0x4d8420 : 0x5be058;
		((void (*)(u32, const char*, long, u64, u64, u64, float, float, float, float, float, float))draw)
			(color, text, -1, 0, 1, 0x80000000, x * SCREEN_WIDTH, y * SCREEN_HEIGHT, 1, 1, 1, 1);
	}
}

float menuBackendWidth(const char* text)
{
	u32 width;
	if (currentMode == DL_MODE_CAMPAIGN)
		return (float)gfxGetFontWidth(text, -1, 1) / SCREEN_WIDTH;
	width = multiplayerGameCode() ? 0x4d8130 : 0x5bdc90;
	return (float)((int (*)(const char*, int, float))width)(text, -1, 1) / SCREEN_WIDTH;
}

static void pauseOpen(long menu)
{
	/* Campaign owns a separate pause renderer and captured frame buffers.
	 * Keep that renderer alive until our overlay closes instead of drawing
	 * while its teardown restores the normal gameplay VU chains. */
	if (currentMode == DL_MODE_MULTIPLAYER)
		((void (*)(long))pauseClose)(menu);
	if (openPauseMenu)
		openPauseMenu();
}

static void pauseButton(long menu, u64 label, u64 callback, u8 flags)
{
	(void)label;
	(void)callback;
	((void (*)(long, const char*, void*, u8))pauseAdd)(menu, "MOD MENU", pauseOpen, flags);
}

static void restorePause(void)
{
	u32 jump = 0x0c000000 | ((u32)pauseButton >> 2);
	if (pauseCall && WORD(pauseCall) == jump) {
		WORD(pauseCall) = pauseOriginal;
		flush();
	}
	pauseCall = 0;
}

void menuBackendPause(u32 mode, void (*openMenu)(void))
{
	u32 call, add, close;
	u32 jump = 0x0c000000 | ((u32)pauseButton >> 2);
	openPauseMenu = openMenu;
	if (mode == DL_MODE_CAMPAIGN) {
		call = GetAddressImmediate(&vaPauseCall);
		add = GetAddressImmediate(&vaPauseButton);
		close = GetAddressImmediate(&vaPauseClose);
	} else {
		call = WORD(0x21e1ec) ? PAUSE_CALL : 0;
		add = 0x5600f8;
		close = 0x560e30;
	}
	if (pauseCall && pauseCall != call)
		restorePause();
	if (!call || !add || !close)
		return;
	if (WORD(call) == (0x0c000000 | (add >> 2))) {
		pauseCall = call;
		pauseOriginal = WORD(call);
		pauseAdd = add;
		pauseClose = close;
		WORD(call) = jump;
		flush();
	}
}

void menuBackendDeactivate(void)
{
	menuBackendRelease();
	restorePause();
	restoreFrame();
	updateMenu = 0;
	ready = 0;
	openPauseMenu = 0;
}
