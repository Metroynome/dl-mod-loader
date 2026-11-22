#include <librac4/map.h>
#include "world.h"
#include "objects.h"
#include "ui/session.h"
#include <librac4/interop.h>
#include <librac4/game.h>
#include <librac4/player.h>

#define WORLD_CHANNEL (0x444c4357)
#define WORLD_MAGIC (0x34574344)
#define WORLD_INTERVAL (30000000)
#define WORLD_MENU_INTERVAL (5000000)
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
	/* 0x38 */ coopSessionMenu_t menu;
	/* 0x44 */ u32 hitEpoch;
	/* 0x48 */ u32 hitSequence[2];
} coopWorld_t;

typedef char coopWorldSize[(sizeof(coopWorld_t) == 0x50) ? 1 : -1];

static u32 lastSent;
static int pendingDeath[2], deathLevel, deathMission;
static int pendingHit[2], previousHitState[2];
static int hitLevel, hitMission, hitRole;
static u32 hitEpoch, hitSequence[2];

static int requestedLevel, requestedMission = -1;
static coopHostSession_t hostSession;
static u32 hostSessionTime;
static int hostSessionValid;
static u32 lastHostFrame;

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
 * NAME : coopWorldGetHostSession
 * DESCRIPTION : Read a fresh, playable host mission before the guest loads.
 * NOTES : Mission selection remains joinable; transitions expire after two seconds.
 * ARGS : Session destination.
 * RETURN : Nonzero when the host has advertised a supported ready mission.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopWorldGetHostSession(coopHostSession_t* session)
{
	if (!session || !hostSessionValid || worldTicks() - hostSessionTime >= 294912000)
		return 0;
	*session = hostSession;
	return 1;
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
	if ((Level != 21 && Level != 22) || playerGetNumLocals() != 2 || !address ||
	    WORD(address) != 0x27bdffd0 || WORD(address + 0x34) != (0x8c630000 | ((u32)&Level & 0xffff)) ||
	    !pending || !mission || WORD(pending) || Mission->Loading)
		return 0;
	return (mode == GAME_MODE_NORMAL || mode == GAME_MODE_PAUSE || coopSessionRead(0)) &&
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
		/* Native Inc/DecHitPoints reveal the health HUD for six seconds. */
		if (*(float*)(hero + 0x2e20) != frame->health[i])
			WORD(hero + 0x2ed8) = 360;
		*(float*)(hero + 0x2f24) = frame->maxHealth[i];
		*(float*)(hero + 0x2e20) = frame->health[i] > 0 ? frame->health[i] : 0;
		pendingDeath[i] = frame->health[i] <= 0 || playerStateIsDead(frame->playerState[i]);
	}
	if (hitEpoch != frame->hitEpoch) {
		hitEpoch = frame->hitEpoch;
		pendingHit[0] = pendingHit[1] = 0;
		for (i = 0; i < 2; ++i)
			hitSequence[i] = frame->hitSequence[i];
	} else {
		for (i = 0; i < 2; ++i) {
			if ((int)(frame->hitSequence[i] - hitSequence[i]) <= 0)
				continue;
			hitSequence[i] = frame->hitSequence[i];
			pendingHit[i] = !pendingDeath[i];
		}
	}
}

/*
 * NAME : coopWorldApplyPlayers
 * DESCRIPTION : Enter confirmed hits and deaths through the native body state.
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
		pendingHit[0] = pendingHit[1] = 0;
		return;
	}
	if ((!pendingDeath[0] && !pendingDeath[1] && !pendingHit[0] && !pendingHit[1]) ||
	    !coopWorldReady() || !init || WORD(init) != 0x27bdff20)
		return;
	heroes = GetAddressImmediate(&vaCoopPlayers);
	for (i = 0; i < 2; ++i) {
		if (!pendingDeath[i] && !pendingHit[i])
			continue;
		hero = WORD(heroes + i * 4);
		if (hero < 0x100000 || hero > 0x1ffd000 || WORD(hero + 0x2f10) != (u32)i)
			continue;
		vtable = WORD(hero + 0x14);
		if (vtable < 0x100000 || vtable > 0x1ffffc0 || WORD(vtable + 0x3c) != init)
			continue;
		if (pendingDeath[i]) {
			pendingDeath[i] = pendingHit[i] = 0;
			if (!playerStateIsDead(WORD(hero + 0x25cc)))
				((int (*)(u32, int, int, int, int))init)(hero, PLAYER_STATE_DEATH, 1, 1, 1);
		} else {
			u32 moby = WORD(hero + 0x2ef0);
			u32 mode = GetAddressImmediate(&vaCoopGameMode);
			pendingHit[i] = 0;
			if (!mode || WORD(mode) != GAME_MODE_NORMAL ||
			    moby < 0x00100000 || moby > 0x01ffff00 ||
			    *(float*)(hero + 0x2e20) <= 0 || playerStateIsDead(WORD(hero + 0x25cc)))
				continue;
			if (WORD(hero + 0x25cc) == PLAYER_STATE_GET_HIT)
				continue;
			/* InitBodyState reads this transient collision index to apply damage. */
			*(u8*)(moby + 0x2c) = 255;
			((int (*)(u32, int, int, int, int))init)(hero, PLAYER_STATE_GET_HIT, 1, 0, 1);
		}
	}
}

/*
 * NAME : captureHits
 * DESCRIPTION : Retain each host-confirmed entry into the regular hit reaction.
 * NOTES : Runs every update, before world packet throttling; health changes are not hit events.
 * ARGS : Current clock and whether the host has a loaded mission.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void captureHits(u32 now, int ready)
{
	u32 heroes = ready ? GetAddressImmediate(&vaCoopPlayers) : 0;
	int i, fresh = !hitEpoch || hitLevel != Level || hitMission != Mission->ActiveMission;
	if (!heroes) {
		hitEpoch = 0;
		return;
	}
	if (fresh) {
		hitEpoch = now | 1;
		hitLevel = Level;
		hitMission = Mission->ActiveMission;
		hitSequence[0] = hitSequence[1] = 0;
	}
	for (i = 0; i < 2; ++i) {
		u32 hero = WORD(heroes + i * 4);
		int state;
		if (hero < 0x00100000 || hero > 0x01ffd000 || WORD(hero + 0x2f10) != (u32)i)
			continue;
		state = WORD(hero + 0x25cc);
		if (!fresh && state == PLAYER_STATE_GET_HIT && previousHitState[i] != state)
			++hitSequence[i];
		previousHitState[i] = state;
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
		coopSessionReset();
		hostSessionValid = 0;
		requestedLevel = 0;
		requestedMission = -1;
		pendingDeath[0] = pendingDeath[1] = 0;
		pendingHit[0] = pendingHit[1] = 0;
		hitEpoch = hitRole = 0;
		return 0;
	}
	if (hitRole != role) {
		hitRole = role;
		hitEpoch = 0;
		pendingHit[0] = pendingHit[1] = 0;
	}
	ready = coopWorldReady();
	if (role == ONLINE_HOST)
		captureHits(now, ready);
	size = online->receive(WORLD_CHANNEL, &frame, sizeof(frame));
	if (role == ONLINE_JOIN && size == sizeof(frame) && frame.magic == WORLD_MAGIC) {
		hostSessionValid = (frame.flags & WORLD_READY) && (frame.level == 21 || frame.level == 22) &&
		    frame.mission >= 0 && frame.mission < 64 && frame.missionLink >= -1 && frame.missionLink < 256;
		if (hostSessionValid) {
			lastHostFrame = now;
			hostSession.level = frame.level;
			hostSession.mission = frame.mission;
			hostSession.missionLink = frame.missionLink;
			hostSessionTime = now;
		}
	}
	if (role == ONLINE_JOIN && size == sizeof(frame) && frame.magic == WORLD_MAGIC &&
	    (frame.flags & WORLD_READY) && (frame.level == 21 || frame.level == 22) &&
	    frame.mission >= 0 && frame.mission < 64 && frame.missionLink >= -1 && frame.missionLink < 256 && ready) {
		if (frame.level != Level || frame.mission != (int)WORD(GetAddressImmediate(&vaMissionIndex)) ||
		    (frame.menu.screen == 0 && frame.missionLink != Mission->ActiveMission)) {
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
			coopSessionApply(&frame.menu);
			applySharedStats(&frame);
		}
	}
	if (role == ONLINE_JOIN && coopSessionWatching() && now - lastHostFrame >= 294912000)
		coopSessionApply(0);
	if (role != ONLINE_HOST)
		return 0;
	frame.menu.screen = frame.menu.focus = frame.menu.planet = 0;
	if (ready)
		coopSessionRead(&frame.menu);
	if (now - lastSent < (frame.menu.screen ? WORLD_MENU_INTERVAL : WORLD_INTERVAL))
		return 0;
	frame.magic = WORLD_MAGIC;
	frame.level = Level;
	frame.mission = ready ? (int)WORD(GetAddressImmediate(&vaMissionIndex)) : -1;
	frame.missionLink = Mission->ActiveMission;
	frame.flags = ready ? WORLD_READY : 0;
	frame.hitEpoch = hitEpoch;
	for (i = 0; i < 2; ++i)
		frame.hitSequence[i] = hitSequence[i];
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
