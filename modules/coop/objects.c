#include <librac4/moby.h>
#include "objects.h"
#include <librac4/game.h>
#include <librac4/map.h>
#include <librac4/interop.h>

#define OBJECT_CHANNEL (0x444c434f)
#define OBJECT_MAGIC (0x324f4344)
#define OBJECT_LIMIT (128)
#define OBJECT_BATCH (2)
#define OBJECT_INTERVAL (5000000)
#define OBJECT_RETRY (15000000)
#define OBJECT_REFRESH (600000000)
#define OBJECT_STREAM_INTERVAL (10000000)
#define OBJECT_REMOVED (1)
#define OBJECT_NO_HEALTH (2)
#define OBJECT_UNSUPPORTED (4)
#define WORD(address) (*(volatile u32*)(address))

typedef enum objectPacketKind {
	OBJECT_DATA = 1,
	OBJECT_ACK,
	OBJECT_END,
	OBJECT_FIRST,
	OBJECT_STREAM
} objectPacketKind_e;

typedef struct objectState {
	/* 0x00 */ s16 uid;
	/* 0x02 */ u16 oClass;
	/* 0x04 */ u8 generation;
	/* 0x05 */ s8 state;
	/* 0x06 */ u8 flags;
	/* 0x07 */ u8 collision;
	/* 0x08 */ float position[3];
	/* 0x14 */ float rotation[3];
	/* 0x20 */ float health;
	/* 0x24 */ u16 mode;
	/* 0x26 */ u16 slot;
	/* 0x28 */ float animTime;
	/* 0x2c */ float animSpeed;
	/* 0x30 */ u8 animId;
	/* 0x31 */ u8 animFlags;
	/* 0x32 */ u16 reserved;
} objectState_t;

typedef struct objectPacket {
	/* 0x00 */ u32 magic;
	/* 0x04 */ int level;
	/* 0x08 */ int mission;
	/* 0x0c */ u32 epoch;
	/* 0x10 */ u32 sequence;
	/* 0x14 */ u16 count;
	/* 0x16 */ u16 kind;
	/* 0x18 */ objectState_t objects[OBJECT_BATCH];
} objectPacket_t;

typedef struct trackedObject {
	/* 0x00 */ u32 moby;
	/* 0x04 */ u32 original;
	/* 0x08 */ u32 target;
	/* 0x0c */ u8 localGeneration;
	/* 0x0d */ s8 localState;
	/* 0x0e */ u16 localMode;
	/* 0x10 */ u32 localCollision;
	/* 0x14 */ float localHealth;
	/* 0x18 */ objectState_t state;
	/* 0x4c */ objectState_t acknowledged;
	/* 0x80 */ int acknowledgedValid;
	/* 0x84 */ int animationPending;
	/* 0x88 */ u32 lastStream;
} trackedObject_t;

static void replicaUpdate(void* moby);
static void applyReplica(void* moby, int animate);

static trackedObject_t tracked[OBJECT_LIMIT];
static objectPacket_t pending, incomingPacket, outgoingPacket;
static int incomingBytes, outgoingBytes;
static int trackedCount, cursor, waiting, baseline, peerRole, map, mission;
static u32 epoch, sequence, receivedSequence, lastSend, nextSend;
static u32 applied, missing, cycleMissing;
static int pendingNextCursor, streamCursor;
static u32 nextStream;
static u16 pendingIndices[OBJECT_BATCH];
static u16 rejectedMask;

/*
 * NAME : objectTicks
 * DESCRIPTION : Read the EE clock for snapshot pacing and retransmission.
 * NOTES :
 * ARGS :
 * RETURN : Wrapping EE count.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 objectTicks(void)
{
	u32 value;
	__asm__ volatile("mfc0 %0, $9" : "=r"(value));
	return value;
}

/*
 * NAME : coopObjectsEnemyClass
 * DESCRIPTION : Identify enemy classes adapted for host-owned encounters.
 * NOTES :
 * ARGS : Native object class.
 * RETURN : Nonzero for a supported enemy.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopObjectsEnemyClass(u16 oClass)
{
	return oClass == 0x099c || oClass == 0x20f6 || oClass == 0x2130;
}

/*
 * NAME : supportedClass
 * DESCRIPTION : Select the initial Catacrom replica classes.
 * NOTES : Vehicles, projectiles and scripted platforms need separate adapters.
 * ARGS : Native object class.
 * RETURN : Nonzero for a supported enemy, pickup or destructible.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int supportedClass(u16 oClass)
{
	if (coopObjectsEnemyClass(oClass))
		return 1;
	switch (oClass) {
		case 0x01f5: /* Health container. */
		case 0x0326: /* Health orb. */
		case 0x01f7: /* Bolt crate. */
		case 0x01ff: /* Ammo crate. */
		case 0x2639: /* Destructible barricade. */
			return 1;
	}
	return 0;
}

/*
 * NAME : coopObjectsBounds
 * DESCRIPTION : Validate the current native object pool.
 * NOTES : Never follow a pointer received over the network.
 * ARGS : Output start and end addresses.
 * RETURN : Nonzero for a bounded pool of complete moby records.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopObjectsBounds(u32* start, u32* end)
{
	u32 a = GetAddressImmediate(&vaCoopMobyListStart), b = GetAddressImmediate(&vaCoopMobyListEnd);
	if (!a || !b)
		return 0;
	*start = WORD(a);
	*end = WORD(b);
	return *start >= 0x100000 && *end >= *start && *end <= 0x2000000 &&
	    *end - *start <= 0x100000 && !((*end - *start) & 255);
}

/*
 * NAME : targetVars
 * DESCRIPTION : Read target variables for the supported non-player classes.
 * NOTES : This is the native MB_getTargetVars path for mode bit 0x20.
 * ARGS : Validated local moby address.
 * RETURN : Local target-variable pointer, or zero.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 targetVars(u32 moby)
{
	u32 vars = WORD(moby + 0xac), target;
	if (!(*(u16*)(moby + 0x34) & 0x20) || vars < 0x100000 || vars > 0x1fffffc)
		return 0;
	target = WORD(vars);
	return target >= 0x100000 && target <= 0x1ffff70 ? target : 0;
}

/*
 * NAME : setAnimation
 * DESCRIPTION : Apply a host animation using the local model's native sequence.
 * NOTES : Call only from the native moby update; the decoder uses scratch memory.
 * ARGS : Local moby and validated animation state.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void setAnimation(u32 moby, const objectState_t* state)
{
	u32 model = WORD(moby + 0x24), sequence, snap = GetAddressImmediate(&vaCoopMobySnapAnimation);
	if (state->animId == 255 || !snap || WORD(snap) != 0x27bdfff0 ||
	    model < 0x100000 || model > 0x1fffb00 || state->animId >= *(u8*)(model + 0x0c))
		return;
	sequence = WORD(model + 0x48 + state->animId * 4);
	if (sequence < 0x100000 || sequence > 0x1ffffe0 ||
	    !(state->animTime >= 0 && state->animTime < *(u8*)(sequence + 0x10)))
		return;
	if (*(u8*)(moby + 0x54) != state->animId || WORD(moby + 0x40) != sequence) {
		((void (*)(void*, int, float))snap)((void*)moby, state->animId, state->animTime);
	} else if (state->animSpeed == 0) {
		/* Moving animations play locally between packets. Only a held pose
		 * needs its frame reapplied without changing the sequence. */
		*(float*)(moby + 0x44) = state->animTime;
	}
	*(float*)(moby + 0x48) = state->animSpeed;
	*(u8*)(moby + 0x55) = state->animFlags;
}

/*
 * NAME : sameObject
 * DESCRIPTION : Reject recycled native object storage.
 * NOTES : Local allocation generations are separate from the host's generations.
 * ARGS : Tracked local object.
 * RETURN : Nonzero when its identity is unchanged.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int sameObject(const trackedObject_t* object)
{
	u32 moby = object->moby;
	return *(s16*)(moby + 0xb2) == object->state.uid &&
	    *(u16*)(moby + 0xbc) == object->state.oClass &&
	    *(u8*)(moby + 0x2d) == object->localGeneration;
}

/*
 * NAME : coopObjectsReset
 * DESCRIPTION : Release owned replica callbacks before a disconnect or map load.
 * NOTES : Never restore old actor data into a newly loaded map.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopObjectsReset(void)
{
	u32 start, end;
	int i;
	if (map == Level && mission == Mission->ActiveMission && coopObjectsBounds(&start, &end)) {
		for (i = 0; i < trackedCount; ++i) {
			trackedObject_t* object = &tracked[i];
			u32 moby = object->moby;
			if (moby < start || moby >= end || WORD(moby + 0xa8) != (u32)replicaUpdate)
				continue;
			/* A replaced class normally installs its own callback. If it did
			 * not, stop it instead of leaving a pointer into an unloaded mod. */
			if (sameObject(object)) {
				*(s8*)(moby + 0x20) = object->localState;
				*(u16*)(moby + 0x34) = (*(u16*)(moby + 0x34) & ~0x87) | (object->localMode & 0x87);
				WORD(moby + 0x9c) = object->localCollision;
				if (object->target && targetVars(moby) == object->target)
					*(float*)object->target = object->localHealth;
				WORD(moby + 0xa8) = object->original;
			} else {
				WORD(moby + 0xa8) = 0;
				*(u16*)(moby + 0x34) |= 2;
			}
		}
	}
	trackedCount = cursor = waiting = baseline = streamCursor = 0;
	nextStream = 0;
	peerRole = 0;
	epoch = sequence = receivedSequence = 0;
	applied = missing = cycleMissing = 0;
	incomingBytes = outgoingBytes = 0;
}

/*
 * NAME : replicaUpdate
 * DESCRIPTION : Display the host's object without running local enemy decisions.
 * NOTES : The native post-update path still handles transforms and collision grids.
 * ARGS : Native moby.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void replicaUpdate(void* pointer)
{
	applyReplica(pointer, 1);
}

/*
 * NAME : applyReplica
 * DESCRIPTION : Apply replica fields, deferring animation decoding to native updates.
 * NOTES : Network receipt may interrupt rendering; it must not invoke the decoder.
 * ARGS : Local moby and whether this is its native update callback.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void applyReplica(void* pointer, int animate)
{
	u32 moby = (u32)pointer;
	int i, j;
	float distance, delta, scale;
	for (i = 0; i < trackedCount; ++i) {
		trackedObject_t* object = &tracked[i];
		objectState_t* state = &object->state;
		if (object->moby != moby)
			continue;
		if (!sameObject(object))
			return;
		if (state->flags & OBJECT_REMOVED) {
			object->animationPending = 0;
			*(u16*)(moby + 0x34) |= 0x80;
			WORD(moby + 0x9c) = 0;
			if (object->target && targetVars(moby) == object->target)
				*(float*)object->target = 0;
			return;
		}
		distance = 0;
		for (j = 0; j < 3; ++j) {
			delta = state->position[j] - ((float*)(moby + 0x10))[j];
			distance += delta * delta;
		}
		scale = distance > 100 ? 1 : animate ? 0.35f : 0;
		for (j = 0; j < 3; ++j) {
			((float*)(moby + 0x10))[j] += (state->position[j] - ((float*)(moby + 0x10))[j]) * scale;
			if (animate)
				((float*)(moby + 0xf0))[j] = state->rotation[j];
		}
		/* Dormant enemies disable post-update. Restore the host flag too so
		 * native transforms and drawing bounds follow the replicated position. */
		*(s8*)(moby + 0x20) = state->state;
		*(u16*)(moby + 0x34) = (*(u16*)(moby + 0x34) & ~0x87) | (state->mode & 0x87);
		WORD(moby + 0x9c) = state->collision;
		if (object->target && targetVars(moby) == object->target && !(state->flags & OBJECT_NO_HEALTH))
			*(float*)object->target = state->health;
		if (animate && object->animationPending) {
			object->animationPending = 0;
			setAnimation(moby, state);
		}
		return;
	}
}

/*
 * NAME : captureObject
 * DESCRIPTION : Capture pointer-free state, retaining tombstones for reused slots.
 * NOTES : Only the host runs the original damage, reward and object update code.
 * ARGS : Host object record.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void captureObject(trackedObject_t* object)
{
	objectState_t* state = &object->state;
	u32 moby = object->moby, target;
	int j;
	if (!sameObject(object) || *(s8*)(moby + 0x20) < 0) {
		state->flags = OBJECT_REMOVED;
		if (*(s16*)(moby + 0xb2) == state->uid && *(u16*)(moby + 0xbc) != state->oClass)
			state->flags |= OBJECT_UNSUPPORTED;
		return;
	}
	target = targetVars(moby);
	state->flags = target ? 0 : OBJECT_NO_HEALTH;
	state->state = *(s8*)(moby + 0x20);
	state->generation = *(u8*)(moby + 0x2d);
	state->mode = *(u16*)(moby + 0x34);
	state->collision = WORD(moby + 0x9c) != 0;
	state->health = target ? *(float*)target : 0;
	state->animTime = *(float*)(moby + 0x44);
	state->animSpeed = *(float*)(moby + 0x48);
	state->animId = coopObjectsEnemyClass(state->oClass) && WORD(moby + 0x40) ? *(u8*)(moby + 0x54) : 255;
	state->animFlags = *(u8*)(moby + 0x55);
	if (state->animId == 255) {
		state->animTime = state->animSpeed = 0;
		state->animFlags = 0;
	}
	state->reserved = 0;
	for (j = 0; j < 3; ++j) {
		state->position[j] = ((float*)(moby + 0x10))[j];
		state->rotation[j] = ((float*)(moby + 0xf0))[j];
	}
}

/*
 * NAME : discoverObjects
 * DESCRIPTION : Register supported host objects without changing native storage.
 * NOTES : Ambiguous UID/class pairs are rejected by the receiver.
 * ARGS : Validated local pool bounds.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void discoverObjects(u32 start, u32 end)
{
	u32 moby;
	int i;
	for (moby = start; moby < end && trackedCount < OBJECT_LIMIT; moby += 0x100) {
		u16 oClass = *(u16*)(moby + 0xbc);
		if (*(s8*)(moby + 0x20) == -1 || *(s16*)(moby + 0xb2) < 0 || !supportedClass(oClass))
			continue;
		for (i = 0; i < trackedCount; ++i)
			if (tracked[i].moby == moby && sameObject(&tracked[i]))
				break;
		if (i != trackedCount)
			continue;
		/* Dormant enemies keep native setup until the host wakes them. */
		if (coopObjectsEnemyClass(oClass) && (*(u16*)(moby + 0x34) & 3) == 3)
			continue;
		tracked[i].moby = moby;
		tracked[i].localGeneration = *(u8*)(moby + 0x2d);
		tracked[i].state.uid = *(s16*)(moby + 0xb2);
		tracked[i].state.oClass = oClass;
		tracked[i].state.slot = (moby - start) >> 8;
		tracked[i].acknowledgedValid = 0;
		tracked[i].lastStream = 0;
		tracked[i].original = 0;
		++trackedCount;
		captureObject(&tracked[i]);
	}
}

/*
 * NAME : applyObject
 * DESCRIPTION : Bind a host record to one unambiguous, matching local object.
 * NOTES : No allocation, raw pVar copying, class replacement or reward replay.
 * ARGS : Host object state and validated local pool bounds.
 * RETURN : Nonzero when the record can be represented locally.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int applyObject(const objectState_t* state, u32 start, u32 end)
{
	u32 moby, found = 0;
	trackedObject_t* object;
	int i, j;
	if (!supportedClass(state->oClass) || state->uid < 0 ||
	    !(state->health >= 0 && state->health <= 10000000) ||
	    !(state->animTime >= 0 && state->animTime <= 255) ||
	    !(state->animSpeed >= -32 && state->animSpeed <= 32))
		return 0;
	for (j = 0; j < 3; ++j)
		if (!(state->position[j] >= 0 && state->position[j] <= 1024) ||
		    !(state->rotation[j] >= -100 && state->rotation[j] <= 100))
			return 0;
	for (moby = start; moby < end; moby += 0x100) {
		if (*(s16*)(moby + 0xb2) != state->uid || *(u16*)(moby + 0xbc) != state->oClass)
			continue;
		if (!state->uid) {
			/* Static health orbs share UID zero. Their map slot and spawn
			 * position must agree before that slot becomes a replica. */
			if (state->oClass == 0x0326 && state->slot == (moby - start) / 0x100) {
				found = moby;
				break;
			}
		} else {
			if (found)
				return 0;
			found = moby;
		}
	}
	if (!found || *(s8*)(found + 0x20) < 0)
		return (state->flags & OBJECT_REMOVED) != 0;
	for (i = 0; i < trackedCount; ++i)
		if (tracked[i].moby == found)
			break;
	if (i == trackedCount) {
		if (!state->uid) {
			float distance = 0;
			for (j = 0; j < 3; ++j) {
				float delta = state->position[j] - ((float*)(found + 0x10))[j];
				distance += delta * delta;
			}
			if (!(distance <= 1))
				return 0;
		}
		if (trackedCount == OBJECT_LIMIT || WORD(found + 0xa8) < 0x100000 || WORD(found + 0xa8) >= 0x2000000)
			return 0;
		object = &tracked[trackedCount++];
		object->moby = found;
		object->original = WORD(found + 0xa8);
		object->localGeneration = *(u8*)(found + 0x2d);
		object->localState = *(s8*)(found + 0x20);
		object->localMode = *(u16*)(found + 0x34);
		object->localCollision = WORD(found + 0x9c);
		object->target = targetVars(found);
		object->localHealth = object->target ? *(float*)object->target : 0;
	} else {
		object = &tracked[i];
		if (!sameObject(object) || object->state.slot != state->slot ||
		    object->state.generation != state->generation || WORD(found + 0xa8) != (u32)replicaUpdate)
			return 0;
	}
	object->target = targetVars(found);
	object->state = *state;
	WORD(found + 0xa8) = (u32)replicaUpdate;
	/* Apply the snapshot even while native distance culling skips this object. */
	object->animationPending = 1;
	applyReplica((void*)found, 0);
	++applied;
	return !(state->flags & OBJECT_UNSUPPORTED);
}

/*
 * NAME : objectChanged
 * DESCRIPTION : Send changed state instead of repeatedly transmitting idle objects.
 * NOTES : Health and lifecycle changes are exact; small position noise is ignored.
 * ARGS : Captured host record and its last acknowledged state.
 * RETURN : Nonzero when an update is needed.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int objectChanged(const trackedObject_t* object)
{
	const objectState_t* a = &object->state;
	const objectState_t* b = &object->acknowledged;
	float position = 0, rotation = 0, delta;
	int i;
	if (!object->acknowledgedValid || a->flags != b->flags || a->state != b->state ||
	    a->generation != b->generation || a->health != b->health ||
	    (a->mode & 0x87) != (b->mode & 0x87) || a->collision != b->collision ||
	    a->animId != b->animId || a->animFlags != b->animFlags || a->animSpeed != b->animSpeed ||
	    a->animTime < b->animTime || a->animTime - b->animTime >= 1)
		return 1;
	for (i = 0; i < 3; ++i) {
		delta = a->position[i] - b->position[i];
		position += delta * delta;
		delta = a->rotation[i] - b->rotation[i];
		rotation += delta * delta;
	}
	return position > 0.0025f || rotation > 0.0001f;
}

/*
 * NAME : coopObjectsReceive
 * DESCRIPTION : Stage enemy data carried by the newest player snapshot.
 * NOTES : Object sequence checks discard stale or duplicated payloads.
 * ARGS : Received payload and byte count.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopObjectsReceive(const void* data, int bytes)
{
	int i;
	if (!data || bytes < 0x18 || bytes > (int)sizeof(incomingPacket))
		return;
	for (i = 0; i < bytes; ++i)
		((u8*)&incomingPacket)[i] = ((const u8*)data)[i];
	incomingBytes = bytes;
}

/*
 * NAME : coopObjectsWrite
 * DESCRIPTION : Attach queued enemy data to a player snapshot.
 * NOTES : Repeat it until replaced, since unsent player snapshots may coalesce.
 * ARGS : Output payload buffer.
 * RETURN : Queued byte count.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopObjectsWrite(void* data)
{
	int i;
	for (i = 0; i < outgoingBytes; ++i)
		((u8*)data)[i] = ((u8*)&outgoingPacket)[i];
	return outgoingBytes;
}

/*
 * NAME : queuePacket
 * DESCRIPTION : Queue enemy state without consuming a separate network slot.
 * NOTES : Unsent snapshots retain only the newest object payload.
 * ARGS : Packet and byte count.
 * RETURN : Zero when staged.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int queuePacket(const objectPacket_t* packet, int bytes)
{
	int i;
	for (i = 0; i < bytes; ++i)
		((u8*)&outgoingPacket)[i] = ((const u8*)packet)[i];
	outgoingBytes = bytes;
	return 0;
}

/*
 * NAME : coopObjectsReady
 * DESCRIPTION : Report whether a complete object pass was accepted by the guest.
 * NOTES : Missing/type-changed objects keep the baseline incomplete.
 * ARGS :
 * RETURN : Nonzero after a complete accepted pass.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopObjectsReady(void)
{
	return baseline && peerRole == ONLINE_JOIN && map == Level && mission == Mission->ActiveMission;
}

/*
 * NAME : coopObjectsUpdate
 * DESCRIPTION : Exchange acknowledged host snapshots alongside player input.
 * NOTES : Two animated records share each player snapshot within the existing datagram.
 * ARGS : Loader, transport, enable/ready flags and outgoing-slot availability.
 * RETURN : Nonzero when an object packet owns this frame's outgoing slot.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopObjectsUpdate(const moduleApi_t* loader, const onlineApi_t* online, int enabled, int ready, int canSend)
{
	objectPacket_t packet;
	u32 start, end, now = objectTicks();
	int role, size, i;
	packet = incomingPacket;
	size = incomingBytes;
	incomingBytes = 0;
	if (!enabled || loader->context != DL_CONTEXT_CAMPAIGN || !online || !(role = online->role()) ||
	    Level != 22 || Mission->Loading) {
		coopObjectsReset();
		return 0;
	}
	if (peerRole != role || map != Level || mission != Mission->ActiveMission) {
		coopObjectsReset();
		peerRole = role;
		map = Level;
		mission = Mission->ActiveMission;
		epoch = role == ONLINE_HOST ? now | 1 : 0;
		nextSend = now;
	}
	if (!ready || !coopObjectsBounds(&start, &end))
		return 0;
	if (size >= 0x18 && packet.magic == OBJECT_MAGIC && packet.level == Level && packet.mission == Mission->ActiveMission &&
	    packet.count <= OBJECT_BATCH && size == (int)(0x18 + packet.count * sizeof(objectState_t))) {
		if (role == ONLINE_HOST && waiting && (packet.kind & 255) == OBJECT_ACK && packet.count == 0 &&
		    packet.epoch == epoch && packet.sequence == pending.sequence) {
			for (i = 0; i < pending.count; ++i) {
				if (!(packet.kind & (1 << (8 + i)))) {
					tracked[pendingIndices[i]].acknowledged = pending.objects[i];
					tracked[pendingIndices[i]].acknowledgedValid = 1;
				}
			}
			cursor = pendingNextCursor;
			if (pending.kind == OBJECT_END) {
				cursor = 0;
				baseline = 1;
			}
			waiting = 0;
		} else if (role == ONLINE_JOIN && packet.epoch && packet.sequence &&
		    (packet.kind == OBJECT_DATA || packet.kind == OBJECT_FIRST || packet.kind == OBJECT_END || packet.kind == OBJECT_STREAM)) {
			if (epoch != packet.epoch) {
				coopObjectsReset();
				peerRole = role;
				map = Level;
				mission = Mission->ActiveMission;
				epoch = packet.epoch;
			}
			if ((int)(packet.sequence - receivedSequence) > 0) {
				rejectedMask = 0;
				if (packet.kind == OBJECT_FIRST)
					cycleMissing = 0;
				for (i = 0; i < packet.count; ++i)
					if (!applyObject(&packet.objects[i], start, end)) {
						++missing;
						baseline = 0;
						++cycleMissing;
						rejectedMask |= 1 << (8 + i);
					}
				if (packet.kind == OBJECT_END)
					baseline = cycleMissing == 0;
				receivedSequence = packet.sequence;
			}
			if (packet.kind != OBJECT_STREAM) {
				pending = packet;
				pending.kind = OBJECT_ACK | rejectedMask;
				pending.count = 0;
				waiting = 1;
			}
		}
	}
	if (!canSend)
		return 0;
	if (role == ONLINE_JOIN) {
		if (!waiting)
			return 0;
		if (queuePacket(&pending, 0x18) != 0)
			return 0;
		waiting = 0;
		return 1;
	}
	if (baseline && !waiting) {
		/* Acknowledgements establish the baseline. Ongoing snapshots repeat
		 * changed/recent state without a round trip for each small batch. */
		if ((int)(now - nextStream) < 0)
			return 0;
		if (!streamCursor)
			discoverObjects(start, end);
		packet.magic = OBJECT_MAGIC;
		packet.level = Level;
		packet.mission = Mission->ActiveMission;
		packet.epoch = epoch;
		packet.sequence = sequence + 1;
		packet.kind = OBJECT_STREAM;
		packet.count = 0;
		for (i = streamCursor; i < trackedCount && packet.count < OBJECT_BATCH; ++i) {
			captureObject(&tracked[i]);
			if (!objectChanged(&tracked[i]) && now - tracked[i].lastStream < OBJECT_REFRESH)
				continue;
			pendingIndices[packet.count] = i;
			packet.objects[packet.count++] = tracked[i].state;
		}
		if (!packet.count) {
			streamCursor = 0;
			return 0;
		}
		if (queuePacket(&packet, 0x18 + packet.count * sizeof(objectState_t)) != 0)
			return 0;
		++sequence;
		streamCursor = i == trackedCount ? 0 : i;
		nextStream = now + OBJECT_STREAM_INTERVAL;
		for (i = 0; i < packet.count; ++i) {
			trackedObject_t* object = &tracked[pendingIndices[i]];
			object->acknowledged = packet.objects[i];
			object->acknowledgedValid = 1;
			object->lastStream = now;
		}
		return 1;
	}
	if (waiting) {
		if (now - lastSend < OBJECT_RETRY)
			return 0;
	} else {
		if ((int)(now - nextSend) < 0)
			return 0;
		if (!cursor)
			discoverObjects(start, end);
		pending.magic = OBJECT_MAGIC;
		pending.level = Level;
		pending.mission = Mission->ActiveMission;
		pending.epoch = epoch;
		pending.sequence = ++sequence;
		pending.count = 0;
		pending.kind = cursor == trackedCount ? OBJECT_END : cursor ? OBJECT_DATA : OBJECT_FIRST;
		for (i = cursor; i < trackedCount && pending.count < OBJECT_BATCH; ++i) {
			captureObject(&tracked[i]);
			if (!objectChanged(&tracked[i]))
				continue;
			pendingIndices[pending.count] = i;
			pending.objects[pending.count++] = tracked[i].state;
		}
		pendingNextCursor = i;
		if (!pending.count && pending.kind != OBJECT_END) {
			cursor = 0;
			nextSend = now + OBJECT_INTERVAL;
			return 0;
		}
		waiting = 1;
	}
	lastSend = now;
	nextSend = now + OBJECT_INTERVAL;
	return queuePacket(&pending, 0x18 + pending.count * sizeof(objectState_t)) == 0;
}
