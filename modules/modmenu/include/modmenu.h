#ifndef DL_MODMENU_H
#define DL_MODMENU_H

#include "module.h"

#define MODMENU_SERVICE_ID        (0x4d454e55)
#define MODMENU_API_VERSION       (1)
#define MODMENU_ACTION            (1)
#define MODMENU_TOGGLE            (2)
#define MODMENU_RANGE             (3)
#define MODMENU_LABEL             (4)
#define MODMENU_SHOW_MAIN_MENU    (1)
#define MODMENU_SHOW_CAMPAIGN     (2)
#define MODMENU_SHOW_MULTIPLAYER  (4)
#define MODMENU_SHOW_EVERYWHERE   (7)
#define MODMENU_MAX_TABS          (8)
#define MODMENU_MAX_ITEMS         (40)
#define MODMENU_CONFIG_MAX_FIELDS (DL_CONFIG_MAX_FIELDS)
#define MODMENU_CONFIG_OK         (DL_CONFIG_OK)
#define MODMENU_CONFIG_MISSING    (DL_CONFIG_MISSING)
#define MODMENU_CONFIG_INVALID    (DL_CONFIG_INVALID)
#define MODMENU_CONFIG_IO         (DL_CONFIG_IO)

/* IDs are opaque positive handles; negative return values indicate failure.
 * Labels are copied. Value/context pointers must remain valid until removeTab.
 * Callbacks run synchronously on the game thread. Never block inside one. */
typedef void (*ModMenuCallback)(void* context);

/* Pointer-bearing offsets describe the EE's 32-bit ABI. */
typedef struct modMenuItem {
	/* 0x00 */ const char* label;
	/* 0x04 */ u32 type;
	/* 0x08 */ int* value;
	/* 0x0c */ int minimum;
	/* 0x10 */ int maximum;
	/* 0x14 */ int step;
	/* 0x18 */ ModMenuCallback callback;
	/* 0x1c */ void* context;
} modMenuItem_t;

/* Compatibility aliases; persistence is owned by the loader. */
typedef moduleConfigField_t modMenuConfigField_t;

typedef const char* (*ModMenuListLabel)(void* context, int index);
typedef void (*ModMenuListSelect)(void* context, int index);
/* Draw once per UI update; return zero to dismiss. Buttons are new presses. */
typedef int (*ModMenuModal)(u32 pressed);

typedef struct modMenuApi {
	/* 0x00 */ u32 version;
	/* 0x04 */ u32 size;
	/* 0x08 */ int (*addTab)(const char* key, const char* label, u32 modes);
	/* 0x0c */ int (*addItem)(int tab, const modMenuItem_t* item);
	/* 0x10 */ int (*removeTab)(int tab);
	/* 0x14 */ int (*setItemEnabled)(int item, int enabled);
	/* 0x18 */ int (*isOpen)(void);
	/* 0x1c */ void (*close)(void);
	/* Appended API-1 fields; size guards callers using this extension. */
	/* 0x20 */ int (*getActiveTab)(void);
	/* 0x24 */ int (*focusTab)(int tab);
	/* Virtual list: entries do not consume item slots. X commits selection. */
	/* 0x28 */ int (*setList)(int tab, int count, ModMenuListLabel label, ModMenuListSelect select, void* context, int initial);
	/* Optional information for the highlighted row, displayed on the left. */
	/* 0x2c */ int (*setListInfo)(int tab, ModMenuListLabel info);
	/* Contexts are independent of module execution modes; combine with |. */
	/* 0x30 */ int (*setTabVisibility)(int tab, u32 contexts);
	/* API-1 extension: execution contexts ANDed with the existing screen
     * visibility filter. Thus LOCAL + MAIN_MENU means Local Play menus only. */
	/* 0x34 */ int (*setTabContexts)(int tab, u32 contexts);
	/* host0:dl/config/<mod>.<region>.ini. Explicit calls, never per frame.
     * Save replaces the file; comments/unknown keys are not preserved. */
	/* 0x38 */ int (*loadConfig)(const char* mod, const modMenuConfigField_t* fields, int count);
	/* 0x3c */ int (*saveConfig)(const char* mod, const modMenuConfigField_t* fields, int count);
	/* Modal progress overlay. total<=0 shows indeterminate discovery;
     * label=NULL dismisses it. Caller must dismiss on deactivation. */
	/* 0x40 */ void (*setProgress)(const char* label, int current, int total);
	/* Optional module-owned popup. Returns zero if another popup owns it.
	 * Owner must clear it with NULL on deactivation; content is not copied. */
	/* 0x44 */ int (*setModal)(ModMenuModal draw);
	/* 0x48 */ void (*drawBox)(float x, float y, float width, float height, u32 color);
	/* 0x4c */ void (*drawText)(float x, float y, u32 color, const char* text);
	/* Focused-tab Square action. Label is copied; NULL callback clears it.
	 * Appended API-1 extension; EE function-pointer offset is 0x50. */
	/* 0x50 */ int (*setTabAction)(int tab, const char* label, ModMenuCallback callback, void* context);
	/* EE offset 0x54: named choices for a range, wrapping at either end.
	 * Uses the item's context and current value; NULL restores a numeric range. */
	/* 0x54 */ int (*setItemValueLabel)(int item, ModMenuListLabel label);
} modMenuApi_t;

/* Look this up again after mode changes; absence is normal, not fatal.
 * Retry registration from moduleUpdate until the provider is available. */
static inline const modMenuApi_t* modmenuFind(const moduleApi_t* loader)
{
	const modMenuApi_t* menu;
	if (!loader || loader->version < 2 || !loader->findService)
		return 0;
	menu = (const modMenuApi_t*)loader->findService(MODMENU_SERVICE_ID, MODMENU_API_VERSION);
	return menu && menu->version == MODMENU_API_VERSION && menu->size >= sizeof(*menu) ? menu : 0;
}
#endif
