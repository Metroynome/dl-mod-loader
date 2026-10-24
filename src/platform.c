#include "platform.h"
/* Deadlocked keeps one resident SDK for campaign, co-op and multiplayer.
 * These addresses and signatures match all four retail boot/core1 images.
 * Never use a level-overlay library function here. */
void platformApi(moduleApi_t *api, u32 mode)
{
	api->version = DL_API_VERSION;
	api->region = DL_MODULE_REGION;
	api->mode = mode;
	api->print = (void *)0x0011d5d8;
	api->open = (void *)0x0012b338;
	api->close = (void *)0x0012b5c8;
	api->read = (void *)0x0012b980;
	api->flushCache = (void *)0x00126e20;
}

int platformReady(u32 layout)
{
	(void)layout;
	return *(volatile u32 *)0x11d5d8 == 0x27bdff70 &&
	       *(volatile u32 *)0x12b338 == 0x27bdfeb0 &&
	       *(volatile u32 *)0x12b5c8 == 0x27bdff60 &&
	       *(volatile u32 *)0x12b980 == 0x27bdff20 &&
	       *(volatile u32 *)0x126e20 == 0x24030064;
}

static u32 levelMode(void)
{
	int level;
#if DL_MODULE_REGION == 2
	level = *(volatile int *)0x0021de90;
#elif DL_MODULE_REGION == 3 || DL_MODULE_REGION == 4
	level = *(volatile int *)0x00238610;
#else
	level = *(volatile int *)0x0021de10;
#endif
	/* The initial menu is -1 (0 is also used by librac4's menu check).
	 * Co-op planets use campaign IDs +20. Multiplayer uses +40, and
	 * splitscreen +60. Enumerate known IDs so unknown transitions defer. */
	switch (level) {
    case -1:
	case 1: case 2: case 4: case 5: case 6: case 7: case 8:
	case 10: case 11: case 13: case 14: case 15:
	case 21: case 22: case 24: case 25: case 26: case 27: case 28:
	case 30: case 31: case 33: case 34: case 35:
	    return DL_MODE_CAMPAIGN;
    case 0: case 39:
	case 41: case 42: case 44: case 45: case 46: case 47: case 48:
	case 50: case 51: case 53: case 54:
	case 61: case 62: case 64: case 65: case 66: case 67: case 68:
	case 70: case 71: case 73: case 74:
	    return DL_MODE_MULTIPLAYER;
	default:
	    return 0;
	}
}

/* Both return sites may execute in one mode; only its selected hook dispatches. */
u32 platformMode(u32 hookMode)
{
	u32 mode = levelMode();
	return mode == hookMode ? mode : 0;
}

/* NTSC-U menu identifiers come from libdl/ui.h. Unknown online menus stay
 * online until Local Play is positively identified. */
u32 platformContext(u32 mode)
{
	static u32 previousContext;
	if (mode == DL_MODE_CAMPAIGN)
		return previousContext = DL_CONTEXT_CAMPAIGN;
#if DL_MODULE_REGION == 1
	if (mode == DL_MODE_MULTIPLAYER) {
		u32 menu = *(volatile u32*)0x3434b8;
		u32 active = *(volatile u32*)(0x11c7000 + 0x108);
		int level = *(volatile int*)0x21de10;
		if (*(volatile u32*)0x1af91c)
			return previousContext = DL_CONTEXT_ONLINE;
		if (level > 0)
			return previousContext = DL_CONTEXT_LOCAL;
		if (menu == 0xff || menu == 0x136)
			return previousContext = DL_CONTEXT_LOCAL;
		if (active && active == *(volatile u32*)(0x11c7000 + 0x64 + 0x19 * 4))
			return previousContext = DL_CONTEXT_LOCAL;
		/* Start Game's confirmation dialog replaces the active staging object.
		 * Menu 0 is also used while returning from a local game. Neither
		 * transition is evidence of an online connection. Only retain a
		 * previously confirmed Local Play context for these transient screens. */
		if (previousContext == DL_CONTEXT_LOCAL && (menu == 0x125 || menu == 0))
			return DL_CONTEXT_LOCAL;
		return previousContext = DL_CONTEXT_ONLINE;
	}
#endif
	return 0;
}
