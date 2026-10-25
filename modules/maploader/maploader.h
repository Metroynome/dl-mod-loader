#ifndef DL_HOST_MAPLOADER_H
#define DL_HOST_MAPLOADER_H

#include "module.h"
#include "modules/forge/include/forge.h"

#define MAP_LIMIT (96)
#define MAP_RETAIL_COUNT (11)
#define MAP_DIRECTORY_PENDING (-999)

typedef enum mapDirectoryCommand {
	MAP_DIRECTORY_OPEN = 1,
	MAP_DIRECTORY_NEXT = 2,
	MAP_DIRECTORY_CLOSE = 3
} mapDirectoryCommand_e;

/* EE layout: name ends at 0xa1; u32 alignment adds three padding bytes. */
typedef struct mapRecord {
	/* 0x00 */ char filename[FORGE_FILENAME_SIZE];
	/* 0x81 */ char name[32];
	/* 0xa4 */ u32 version;
	/* 0xa8 */ u32 base;
	/* 0xac */ u32 wad;
	/* 0xb0 */ u32 world;
	/* 0xb4 */ u32 sound;
	/* 0xb8 */ u32 radar;
} mapRecord_t;

typedef struct mapRetail {
	/* 0x00 */ u32 base;
	/* 0x04 */ u32 background;
} mapRetail_t;

typedef char MapRecordSize[(sizeof(mapRecord_t) == 188) ? 1 : -1];

extern const moduleApi_t* mapApi;
extern mapRecord_t* mapRecords;
extern int mapCount, mapSelection;
extern int mapCatalogReady;
extern u32 mapCatalogRevision;
extern int mapAllowOnline;
extern int mapHopResult;
extern const mapRetail_t mapRetailMaps[MAP_RETAIL_COUNT];

void mapCopyRecord(mapRecord_t* to, const mapRecord_t* from);
int mapReadInfo(const mapRecord_t* map, char* author, char* description);
int mapReadExact(int fd, void* dest, u32 size);
int mapOpenPart(const mapRecord_t* map, const char* extension);
int mapReadPart(const mapRecord_t* map, const char* extension, void* dest, u32 size);
void* memset(void* dest, int value, u32 size);
void* mapAlloc(u32 size);
void mapFree(void* p);
int mapDirectoryIdle(void);
void mapDirectoryReset(void);
int mapDirectoryReady(void);
int mapDirectoryRequest(int command);
int mapDirectoryPoll(char* name);
void mapScanStart(void);
int mapScanTick(void);
void mapScanRelease(void);
int mapScanActive(void);
int mapScanCurrent(void);
int mapScanTotal(void);
int mapValidate(const mapRecord_t* map);
int mapOnlineDisabled(void);
int mapMultiplayerReady(void);
/* Nonzero only while the Local Play staging screen is active. */
int mapStaging(void);
void mapUpdateName(void);
int mapChooseBase(u32 base);
int mapChooseRetail(u32 base);
u32 mapCurrentBase(void);
const char* mapRetailName(u32 base);
int mapCompareText(const char* a, const char* b);
void mapRestoreBase(void);
int mapHooksReady(void);
void mapInstallHooks(void);
void mapQuickLoadBegin(void);
int mapQuickLoadEnabled(void);
void mapDisableHooks(void);
int mapLoading(void);
void mapClearLoad(void);
void mapMessage(const char* message);
int mapForgeActive(void);
int mapHopInGame(void);
int mapHopAvailable(void);
/* Nonnegative values are catalog indices; negative values select -retailBase. */
int mapHopRequest(int index);
int mapStartGame(void);
void mapHopTick(void);
void mapHopCancel(void);
void mapHopOnLoad(void);
#endif
