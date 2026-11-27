#include "module.h"
#include "modules/forge/include/forge.h"
#include "shared/travel.h"

#ifdef FORGE_MULTIPLAYER
int forgeMultiplayerInit(const moduleApi_t* api);
void forgeMultiplayerUpdate(void);
#else
void forgeBridgeInit(void);
void forgeBridgeDisable(void);
void forgeBridgeTick(const mapHopApi_t* api);

/*
 * NAME : deactivate
 * DESCRIPTION : Release travel controls when Forge becomes inactive.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void deactivate(void)
{
	forgeTravelDisable();
	forgeBridgeDisable();
}
#endif

/*
 * NAME : moduleInit
 * DESCRIPTION : Initialize shared Forge controls and the mode-specific backend.
 * NOTES : Native gameplay calls remain inside their respective backend.
 * ARGS : Loader API.
 * RETURN : Module initialization result.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int moduleInit(const moduleApi_t* api)
{
	forgeTravelInit(api);
#ifdef FORGE_MULTIPLAYER
	return forgeMultiplayerInit(api);
#else
	if (api->version < DL_API_VERSION || api->size < sizeof(*api))
		return -1;
	forgeBridgeInit();
	api->restrictContexts(DL_CONTEXT_CAMPAIGN);
	api->setLifecycle(0, deactivate);
	return 0;
#endif
}

/*
 * NAME : moduleUpdate
 * DESCRIPTION : Update Forge while releasing the previous mode's menu first.
 * NOTES : Avoids temporarily exhausting shared Mod Menu item slots.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void moduleUpdate(void)
{
#ifdef FORGE_MULTIPLAYER
	if (forgeTravelContext()) {
		forgeMultiplayerUpdate();
		forgeTravelUpdate();
	} else {
		forgeTravelUpdate();
		forgeMultiplayerUpdate();
	}
#else
	forgeTravelUpdate();
	forgeBridgeTick(0);
#endif
}
