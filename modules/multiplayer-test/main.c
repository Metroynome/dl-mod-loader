#include "module.h"
#include <libdl/stdio.h>
/* Include <libdl/...h> for this mode's game functions. */
#include <libdl/dl.h>

/* This pointer is updated by the loader on mode transitions. */
static const ModuleApi *loader;
int moduleInit(const ModuleApi *api)
{
    loader = api;
    printf("multiplayer-test: initialized (region %u, mode %u)\n", api->region, api->mode);
    return 0;
}
void moduleUpdate(void)
{
    dlPreUpdate();
    /* Called only in this module's declared mode. Check that the level/player
     * is ready before using game-library addresses. Keep persistent state here
     * in module BSS, never in hard-coded game memory. */
    dlPostUpdate();
}
