#include <librac4/map.h>
#include "world.h"
#include "objects.h"
#include <librac4/interop.h>
#include <librac4/game.h>
#include <librac4/player.h>

#define WORLD_CHANNEL (0x444c4357)
#define WORLD_MAGIC (0x32574344)
#define WORLD_INTERVAL (30000000)
#define WORLD_READY (1)
#define WORD(address) (*(volatile u32*)(address))

typedef struct coopWorld {
	/* 0x00 */ u32 magic;
	/* 0x04 */ int level;
	/* 0x08 */ int mission;
	/* 0x0c */ u32 flags;
	/* 0x10 */ int bolts;
	/* 0x14 */ int xp;
	/* 0x18 */ int points;
	/* 0x1c */ float health[2];
	/* 0x24 */ float maxHealth[2];
	/* 0x2c */ int missionLink;
	/* 0x30 */ int playerState[2];
} coopWorld_t;

static u32 lastSent;
static int pendingDeath[2], deathLevel, deathMission;

static int requestedLevel, requestedMission = -1;

static VariableAddress_t vaMissionIndex = {
#if RAC4_PAL
	.Battledome = 0x21ec58,
	.Catacrom = 0x21eb18,
	.Sarathos = 0x21ead8,
	.Kronos = 0x21ead8,
	.Shaar = 0x21ead8,
	.Valix = 0x21ead8,
	.Orxon = 0x21ead8,
	.Torval = 0x21ead8,
	.Stygia = 0x21ead8,
	.Maraxus = 0x21ead8,
	.GhostStation = 0x21ead8,
	.DreadZoneInterior = 0x21eb18
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x239358,
	.Catacrom = 0x239218,
	.Sarathos = 0x2391d8,
	.Kronos = 0x2391d8,
	.Shaar = 0x2391d8,
	.Valix = 0x2391d8,
	.Orxon = 0x2391d8,
	.Torval = 0x2391d8,
	.Stygia = 0x2391d8,
	.Maraxus = 0x2391d8,
	.GhostStation = 0x2391d8,
	.DreadZoneInterior = 0x239218
#else
	.Battledome = 0x21ebd8,
	.Catacrom = 0x21ea98,
	.Sarathos = 0x21ea58,
	.Kronos = 0x21ea58,
	.Shaar = 0x21ea58,
	.Valix = 0x21ea58,
	.Orxon = 0x21ea58,
	.Torval = 0x21ea58,
	.Stygia = 0x21ea58,
	.Maraxus = 0x21ea58,
	.GhostStation = 0x21ea58,
	.DreadZoneInterior = 0x21ea98
#endif
};

static VariableAddress_t vaLevelPending = {
#if RAC4_PAL
	.Battledome = 0x21e88c,
	.Catacrom = 0x21e75c,
	.Sarathos = 0x21e6fc,
	.Kronos = 0x21e6fc,
	.Shaar = 0x21e6fc,
	.Valix = 0x21e6fc,
	.Orxon = 0x21e6fc,
	.Torval = 0x21e6fc,
	.Stygia = 0x21e6fc,
	.Maraxus = 0x21e6fc,
	.GhostStation = 0x21e6fc,
	.DreadZoneInterior = 0x21e73c
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x23901c,
	.Catacrom = 0x238eec,
	.Sarathos = 0x238e8c,
	.Kronos = 0x238e8c,
	.Shaar = 0x238e8c,
	.Valix = 0x238e8c,
	.Orxon = 0x238e8c,
	.Torval = 0x238e8c,
	.Stygia = 0x238e8c,
	.Maraxus = 0x238e8c,
	.GhostStation = 0x238e8c,
	.DreadZoneInterior = 0x238ecc
#else
	.Battledome = 0x21e80c,
	.Catacrom = 0x21e6dc,
	.Sarathos = 0x21e67c,
	.Kronos = 0x21e67c,
	.Shaar = 0x21e67c,
	.Valix = 0x21e67c,
	.Orxon = 0x21e67c,
	.Torval = 0x21e67c,
	.Stygia = 0x21e67c,
	.Maraxus = 0x21e67c,
	.GhostStation = 0x21e67c,
	.DreadZoneInterior = 0x21e6bc
#endif
};

/*
 * NAME : worldTicks
 * DESCRIPTION : Read the EE clock for periodic campaign snapshots.
 * NOTES :
 * ARGS :
 * RETURN : Wrapping EE count.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 worldTicks(void)
{
	u32 value;
	__asm__ volatile("mfc0 %0, $9" : "=r"(value));
	return value;
}

/*
 * NAME : coopWorldReady
 * DESCRIPTION : Verify a stable supported co-op mission before touching actors.
 * NOTES : Transition flags are derived from the native SwitchToLevel store.
 * ARGS :
 * RETURN : Nonzero for normal gameplay or pause with no pending level load.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopWorldReady(void)
{
	u32 address = GetAddressImmediate(&vaSwitchToLevelFunc);
	u32 pending = GetAddressImmediate(&vaLevelPending);
	u32 mission = GetAddressImmediate(&vaMissionIndex);
	u32 modeAddress = GetAddressImmediate(&vaCoopGameMode);
	int mode = modeAddress ? (int)WORD(modeAddress) : GAME_MODE_NONE;
	if ((Level != 21 && Level != 22) || playerGetNumLocals() != 2 ||
	    (mode != GAME_MODE_NORMAL && mode != GAME_MODE_PAUSE) || !address ||
	    WORD(address) != 0x27bdffd0 || WORD(address + 0x34) != (0x8c630000 | ((u32)&Level & 0xffff)))
		return 0;
	return pending && mission && !WORD(pending) && !Mission->Loading &&
	    (int)WORD(mission) >= 0 && WORD(mission) < 64;
}

/*
 * NAME : applySharedStats
 * DESCRIPTION : Apply host health independently of the object baseline.
 * NOTES : Inventory, mission objectives and save persistence are not copied here.
 * ARGS : Validated host snapshot for the current loaded mission.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void applySharedStats(const coopWorld_t* frame)
{
	u32 heroes = GetAddressImmediate(&vaCoopPlayers), hero;
	int i;
	int* save = (int*)GetAddressImmediate(&vaCoopHeroSave);
	if (!heroes || !save || frame->bolts < 0 || frame->xp < 0 || frame->points < 0)
		return;
	for (i = 0; i < 2; ++i)
		if (!(frame->maxHealth[i] > 0 && frame->maxHealth[i] <= 1000000) ||
		    !(frame->health[i] >= -1000000 && frame->health[i] <= frame->maxHealth[i]))
			return;
	for (i = 0; i < 2; ++i) {
		hero = WORD(heroes + i * 4);
		if (hero < 0x100000 || hero > 0x1ffd000 || WORD(hero + 0x2f10) != (u32)i)
			return;
	}
	/* A missing crate or enemy must not stop health/death replication. */
	if (coopObjectsReady()) {
		save[0] = frame->bolts;
		save[2] = frame->xp;
		save[3] = frame->points;
	}
	deathLevel = Level;
	deathMission = Mission->ActiveMission;
	for (i = 0; i < 2; ++i) {
		hero = WORD(heroes + i * 4);
		*(float*)(hero + 0x2f24) = frame->maxHealth[i];
		*(float*)(hero + 0x2e20) = frame->health[i] > 0 ? frame->health[i] : 0;
		pendingDeath[i] = frame->health[i] <= 0 || playerStateIsDead(frame->playerState[i]);
	}
}

/*
 * NAME : coopWorldApplyPlayers
 * DESCRIPTION : Enter replicated deaths through the normal native body state.
 * NOTES : Called before drawing, never from an interrupting packet callback.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopWorldApplyPlayers(void)
{
	u32 heroes, hero, vtable, init = GetAddressImmediate(&vaCoopPlayerInitBodyState);
	int i;
	if (deathLevel != Level || deathMission != Mission->ActiveMission) {
		pendingDeath[0] = pendingDeath[1] = 0;
		return;
	}
	if ((!pendingDeath[0] && !pendingDeath[1]) || !coopWorldReady() || !init || WORD(init) != 0x27bdff20)
		return;
	heroes = GetAddressImmediate(&vaCoopPlayers);
	for (i = 0; i < 2; ++i) {
		if (!pendingDeath[i])
			continue;
		hero = WORD(heroes + i * 4);
		if (hero < 0x100000 || hero > 0x1ffd000 || WORD(hero + 0x2f10) != (u32)i)
			continue;
		vtable = WORD(hero + 0x14);
		if (vtable < 0x100000 || vtable > 0x1ffffc0 || WORD(vtable + 0x3c) != init)
			continue;
		pendingDeath[i] = 0;
		if (!playerStateIsDead(WORD(hero + 0x25cc)))
			((void (*)(u32, int, int, int, int))init)(hero, PLAYER_STATE_DEATH, 1, 1, 1);
	}
}

/*
 * NAME : coopWorldUpdate
 * DESCRIPTION : Advertise host mission identity and follow its normal transition.
 * NOTES : Only the host supplies campaign state. World-object transfer is separate.
 * ARGS : Loader, optional Online API and whether the adapter is enabled.
 * RETURN : Nonzero when this frame's outgoing slot contains a world snapshot.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopWorldUpdate(const moduleApi_t* loader, const onlineApi_t* online, int enabled)
{
	coopWorld_t frame;
	u32 now = worldTicks(), address, heroes, hero;
	int* save;
	int role, ready, i, size;
	if (!enabled || loader->context != DL_CONTEXT_CAMPAIGN || !online || !(role = online->role())) {
		requestedLevel = 0;
		requestedMission = -1;
		pendingDeath[0] = pendingDeath[1] = 0;
		return 0;
	}
	ready = coopWorldReady();
	size = online->receive(WORLD_CHANNEL, &frame, sizeof(frame));
	if (role == ONLINE_JOIN && size == sizeof(frame) && frame.magic == WORLD_MAGIC &&
	    (frame.flags & WORLD_READY) && (frame.level == 21 || frame.level == 22) &&
	    frame.mission >= 0 && frame.mission < 64 && frame.missionLink >= 0 && frame.missionLink < 256 && ready) {
		if (frame.level != Level || frame.mission != (int)WORD(GetAddressImmediate(&vaMissionIndex)) || frame.missionLink != Mission->ActiveMission) {
			if (requestedLevel != frame.level || requestedMission != frame.mission) {
				address = GetAddressImmediate(&vaSwitchToLevelFunc);
				requestedLevel = frame.level;
				requestedMission = frame.mission;
				/* Use the native transition without requesting a guest save. */
				((void (*)(int, int, int))address)(frame.level, 0, frame.mission);
			}
		} else {
			requestedLevel = 0;
			requestedMission = -1;
			applySharedStats(&frame);
		}
	}
	if (role != ONLINE_HOST || now - lastSent < WORLD_INTERVAL)
		return 0;
	frame.magic = WORLD_MAGIC;
	frame.level = Level;
	frame.mission = ready ? (int)WORD(GetAddressImmediate(&vaMissionIndex)) : -1;
	frame.missionLink = Mission->ActiveMission;
	frame.flags = ready ? WORLD_READY : 0;
	save = (int*)GetAddressImmediate(&vaCoopHeroSave);
	frame.bolts = save ? save[0] : 0;
	frame.xp = save ? save[2] : 0;
	frame.points = save ? save[3] : 0;
	heroes = ready ? GetAddressImmediate(&vaCoopPlayers) : 0;
	for (i = 0; i < 2; ++i) {
		hero = heroes ? WORD(heroes + i * 4) : 0;
		frame.health[i] = frame.maxHealth[i] = 0;
		frame.playerState[i] = 0;
		if (hero >= 0x100000 && hero < 0x1ffd000) {
			frame.health[i] = *(float*)(hero + 0x2e20);
			frame.maxHealth[i] = *(float*)(hero + 0x2f24);
			frame.playerState[i] = WORD(hero + 0x25cc);
		}
	}
	if (online->send(WORLD_CHANNEL, &frame, sizeof(frame)) != 0)
		return 0;
	lastSent = now;
	return 1;
}
