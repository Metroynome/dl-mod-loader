#include "encounters.h"
#include "objects.h"
#include "world.h"
#include <librac4/game.h>
#include <librac4/map.h>
#include <librac4/interop.h>
#include <librac4/math.h>

#define ENCOUNTER_CHANNEL (0x444c4345)
#define ENCOUNTER_MAGIC (0x31454344)
#define ENCOUNTER_LIMIT (128)
#define ENCOUNTER_RETRY (15000000)
#define ENCOUNTER_TIMEOUT (900000000)
#define ENCOUNTER_WAKE (1)
#define ENCOUNTER_ACK (2)
#define ENCOUNTER_REJECT (4)
#define WORD(address) (*(volatile u32*)(address))
#define RANDOM_SEED (*mathRandomSeed)
#if RAC4_PAL
#define RANDOM_FUNCTION (0x163410)
#elif RAC4_NTSCJ || RAC4_NTSCK
#define RANDOM_FUNCTION (0x1633c0)
#else
#define RANDOM_FUNCTION (0x163408)
#endif

typedef struct encounterEvent {
	/* 0x00 */ u32 magic;
	/* 0x04 */ int level;
	/* 0x08 */ int mission;
	/* 0x0c */ u32 epoch;
	/* 0x10 */ u32 sequence;
	/* 0x14 */ u32 kind;
	/* 0x18 */ s16 uid;
	/* 0x1a */ u16 oClass;
	/* 0x1c */ s16 controller;
	/* 0x1e */ s16 wave;
	/* 0x20 */ s16 reference;
	/* 0x22 */ u8 placed;
	/* 0x23 */ u8 reserved;
	/* 0x24 */ float position[3];
	/* 0x30 */ float rotation[3];
	/* 0x3c */ u32 seed;
} encounterEvent_t;

static void wakeMoby(u32 moby, int placed, u32 reference, u32 wave, u32 controller);
static void updateWaves(u32 controller);

static encounterEvent_t history[ENCOUNTER_LIMIT], pending;
static int recorded, acknowledged, installed, currentRole, currentMap, currentMission;
static int pendingWake, pendingAck;
static u32 epoch, receivedSequence, lastSent, receivedTime, rejected, overflow;
static VariableAddress_t vaWakeMoby = {
#if RAC4_PAL
	.Battledome = 0x56fe58,
	.Catacrom = 0x521f58,
	.Sarathos = 0x522108,
	.Kronos = 0x546800,
	.Shaar = 0x519b30,
	.Valix = 0x50b2a8,
	.Orxon = 0x5231b8,
	.Torval = 0x52df30,
	.Stygia = 0x51abc8,
	.Maraxus = 0x5345d8,
	.GhostStation = 0x52d2a0,
	.DreadZoneInterior = 0x53ee98
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x588390,
	.Catacrom = 0x53a8b8,
	.Sarathos = 0x53ac98,
	.Kronos = 0x55f1d0,
	.Shaar = 0x532940,
	.Valix = 0x523f38,
	.Orxon = 0x53bb48,
	.Torval = 0x546900,
	.Stygia = 0x533858,
	.Maraxus = 0x54cee8,
	.GhostStation = 0x545bf0,
	.DreadZoneInterior = 0x557880
#else
	.Battledome = 0x56d280,
	.Catacrom = 0x51f9a8,
	.Sarathos = 0x51fcc8,
	.Kronos = 0x5441c0,
	.Shaar = 0x5179f0,
	.Valix = 0x5093e8,
	.Orxon = 0x520bf8,
	.Torval = 0x52b9f0,
	.Stygia = 0x518908,
	.Maraxus = 0x531f98,
	.GhostStation = 0x52aca0,
	.DreadZoneInterior = 0x53c7b0
#endif
};

static VariableAddress_t vaUpdateWaves = {
#if RAC4_PAL
	.Battledome = 0x56fca0,
	.Catacrom = 0x521da0,
	.Sarathos = 0x521f50,
	.Kronos = 0x546648,
	.Shaar = 0x519978,
	.Valix = 0x50b0f0,
	.Orxon = 0x523000,
	.Torval = 0x52dd78,
	.Stygia = 0x51aa10,
	.Maraxus = 0x534420,
	.GhostStation = 0x52d0e8,
	.DreadZoneInterior = 0x53ece0
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5881d8,
	.Catacrom = 0x53a700,
	.Sarathos = 0x53aae0,
	.Kronos = 0x55f018,
	.Shaar = 0x532788,
	.Valix = 0x523d80,
	.Orxon = 0x53b990,
	.Torval = 0x546748,
	.Stygia = 0x5336a0,
	.Maraxus = 0x54cd30,
	.GhostStation = 0x545a38,
	.DreadZoneInterior = 0x5576c8
#else
	.Battledome = 0x56d0c8,
	.Catacrom = 0x51f7f0,
	.Sarathos = 0x51fb10,
	.Kronos = 0x544008,
	.Shaar = 0x517838,
	.Valix = 0x509230,
	.Orxon = 0x520a40,
	.Torval = 0x52b838,
	.Stygia = 0x518750,
	.Maraxus = 0x531de0,
	.GhostStation = 0x52aae8,
	.DreadZoneInterior = 0x53c5f8
#endif
};

static VariableAddress_t vaWaveCall = {
#if RAC4_PAL
	.Battledome = 0x56bb68,
	.Catacrom = 0x51dc68,
	.Sarathos = 0x51ded0,
	.Kronos = 0x542510,
	.Shaar = 0x515840,
	.Valix = 0x506fb8,
	.Orxon = 0x51eec8,
	.Torval = 0x529c40,
	.Stygia = 0x516a28,
	.Maraxus = 0x5302e8,
	.GhostStation = 0x529100,
	.DreadZoneInterior = 0x53aba8
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5840b8,
	.Catacrom = 0x5365e0,
	.Sarathos = 0x536a78,
	.Kronos = 0x55aef8,
	.Shaar = 0x52e668,
	.Valix = 0x51fc60,
	.Orxon = 0x537870,
	.Torval = 0x542628,
	.Stygia = 0x52f6d0,
	.Maraxus = 0x548c10,
	.GhostStation = 0x541a68,
	.DreadZoneInterior = 0x5535a8
#else
	.Battledome = 0x568fa8,
	.Catacrom = 0x51b6d0,
	.Sarathos = 0x51baa8,
	.Kronos = 0x53fee8,
	.Shaar = 0x513718,
	.Valix = 0x505110,
	.Orxon = 0x51c920,
	.Torval = 0x527718,
	.Stygia = 0x514780,
	.Maraxus = 0x52dcc0,
	.GhostStation = 0x526b18,
	.DreadZoneInterior = 0x5384d8
#endif
};

static VariableAddress_t vaWakeCallA = {
#if RAC4_PAL
	.Battledome = 0x4c5c78,
	.Catacrom = 0x3fb4b8,
	.Torval = 0x4aab40,
	.GhostStation = 0x4b2050,
	.DreadZoneInterior = 0x4d43d0
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x4deee8,
	.Catacrom = 0x4159d8,
	.Torval = 0x4c42d8,
	.GhostStation = 0x4cb4a0,
	.DreadZoneInterior = 0x4ed8a8
#else
	.Battledome = 0x4c3d90,
	.Catacrom = 0x3fab98,
	.Torval = 0x4a93c0,
	.GhostStation = 0x4b04e8,
	.DreadZoneInterior = 0x4d2768
#endif
};

static VariableAddress_t vaWakeCallB = {
#if RAC4_PAL
	.Battledome = 0x4c67e0,
	.Catacrom = 0x3fc020,
	.Torval = 0x4ab6a8,
	.GhostStation = 0x4b2bb8,
	.DreadZoneInterior = 0x4d4f38
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x4dfa38,
	.Catacrom = 0x416528,
	.Torval = 0x4c4e28,
	.GhostStation = 0x4cbff0,
	.DreadZoneInterior = 0x4ee3f8
#else
	.Battledome = 0x4c48e0,
	.Catacrom = 0x3fb6e8,
	.Torval = 0x4a9f10,
	.GhostStation = 0x4b1038,
	.DreadZoneInterior = 0x4d32b8
#endif
};

static VariableAddress_t vaWakeCallC = {
#if RAC4_PAL
	.Battledome = 0x3d79d4,
	.Catacrom = 0x479c2c,
	.Orxon = 0x3fa52c,
	.Torval = 0x4db264,
	.Maraxus = 0x4c85d4,
	.GhostStation = 0x46cb04,
	.DreadZoneInterior = 0x3c8fac
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x3f23ec,
	.Catacrom = 0x4934c4,
	.Orxon = 0x414a4c,
	.Torval = 0x4f468c,
	.Maraxus = 0x4e1a8c,
	.GhostStation = 0x48663c,
	.DreadZoneInterior = 0x3e3844
#else
	.Battledome = 0x3d752c,
	.Catacrom = 0x4785ac,
	.Orxon = 0x3f9be4,
	.Torval = 0x4d96f4,
	.Maraxus = 0x4c6ad4,
	.GhostStation = 0x46b724,
	.DreadZoneInterior = 0x3c8bc4
#endif
};

static VariableAddress_t vaWakeCallD = {
#if RAC4_PAL
	.Battledome = 0x56f364,
	.Catacrom = 0x521464,
	.Sarathos = 0x521614,
	.Kronos = 0x545d0c,
	.Shaar = 0x51903c,
	.Valix = 0x50a7b4,
	.Orxon = 0x5226c4,
	.Torval = 0x52d43c,
	.Stygia = 0x51a0d4,
	.Maraxus = 0x533ae4,
	.GhostStation = 0x52c7ac,
	.DreadZoneInterior = 0x53e3a4
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5878b8,
	.Catacrom = 0x539de0,
	.Sarathos = 0x53a1c0,
	.Kronos = 0x55e6f8,
	.Shaar = 0x531e68,
	.Valix = 0x523460,
	.Orxon = 0x53b070,
	.Torval = 0x545e28,
	.Stygia = 0x532d80,
	.Maraxus = 0x54c410,
	.GhostStation = 0x545118,
	.DreadZoneInterior = 0x556da8
#else
	.Battledome = 0x56c7a8,
	.Catacrom = 0x51eed0,
	.Sarathos = 0x51f1f0,
	.Kronos = 0x5436e8,
	.Shaar = 0x516f18,
	.Valix = 0x508910,
	.Orxon = 0x520120,
	.Torval = 0x52af18,
	.Stygia = 0x517e30,
	.Maraxus = 0x5314c0,
	.GhostStation = 0x52a1c8,
	.DreadZoneInterior = 0x53bcd8
#endif
};

static VariableAddress_t vaWakeCallE = {
#if RAC4_PAL
	.Battledome = 0x56f73c,
	.Catacrom = 0x52183c,
	.Sarathos = 0x5219ec,
	.Kronos = 0x5460e4,
	.Shaar = 0x519414,
	.Valix = 0x50ab8c,
	.Orxon = 0x522a9c,
	.Torval = 0x52d814,
	.Stygia = 0x51a4ac,
	.Maraxus = 0x533ebc,
	.GhostStation = 0x52cb84,
	.DreadZoneInterior = 0x53e77c
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x587c74,
	.Catacrom = 0x53a19c,
	.Sarathos = 0x53a57c,
	.Kronos = 0x55eab4,
	.Shaar = 0x532224,
	.Valix = 0x52381c,
	.Orxon = 0x53b42c,
	.Torval = 0x5461e4,
	.Stygia = 0x53313c,
	.Maraxus = 0x54c7cc,
	.GhostStation = 0x5454d4,
	.DreadZoneInterior = 0x557164
#else
	.Battledome = 0x56cb64,
	.Catacrom = 0x51f28c,
	.Sarathos = 0x51f5ac,
	.Kronos = 0x543aa4,
	.Shaar = 0x5172d4,
	.Valix = 0x508ccc,
	.Orxon = 0x5204dc,
	.Torval = 0x52b2d4,
	.Stygia = 0x5181ec,
	.Maraxus = 0x53187c,
	.GhostStation = 0x52a584,
	.DreadZoneInterior = 0x53c094
#endif
};

/*
 * NAME : encounterTicks
 * DESCRIPTION : Pace wake-event retries without level-dependent game calls.
 * NOTES :
 * ARGS :
 * RETURN : Wrapping EE count.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 encounterTicks(void)
{
	u32 value;
	__asm__ volatile("mfc0 %0, $9" : "=r"(value));
	return value;
}

/*
 * NAME : objectForUid
 * DESCRIPTION : Resolve one unambiguous local map object.
 * NOTES : No pointer received over the network is followed.
 * ARGS : Native map UID and optional class filter.
 * RETURN : Matching local moby, or zero.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 objectForUid(int uid, u16 oClass)
{
	u32 start, end, moby, found = 0;
	if (uid < 0 || !coopObjectsBounds(&start, &end))
		return 0;
	for (moby = start; moby < end; moby += 0x100) {
		if (*(s16*)(moby + 0xb2) != uid || (oClass && *(u16*)(moby + 0xbc) != oClass))
			continue;
		if (found)
			return 0;
		found = moby;
	}
	return found;
}

/*
 * NAME : uidForVars
 * DESCRIPTION : Identify a wave or controller by its owning map object.
 * NOTES : Native wave counters depend on initialization order; map UIDs do not.
 * ARGS : Local variable block and expected owner class.
 * RETURN : Unique map UID, or minus one.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int uidForVars(u32 vars, u16 oClass)
{
	u32 start, end, moby;
	int uid = -1;
	if (!vars || !coopObjectsBounds(&start, &end))
		return -1;
	for (moby = start; moby < end; moby += 0x100) {
		if (*(u16*)(moby + 0xbc) != oClass || WORD(moby + 0xac) != vars)
			continue;
		if (uid >= 0)
			return -1;
		uid = *(s16*)(moby + 0xb2);
	}
	return uid;
}

/*
 * NAME : wakeMoby
 * DESCRIPTION : Record host wake decisions and suppress independent guest wakes.
 * NOTES : Unadapted enemy classes retain their native behavior.
 * ARGS : Native moby, placement flag, spawn reference, wave and controller.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void wakeMoby(u32 moby, int placed, u32 reference, u32 wave, u32 controller)
{
	encounterEvent_t* event;
	u32 start, end;
	int gc, waveUid, index = -1, i;
	if (coopObjectsBounds(&start, &end) && moby >= start && moby < end &&
	    !((moby - start) & 255) && coopObjectsEnemyClass(*(u16*)(moby + 0xbc))) {
		if (currentRole == ONLINE_JOIN)
			return;
		gc = uidForVars(controller, 0x2037);
		waveUid = uidForVars(wave, 0x2112);
		if (wave && reference >= wave + 0x50 && reference < wave + 0x170 && !((reference - wave - 0x50) % 0x24))
			index = (reference - wave - 0x50) / 0x24;
		if (recorded < ENCOUNTER_LIMIT && gc >= 0 && (!wave || waveUid >= 0) && (!reference || index >= 0)) {
			event = &history[recorded];
			event->uid = *(s16*)(moby + 0xb2);
			event->oClass = *(u16*)(moby + 0xbc);
			event->controller = gc;
			event->wave = waveUid;
			event->reference = index;
			event->placed = placed != 0;
			event->reserved = 0;
			event->seed = RANDOM_SEED;
			for (i = 0; i < 3; ++i) {
				event->position[i] = ((float*)(moby + 0x10))[i];
				event->rotation[i] = ((float*)(moby + 0xf0))[i];
			}
			/* Publish only after the complete event can survive a VSync. */
			__asm__ volatile("" ::: "memory");
			++recorded;
		} else {
			++overflow;
		}
	}
	((void (*)(u32, int, u32, u32, u32))GetAddressImmediate(&vaWakeMoby))(moby, placed, reference, wave, controller);
	coopObjectsWake(moby);
}

/*
 * NAME : updateWaves
 * DESCRIPTION : Replay a host wake inside the native encounter update.
 * NOTES : Scope the host random seed to spawn setup; restore the guest stream.
 * ARGS : Local native controller.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void updateWaves(u32 controller)
{
	u32 owner, moby, waveMoby, wave = 0, reference = 0, savedSeed, native;
	int i;
	if (currentRole == ONLINE_JOIN && pendingWake &&
	    uidForVars(controller, 0x2037) == pending.controller) {
		owner = objectForUid(pending.controller, 0x2037);
		moby = objectForUid(pending.uid, pending.oClass);
		waveMoby = pending.wave >= 0 ? objectForUid(pending.wave, 0x2112) : 0;
		if (waveMoby)
			wave = WORD(waveMoby + 0xac);
		if (owner && WORD(owner + 0xac) == controller && moby &&
		    *(s8*)(moby + 0x20) >= 0 && (pending.wave < 0 || wave) &&
		    (pending.wave < 0 || (wave >= 0x100000 && wave <= 0x1fffe10 && WORD(wave + 0x1b0) == controller))) {
			if (pending.reference >= 0)
				reference = wave + 0x50 + pending.reference * 0x24;
			if (!reference || *(u16*)reference == pending.oClass) {
				for (i = 0; i < 3; ++i) {
					((float*)(moby + 0x10))[i] = pending.position[i];
					((float*)(moby + 0xf0))[i] = pending.rotation[i];
				}
				native = GetAddressImmediate(&vaWakeMoby);
				savedSeed = RANDOM_SEED;
				RANDOM_SEED = pending.seed;
				((void (*)(u32, int, u32, u32, u32))native)(moby, pending.placed, reference, wave, controller);
				RANDOM_SEED = savedSeed;
				receivedSequence = pending.sequence;
				pendingWake = 0;
				pendingAck = ENCOUNTER_ACK;
				pending.kind = pendingAck;
			}
		}
	}
	((void (*)(u32))GetAddressImmediate(&vaUpdateWaves))(controller);
}

/*
 * NAME : setHooks
 * DESCRIPTION : Install or restore verified encounter call sites.
 * NOTES : Restore only instructions still owned by this module.
 * ARGS : Loader API and desired hook state.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void setHooks(const moduleApi_t* loader, int enabled)
{
	u32 calls[6] = {GetAddressImmediate(&vaWaveCall), GetAddressImmediate(&vaWakeCallA),
	    GetAddressImmediate(&vaWakeCallB), GetAddressImmediate(&vaWakeCallC),
	    GetAddressImmediate(&vaWakeCallD), GetAddressImmediate(&vaWakeCallE)};
	u32 native, replacement;
	int i;
	if (enabled == installed || Level != 22)
		return;
	if (enabled) {
		for (i = 0; i < 6; ++i) {
			native = GetAddressImmediate(i ? &vaWakeMoby : &vaUpdateWaves);
			if (!calls[i] || WORD(calls[i]) != (0x0c000000 | (native >> 2)))
				return;
		}
		if (WORD(RANDOM_FUNCTION) != (0x3c080000 | (((u32)mathRandomSeed + 0x8000) >> 16)) ||
		    WORD(RANDOM_FUNCTION + 8) != (0x25080000 | ((u32)mathRandomSeed & 0xffff)))
			return;
	}
	for (i = 0; i < 6; ++i) {
		native = 0x0c000000 | (GetAddressImmediate(i ? &vaWakeMoby : &vaUpdateWaves) >> 2);
		replacement = 0x0c000000 | ((i ? (u32)wakeMoby : (u32)updateWaves) >> 2);
		if (enabled)
			WORD(calls[i]) = replacement;
		else if (WORD(calls[i]) == replacement)
			WORD(calls[i]) = native;
	}
	installed = enabled;
	loader->flushCache(0);
	loader->flushCache(2);
}

/*
 * NAME : coopEncountersReset
 * DESCRIPTION : Release encounter hooks before module deactivation.
 * NOTES : History belongs only to the current loaded mission.
 * ARGS : Loader API.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopEncountersReset(const moduleApi_t* loader)
{
	setHooks(loader, 0);
	installed = recorded = acknowledged = currentRole = pendingWake = pendingAck = 0;
	epoch = receivedSequence = lastSent = 0;
}

/*
 * NAME : coopEncountersUpdate
 * DESCRIPTION : Deliver ordered host wake events with acknowledgement and retry.
 * NOTES : Existing-object wakes only; missing dynamic objects are reported rejected.
 * ARGS : Loader, optional transport and whether the adapter is enabled.
 * RETURN : Nonzero when this frame queues an encounter packet.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopEncountersUpdate(const moduleApi_t* loader, const onlineApi_t* online, int enabled)
{
	encounterEvent_t packet;
	u32 now = encounterTicks();
	int role = online ? online->role() : 0, size, i;
	if (currentMap != Level || currentMission != Mission->ActiveMission) {
		if (currentMap != Level)
			installed = 0;
		coopEncountersReset(loader);
		currentMap = Level;
		currentMission = Mission->ActiveMission;
	}
	if (!enabled || loader->context != DL_CONTEXT_CAMPAIGN || Level != 22) {
		coopEncountersReset(loader);
		return 0;
	}
	setHooks(loader, 1);
	if (!installed)
		return 0;
	if (role != currentRole) {
		currentRole = role;
		acknowledged = pendingWake = pendingAck = 0;
		epoch = role == ONLINE_HOST ? now | 1 : 0;
		receivedSequence = lastSent = 0;
		if (role == ONLINE_JOIN)
			recorded = 0;
	}
	if (!role || !coopWorldReady())
		return 0;
	size = online->receive(ENCOUNTER_CHANNEL, &packet, sizeof(packet));
	if (size >= 0x18 && packet.magic == ENCOUNTER_MAGIC && packet.level == Level && packet.mission == Mission->ActiveMission && packet.epoch) {
		if (role == ONLINE_HOST && size == 0x18 && packet.epoch == epoch && packet.sequence == (u32)acknowledged + 1 &&
		    (packet.kind == ENCOUNTER_ACK || packet.kind == (ENCOUNTER_ACK | ENCOUNTER_REJECT)) && acknowledged < recorded) {
			++acknowledged;
			rejected += (packet.kind & ENCOUNTER_REJECT) != 0;
			lastSent = 0;
		} else if (role == ONLINE_JOIN && size == sizeof(packet) && packet.kind == ENCOUNTER_WAKE &&
		    coopObjectsEnemyClass(packet.oClass) && packet.uid >= 0 && packet.controller >= 0 &&
		    packet.wave >= -1 && packet.reference >= -1 && packet.reference < 8 &&
		    (packet.reference < 0 || packet.wave >= 0) && packet.placed <= 1) {
			for (i = 0; i < 3; ++i)
				if (!(packet.position[i] >= 0 && packet.position[i] <= 1024) ||
				    !(packet.rotation[i] >= -100 && packet.rotation[i] <= 100))
					return 0;
			if (!epoch) {
				epoch = packet.epoch;
				receivedSequence = 0;
			}
			if (packet.epoch == epoch && packet.sequence == receivedSequence && !pendingWake) {
				pendingAck = pending.kind;
			} else if (packet.epoch == epoch && packet.sequence == receivedSequence + 1 && !pendingWake && !pendingAck) {
				pending = packet;
				pendingWake = 1;
				receivedTime = now;
			}
		}
	}
	if (role == ONLINE_JOIN) {
		if (pendingWake && now - receivedTime >= ENCOUNTER_TIMEOUT) {
			pendingWake = 0;
			receivedSequence = pending.sequence;
			pendingAck = ENCOUNTER_ACK | ENCOUNTER_REJECT;
			pending.kind = pendingAck;
			++rejected;
		}
		if (!pendingAck)
			return 0;
		pending.kind = pendingAck;
		if (online->send(ENCOUNTER_CHANNEL, &pending, 0x18) != 0)
			return 0;
		pendingAck = 0;
		return 1;
	}
	if (acknowledged == recorded || (lastSent && now - lastSent < ENCOUNTER_RETRY))
		return 0;
	packet = history[acknowledged];
	packet.magic = ENCOUNTER_MAGIC;
	packet.level = Level;
	packet.mission = Mission->ActiveMission;
	packet.epoch = epoch;
	packet.sequence = acknowledged + 1;
	packet.kind = ENCOUNTER_WAKE;
	if (online->send(ENCOUNTER_CHANNEL, &packet, sizeof(packet)) != 0)
		return 0;
	lastSent = now;
	return 1;
}
