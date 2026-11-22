#ifndef DL_COOP_SESSION_ADDRESSES_H
#define DL_COOP_SESSION_ADDRESSES_H

#include <librac4/interop.h>

/* Matched retail Cooperative Station/Catacrom menu entry points. */
static VariableAddress_t vaSessionGetScreen = {
#if RAC4_PAL
	.Battledome = 0x5e05b0,
	.Catacrom = 0x590828,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5f6ba0,
	.Catacrom = 0x5a7298,
#else
	.Battledome = 0x5dba38,
	.Catacrom = 0x58c2f0,
#endif
};

static VariableAddress_t vaSessionGoScreen = {
#if RAC4_PAL
	.Battledome = 0x5e05d8,
	.Catacrom = 0x590850,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5f6bc8,
	.Catacrom = 0x5a72c0,
#else
	.Battledome = 0x5dba60,
	.Catacrom = 0x58c318,
#endif
};

static VariableAddress_t vaSessionCloseMenu = {
#if RAC4_PAL
	.Battledome = 0x5e0788,
	.Catacrom = 0x590a00,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5f6d78,
	.Catacrom = 0x5a7470,
#else
	.Battledome = 0x5dbc10,
	.Catacrom = 0x58c4c8,
#endif
};

static VariableAddress_t vaSessionGetState = {
#if RAC4_PAL
	.Battledome = 0x5dccd8,
	.Catacrom = 0x58cf50,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5f3380,
	.Catacrom = 0x5a3a78,
#else
	.Battledome = 0x5d8228,
	.Catacrom = 0x588ae0,
#endif
};

static VariableAddress_t vaSessionPushState = {
#if RAC4_PAL
	.Battledome = 0x5dd448,
	.Catacrom = 0x58d6c0,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5f3a28,
	.Catacrom = 0x5a4120,
#else
	.Battledome = 0x5d88d0,
	.Catacrom = 0x589188,
#endif
};

static VariableAddress_t vaSessionSetFocus = {
#if RAC4_PAL
	.Battledome = 0x5e6848,
	.Catacrom = 0x596ac0,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5fd088,
	.Catacrom = 0x5ad780,
#else
	.Battledome = 0x5e1cd0,
	.Catacrom = 0x592588,
#endif
};

static VariableAddress_t vaSessionMissionData = {
#if RAC4_PAL
	.Battledome = 0x332730,
	.Catacrom = 0x32f2b0,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x34cf30,
	.Catacrom = 0x349a30,
#else
	.Battledome = 0x332730,
	.Catacrom = 0x32f2b0,
#endif
};

static VariableAddress_t vaSessionPlanetData = {
#if RAC4_PAL
	.Battledome = 0x333398,
	.Catacrom = 0x32ff18,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x34db98,
	.Catacrom = 0x34a698,
#else
	.Battledome = 0x333398,
	.Catacrom = 0x32ff18,
#endif
};

static VariableAddress_t vaSessionMenuData = {
#if RAC4_PAL
	.Battledome = 0x32ef00,
	.Catacrom = 0x32ba80,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x349600,
	.Catacrom = 0x346100,
#else
	.Battledome = 0x32ee00,
	.Catacrom = 0x32b980,
#endif
};

static VariableAddress_t vaSessionUpdateMission = {
#if RAC4_PAL
	.Battledome = 0x6036c8,
	.Catacrom = 0x5b3878,
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x619fa8,
	.Catacrom = 0x5ca5d8,
#else
	.Battledome = 0x5fea88,
	.Catacrom = 0x5af278,
#endif
};

#endif
