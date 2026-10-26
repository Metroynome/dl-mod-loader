#ifndef DL_MODULE_H
#define DL_MODULE_H

#include "types.h"

#define DL_MODULE_MAGIC             (0x4d414c44)
#define DL_MODULE_VERSION           (4)
#define DL_MODULE_NO_ENTRY          (0xffffffff)
#define DL_MODE_CAMPAIGN            (1)
#define DL_MODE_MULTIPLAYER         (2)
#define DL_MODE_BOTH                (3)
#define DL_API_VERSION              (4)
#define DL_CONTEXT_CAMPAIGN         (1)
#define DL_CONTEXT_LOCAL            (2)
#define DL_CONTEXT_ONLINE           (4)
#define DL_CONTEXT_ALL              (7)
#define DL_LOADER_DISCOVERY_ADDRESS (0x000a0000)
#define DL_LOADER_DISCOVERY_MAGIC   (0x444c4c44)
#define DL_MODULE_DISCOVERY_MAGIC   (0x444d4c44)
#define DL_DISCOVERY_VERSION        (1)
/* Global exports are retained by KEEP(.module.exports) in src/module.ld. */
#define DL_MODULE_EXPORTS           __attribute__((section(".module.exports")))
#define DL_MODULE_TABLE_MAGIC       (0x444d5442)
#define DL_MODULE_LIMIT             (8)

/* Read only. Loaded does not imply active in the current execution context.
 * Entries remain valid until restart; do not call unknown code via address. */
#define DL_LOADER_DISCOVERY ((const volatile loaderDiscovery_t*)DL_LOADER_DISCOVERY_ADDRESS)
#define DL_MODULE_TABLE     ((const volatile moduleTable_t*)DL_LOADER_DISCOVERY->moduleTable)
/* Flat INI integer bindings: booleans use 0..1, choices use their index range.
 * Missing keys preserve defaults. Loading is transactional on parse failure. */
#define DL_CONFIG_MAX_FIELDS      (32)
#define DL_CONFIG_OK              (1)
#define DL_CONFIG_MISSING         (0)
#define DL_CONFIG_INVALID         (-1)
#define DL_CONFIG_IO              (-2)
#define DL_WARNING_EXTRA_RAM_BIOS (1)

typedef void (*ModuleLifecycle)(void);

/* REGION: 1=NTSC-U, 2=PAL, 3=NTSC-J, 4=NTSC-K.
 * Pointer-bearing offsets below describe the EE's 32-bit ABI, not host tools. */
typedef struct moduleHeader {
	/* 0x00 */ u32 magic;
	/* 0x04 */ u32 version;
	/* 0x08 */ u32 region;
	/* 0x0c */ u32 address;
	/* 0x10 */ u32 imageSize;
	/* 0x14 */ u32 memorySize;
	/* 0x18 */ u32 initOffset;
	/* 0x1c */ u32 updateOffset;
	/* 0x20 */ u32 modes;
	/* 0x24 */ u32 relocationCount;
} moduleHeader_t;

/* Version 4 images are linked at a common base, then moved by the loader.
 * Relocations follow imageSize bytes; BSS and relocation records are not code. */
typedef struct moduleRelocation {
	/* 0x00 */ u32 offset;
	/* 0x04 */ u32 type;
	/* 0x08 */ u32 value;
} moduleRelocation_t;

typedef struct moduleInfo {
	/* 0x00 */ char name[112]; /* Canonical filename, e.g. forge.ntscu.bin. */
	/* 0x70 */ u32 address;
	/* 0x74 */ u32 size;
	/* 0x78 */ u32 modes;
} moduleInfo_t;

typedef struct moduleTable {
	/* 0x00 */ u32 magic;
	/* 0x04 */ u32 version;
	/* 0x08 */ u32 count;
	/* 0x0c */ u32 entrySize;
	/* 0x10 */ moduleInfo_t entries[DL_MODULE_LIMIT];
} moduleTable_t;

/* Fixed EE discovery layouts. Pointer fields are 32-bit EE addresses. */
typedef struct loaderDiscovery {
	/* 0x00 */ u32 moduleTable;
	/* 0x04 */ u32 magic;
	/* 0x08 */ u32 version;
	/* 0x0c */ u32 size;
	/* 0x10 */ u32 reserved[8];
} loaderDiscovery_t;

/* At moduleInfo_t.address. Zero exports means the module has no public table. */
typedef struct moduleDiscovery {
	/* 0x00 */ u32 exports;
	/* 0x04 */ u32 magic;
	/* 0x08 */ u32 version;
	/* 0x0c */ u32 size;
} moduleDiscovery_t;

typedef struct moduleConfigField {
	/* 0x00 */ const char* key;
	/* 0x04 */ int* value;
	/* 0x08 */ int minimum;
	/* 0x0c */ int maximum;
} moduleConfigField_t;

typedef struct moduleApi {
	/* 0x00 */ u32 version;
	/* 0x04 */ u32 region;
	/* 0x08 */ u32 mode;
	/* 0x0c */ int (*print)(const char* format, ...);
	/* 0x10 */ int (*open)(const char* path, int flags, u16 mode);
	/* 0x14 */ int (*read)(int fd, void* buffer, int size);
	/* 0x18 */ int (*close)(int fd);
	/* 0x1c */ void (*flushCache)(int operation);
	/* Services are discovered by ID and exact API version, not linked address.
     * publishService is valid only inside a module callback. The loader owns
     * the registry; pointers remain valid until emulation restarts. */
	/* 0x20 */ int (*publishService)(u32 id, u32 version, const void* service);
	/* 0x24 */ const void* (*findService)(u32 id, u32 version);

	/* API 3: execution context, independent of the campaign/MP library mode.
     * Registration is valid only inside this module's init/update callbacks.
     * The configuration is an upper bound; restrictContexts cannot expand it.
     * Deactivate releases all hooks/input/UI; activate runs on every re-entry. */
	/* 0x28 */ u32 size;
	/* 0x2c */ u32 context;
	/* 0x30 */ int (*setLifecycle)(ModuleLifecycle activate, ModuleLifecycle deactivate);
	/* 0x34 */ int (*restrictContexts)(u32 contexts);
	/* API 4: optional persistence, independent of Mod Menu.
     * host0:dl/config/<mod>.<region>.ini; explicit synchronous operations. */
	/* 0x38 */ int (*loadConfig)(const char* mod, const moduleConfigField_t* fields, int count);
	/* 0x3c */ int (*saveConfig)(const char* mod, const moduleConfigField_t* fields, int count);
	/* Optional trailing field: check size before reading on older loaders. */
	/* 0x40 */ u32 warnings;
} moduleApi_t;

/* Retain the pointer, not a copy: mode and function pointers change on a
 * campaign/multiplayer transition. init runs once, update runs per sync hook
 * (not necessarily once per video frame). Never retain game-object pointers
 * across level changes. */
typedef int (*ModuleInit)(const moduleApi_t* api);
typedef void (*ModuleUpdate)(void);
#endif
