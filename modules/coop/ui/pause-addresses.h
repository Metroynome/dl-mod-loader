#ifndef DL_COOP_PAUSE_ADDRESSES_H
#define DL_COOP_PAUSE_ADDRESSES_H

#include <librac4/interop.h>

/* Native Cooperative Station/Catacrom menu functions and verified call sites. */
static VariableAddress_t vaCoopPauseButtonsCall = {
#if RAC4_PAL
	.Battledome = 0x5fc5d0,
	.Catacrom = 0x5ac780,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x612da8,
	.Catacrom = 0x5c33d8,
#else
	.Battledome = 0x5f78a0,
	.Catacrom = 0x5a8090,
#endif
};

static VariableAddress_t vaCoopPauseLoad = {
#if RAC4_PAL
	.Battledome = 0x5fd7c0,
	.Catacrom = 0x5ad970,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x613f98,
	.Catacrom = 0x5c45c8,
#else
	.Battledome = 0x5f8a90,
	.Catacrom = 0x5a9280,
#endif
};

static VariableAddress_t vaCoopPauseQuit = {
#if RAC4_PAL
	.Battledome = 0x5fd860,
	.Catacrom = 0x5ada10,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x614038,
	.Catacrom = 0x5c4668,
#else
	.Battledome = 0x5f8b30,
	.Catacrom = 0x5a9320,
#endif
};

static VariableAddress_t vaCoopPauseQuitConfirm = {
#if RAC4_PAL
	.Battledome = 0x5fd818,
	.Catacrom = 0x5ad9c8,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x613ff0,
	.Catacrom = 0x5c4620,
#else
	.Battledome = 0x5f8ae8,
	.Catacrom = 0x5a92d8,
#endif
};

static VariableAddress_t vaCoopPauseDialog = {
#if RAC4_PAL
	.Battledome = 0x5e3c20,
	.Catacrom = 0x593e98,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5fa210,
	.Catacrom = 0x5aa908,
#else
	.Battledome = 0x5df0a8,
	.Catacrom = 0x58f960,
#endif
};

static VariableAddress_t vaCoopPauseButtons = {
#if RAC4_PAL
	.Battledome = 0x5e54d0,
	.Catacrom = 0x595748,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5fbd10,
	.Catacrom = 0x5ac408,
#else
	.Battledome = 0x5e0958,
	.Catacrom = 0x591210,
#endif
};

static VariableAddress_t vaCoopPauseCursor = {
#if RAC4_PAL
	.Battledome = 0x5e5680,
	.Catacrom = 0x5958f8,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5fbec0,
	.Catacrom = 0x5ac5b8,
#else
	.Battledome = 0x5e0b08,
	.Catacrom = 0x5913c0,
#endif
};

static VariableAddress_t vaCoopPauseCursorGet = {
#if RAC4_PAL
	.Battledome = 0x5e5698,
	.Catacrom = 0x595910,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5fbed8,
	.Catacrom = 0x5ac5d0,
#else
	.Battledome = 0x5e0b20,
	.Catacrom = 0x5913d8,
#endif
};

static VariableAddress_t vaCoopPauseTitle = {
#if RAC4_PAL
	.Battledome = 0x5e5628,
	.Catacrom = 0x5958a0,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5fbe68,
	.Catacrom = 0x5ac560,
#else
	.Battledome = 0x5e0ab0,
	.Catacrom = 0x591368,
#endif
};

static VariableAddress_t vaCoopPauseClose = {
#if RAC4_PAL
	.Battledome = 0x5e4908,
	.Catacrom = 0x594b80,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5faef8,
	.Catacrom = 0x5ab5f0,
#else
	.Battledome = 0x5dfd90,
	.Catacrom = 0x590648,
#endif
};

static VariableAddress_t vaCoopPauseShow = {
#if RAC4_PAL
	.Battledome = 0x5e4780,
	.Catacrom = 0x5949f8,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5fad70,
	.Catacrom = 0x5ab468,
#else
	.Battledome = 0x5dfc08,
	.Catacrom = 0x5904c0,
#endif
};

#endif
