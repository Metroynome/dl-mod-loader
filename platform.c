#include "platform.h"
/* Deadlocked keeps one resident SDK for campaign, co-op and multiplayer.
 * These addresses and signatures match all four retail boot/core1 images.
 * Never use a level-overlay library function here. */
void platformApi(ModuleApi *api, unsigned int mode)
{
    api->version = DL_MODULE_VERSION;
    api->region = DL_MODULE_REGION;
    api->mode = mode;
    api->print = (void *)0x0011d5d8;
    api->open = (void *)0x0012b338;
    api->close = (void *)0x0012b5c8;
    api->read = (void *)0x0012b980;
    api->flushCache = (void *)0x00126e20;
}

int platformReady(unsigned int layout)
{
    (void)layout;
    return *(volatile unsigned int *)0x11d5d8 == 0x27bdff70 &&
           *(volatile unsigned int *)0x12b338 == 0x27bdfeb0 &&
           *(volatile unsigned int *)0x12b5c8 == 0x27bdff60 &&
           *(volatile unsigned int *)0x12b980 == 0x27bdff20 &&
           *(volatile unsigned int *)0x126e20 == 0x24030064;
}

static unsigned int levelMode(void)
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
    case -1: case 0:
    case 1: case 2: case 4: case 5: case 6: case 7: case 8:
    case 10: case 11: case 13: case 14: case 15:
    case 21: case 22: case 24: case 25: case 26: case 27: case 28:
    case 30: case 31: case 33: case 34: case 35:
        return DL_MODE_CAMPAIGN;
    case 39:
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
unsigned int platformMode(unsigned int hookMode)
{
    unsigned int mode = levelMode();
    return mode == hookMode ? mode : 0;
}
