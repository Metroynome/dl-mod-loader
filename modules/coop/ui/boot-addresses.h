#ifndef DL_COOP_BOOT_ADDRESSES_H
#define DL_COOP_BOOT_ADDRESSES_H

#include <librac4/interop.h>

/* Retail boot-only entry points; other overlays intentionally resolve to zero. */
static VariableAddress_t vaBootDialog = {
#if RAC4_PAL
	.MainMenu = 0x49f4c8,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c2450,
#else
	.MainMenu = 0x49e790,
#endif
};

static VariableAddress_t vaBootText = {
#if RAC4_PAL
	.MainMenu = 0x5062d0,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x5295f0,
#else
	.MainMenu = 0x505238,
#endif
};

static VariableAddress_t vaBootStart = {
#if RAC4_PAL
	.MainMenu = 0x4a2e90,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c6048,
#else
	.MainMenu = 0x4a2158,
#endif
};

static VariableAddress_t vaBootGetMode = {
#if RAC4_PAL
	.MainMenu = 0x4a3018,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c61d0,
#else
	.MainMenu = 0x4a22e0,
#endif
};

static VariableAddress_t vaBootClose = {
#if RAC4_PAL
	.MainMenu = 0x4a01b0,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c3138,
#else
	.MainMenu = 0x49f478,
#endif
};

static VariableAddress_t vaBootButtons = {
#if RAC4_PAL
	.MainMenu = 0x4a0d78,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c3f50,
#else
	.MainMenu = 0x4a0040,
#endif
};

static VariableAddress_t vaBootCursor = {
#if RAC4_PAL
	.MainMenu = 0x4a0f28,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c4100,
#else
	.MainMenu = 0x4a01f0,
#endif
};

static VariableAddress_t vaBootCursorGet = {
#if RAC4_PAL
	.MainMenu = 0x4a0f40,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c4118,
#else
	.MainMenu = 0x4a0208,
#endif
};

static VariableAddress_t vaBootTarget = {
#if RAC4_PAL
	.MainMenu = 0x4a0398,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c3320,
#else
	.MainMenu = 0x49f660,
#endif
};

static VariableAddress_t vaBootHeader = {
#if RAC4_PAL
	.MainMenu = 0x4a0f10,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c40e8,
#else
	.MainMenu = 0x4a01d8,
#endif
};

static VariableAddress_t vaBootShow = {
#if RAC4_PAL
	.MainMenu = 0x4a0028,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c2fb0,
#else
	.MainMenu = 0x49f2f0,
#endif
};

static VariableAddress_t vaBootExit = {
#if RAC4_PAL
	.MainMenu = 0x4a3060,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c6218,
#else
	.MainMenu = 0x4a2328,
#endif
};

static VariableAddress_t vaBootFrameCall = {
#if RAC4_PAL
	.MainMenu = 0x438828,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x45c298,
#else
	.MainMenu = 0x438298,
#endif
};

static VariableAddress_t vaBootFrameSubmit = {
#if RAC4_PAL
	.MainMenu = 0x4e8630,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x50ba78,
#else
	.MainMenu = 0x4e7708,
#endif
};

static VariableAddress_t vaBootCanvasGet = {
#if RAC4_PAL
	.MainMenu = 0x506bb8,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x529ed8,
#else
	.MainMenu = 0x505b20,
#endif
};

static VariableAddress_t vaBootCanvasSet = {
#if RAC4_PAL
	.MainMenu = 0x506b70,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x529e90,
#else
	.MainMenu = 0x505ad8,
#endif
};

static VariableAddress_t vaBootNewGame = {
#if RAC4_PAL
	.MainMenu = 0x4cb6b0,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4ee8a8,
#else
	.MainMenu = 0x4ca818,
#endif
};

static VariableAddress_t vaBootLoadDone = {
#if RAC4_PAL
	.MainMenu = 0x4cc218,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4ef448,
#else
	.MainMenu = 0x4cb378,
#endif
};

static VariableAddress_t vaBootNewLevelFlag = {
#if RAC4_PAL
	.MainMenu = 0x21ee3c,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x2395cc,
#else
	.MainMenu = 0x21edbc,
#endif
};

static VariableAddress_t vaBootAbortDialog = {
#if RAC4_PAL
	.MainMenu = 0x49fc68,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c2bf0,
#else
	.MainMenu = 0x49ef30,
#endif
};

static VariableAddress_t vaBootMultiplayer = {
#if RAC4_PAL
	.MainMenu = 0x4a2e00,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c5fd8,
#else
	.MainMenu = 0x4a20c8,
#endif
};

static VariableAddress_t vaBootSingleplayer = {
#if RAC4_PAL
	.MainMenu = 0x4a2d00,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c5ed8,
#else
	.MainMenu = 0x4a1fc8,
#endif
};

static VariableAddress_t vaBootMenuInput = {
#if RAC4_PAL
	.MainMenu = 0x4a0660,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c3820,
#else
	.MainMenu = 0x49f928,
#endif
};

static VariableAddress_t vaBootDialogShown = {
#if RAC4_PAL
	.MainMenu = 0x49fd18,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.MainMenu = 0x4c2ca0,
#else
	.MainMenu = 0x49efe0,
#endif
};

#endif
