#ifndef DL_MODULE_H
#define DL_MODULE_H
#define DL_MODULE_MAGIC 0x4d4c4444U
#define DL_MODULE_VERSION 1U
#define DL_MODULE_NO_ENTRY 0xffffffffU
#define DL_MODE_CAMPAIGN 1U
#define DL_MODE_MULTIPLAYER 2U
#define DL_MODE_BOTH 3U
/* REGION is set by build.py: 1=NTSC-U, 2=PAL, 3=NTSC-J, 4=NTSC-K. */
typedef struct ModuleHeader {
    unsigned int magic, version, region, address;
    unsigned int imageSize, memorySize, initOffset, updateOffset;
    unsigned int modes, reserved;
} ModuleHeader;
typedef struct ModuleApi {
    unsigned int version, region, mode;
    int (*print)(const char *format, ...);
    int (*open)(const char *path, int flags, unsigned short mode);
    int (*read)(int fd, void *buffer, int size);
    int (*close)(int fd);
    void (*flushCache)(int operation);
} ModuleApi;
/* Retain the pointer, not a copy: mode and function pointers change on a
 * campaign/multiplayer transition. init runs once, update runs per return hook
 * (not necessarily once per video frame). Never retain game-object pointers
 * across level changes. */
typedef int (*ModuleInit)(const ModuleApi *api);
typedef void (*ModuleUpdate)(void);
#endif
