#include "objects.h"
#include "bladeball.h"
#include <librac4/moby.h>
#include <librac4/game.h>
#include <librac4/map.h>
#include <librac4/interop.h>
#include <string.h>

#define OBJECT_CHANNEL (0x444c434f)
#define EVENT_MAGIC (0x34454344)
#define MOTION_MAGIC (0x344d4344)
#define OBJECT_LIMIT (128)
#define MOTION_BATCH (4)
#define EVENT_RETRY (22118400)
#define MOTION_INTERVAL (4915200)
#define INTERPOLATION_MIN (4915200)
#define INTERPOLATION_MAX (22118400)
#define WORD(address) (*(volatile u32*)(address))

typedef enum objectEventKind {
	OBJECT_SYNC = 1,
	OBJECT_CREATE,
	OBJECT_STATE,
	OBJECT_REMOVE,
	OBJECT_ACK
} objectEventKind_e;

typedef struct objectMotion {
	/* 0x00 */ u16 id;
	/* 0x02 */ u16 animTime;
	/* 0x04 */ float position[3];
	/* 0x10 */ s16 rotation[3];
	/* 0x16 */ u8 animId;
	/* 0x17 */ u8 animFlags;
	/* 0x18 */ s16 animSpeed;
	/* 0x1a */ u16 reserved;
} objectMotion_t;

typedef struct objectEvent {
	/* 0x00 */ u32 magic;
	/* 0x04 */ u32 epoch;
	/* 0x08 */ int level;
	/* 0x0c */ int mission;
	/* 0x10 */ u32 revision;
	/* 0x14 */ u16 id;
	/* 0x16 */ s16 uid;
	/* 0x18 */ u16 oClass;
	/* 0x1a */ u8 generation;
	/* 0x1b */ u8 kind;
	/* 0x1c */ s8 state;
	/* 0x1d */ u8 collision;
	/* 0x1e */ u16 mode;
	/* 0x20 */ float health;
	/* 0x24 */ objectMotion_t motion;
} objectEvent_t;

typedef struct objectMotionPacket {
	/* 0x00 */ u32 magic;
	/* 0x04 */ u32 epoch;
	/* 0x08 */ u32 sequence;
	/* 0x0c */ u16 count;
	/* 0x0e */ u16 reserved;
	/* 0x10 */ objectMotion_t objects[MOTION_BATCH];
} objectMotionPacket_t;

typedef struct replicatedObject {
	/* 0x00 */ u32 moby;
	/* 0x04 */ u32 original;
	/* 0x08 */ s16 uid;
	/* 0x0a */ u16 originalMode;
	/* 0x0c */ u8 generation;
	/* 0x0d */ u8 allocated;
	/* 0x0e */ u8 acknowledged;
	/* 0x0f */ u8 created;
	/* 0x10 */ u32 revision;
	/* 0x14 */ u32 lastEvent;
	/* 0x18 */ u32 received;
	/* 0x1c */ u32 interval;
	/* 0x20 */ bladeballState_t state;
	/* 0x4c */ bladeballState_t previous;
	/* 0x78 */ u32 motionSequence;
	/* 0x7c */ u8 originalDistance;
	/* 0x7d */ u8 reserved[3];
} replicatedObject_t;

typedef char objectMotionSize[(sizeof(objectMotion_t) == 0x1c) ? 1 : -1];
typedef char objectEventSize[(sizeof(objectEvent_t) == 0x40) ? 1 : -1];
typedef char objectMotionFits[(sizeof(objectMotionPacket_t) == COOP_OBJECT_BYTES) ? 1 : -1];
typedef char replicatedObjectSize[(sizeof(replicatedObject_t) == 0x80) ? 1 : -1];

static void replicaUpdate(void* pointer);
static void trackHost(u32 moby, u32 start, u32 end);

static replicatedObject_t objects[OBJECT_LIMIT];
static objectMotionPacket_t receivedMotion, outgoingMotion;
static int receivedBytes, outgoingBytes;
static int currentRole, currentLevel, currentMission, objectCount, eventCursor, motionCursor, synchronized;
static u32 epoch, sequence, lastSync, nextMotion;

/*
 * NAME : objectTicks
 * DESCRIPTION : Read the wrapping EE clock without depending on gameplay time.
 * NOTES :
 * ARGS :
 * RETURN : EE Count ticks.
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
 * DESCRIPTION : Preserve encounter wake classification independently of adapter support.
 * NOTES : Only Blade Balls have a replication adapter in this first rewrite.
 * ARGS : Native class ID.
 * RETURN : Nonzero for an encounter-controlled enemy.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopObjectsEnemyClass(u16 oClass)
{
	return oClass == MOBY_ID_BLADEBALL || oClass == MOBY_ID_ROBOT_ZOMBIE || oClass == MOBY_ID_DZ_STRIKER_LEGS;
}

/*
 * NAME : coopObjectsBounds
 * DESCRIPTION : Validate the current native moby pool before following local records.
 * NOTES : Peer packets never supply native pointers.
 * ARGS : Start and end outputs.
 * RETURN : Nonzero for a complete bounded pool.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopObjectsBounds(u32* start, u32* end)
{
	u32 a = GetAddressImmediate(&vaCoopMobyListStart), b = GetAddressImmediate(&vaCoopMobyListEnd);
	if (!a || !b)
		return 0;
	*start = WORD(a);
	*end = WORD(b);
	return *start >= 0x00100000 && *end >= *start && *end <= 0x02000000 &&
	    *end - *start <= 0x00100000 && !((*end - *start) & 255);
}

/*
 * NAME : sameLifetime
 * DESCRIPTION : Reject reused native slots before reading or restoring their state.
 * NOTES : The network ID is never reused within an epoch.
 * ARGS : Registry entry and validated pool bounds.
 * RETURN : Nonzero for the bound native lifetime.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int sameLifetime(const replicatedObject_t* object, u32 start, u32 end)
{
	u32 moby = object->moby;
	return object->allocated && moby >= start && moby < end &&
	    *(u16*)(moby + 0xbc) == MOBY_ID_BLADEBALL && *(s16*)(moby + 0xb2) == object->uid &&
	    *(u8*)(moby + 0x2d) == object->generation;
}

/*
 * NAME : coopObjectsReset
 * DESCRIPTION : Restore owned guest callbacks and discard the previous session registry.
 * NOTES : Recycled slots and replacement callbacks are never overwritten.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopObjectsReset(void)
{
	u32 start, end;
	int i;
	if (currentLevel == Level && currentMission == Mission->ActiveMission && coopObjectsBounds(&start, &end)) {
		for (i = 0; i < OBJECT_LIMIT; ++i) {
			replicatedObject_t* object = &objects[i];
			if (sameLifetime(object, start, end) && object->original &&
			    WORD(object->moby + 0xa8) == (u32)replicaUpdate) {
				WORD(object->moby + 0xa8) = object->original;
				*(u8*)(object->moby + 0x30) = object->originalDistance;
				*(u16*)(object->moby + 0x34) = (*(u16*)(object->moby + 0x34) & ~0x43) | (object->originalMode & 0x43);
			}
		}
	}
	memset(objects, 0, sizeof(objects));
	currentRole = objectCount = eventCursor = motionCursor = synchronized = 0;
	epoch = sequence = lastSync = nextMotion = 0;
	receivedBytes = outgoingBytes = 0;
}

/*
 * NAME : packMotion
 * DESCRIPTION : Encode transform and animation without private moby memory.
 * NOTES : Position remains full precision; rotation uses wrapped radians at 1/4096 resolution.
 * ARGS : Network ID, state and motion destination.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void packMotion(int id, const bladeballState_t* state, objectMotion_t* motion)
{
	int i;
	memset(motion, 0, sizeof(*motion));
	motion->id = id;
	for (i = 0; i < 3; ++i) {
		float angle = state->rotation[i];
		while (angle > 3.14159265f)
			angle -= 6.2831853f;
		while (angle < -3.14159265f)
			angle += 6.2831853f;
		motion->position[i] = state->position[i];
		motion->rotation[i] = (s16)(angle * 4096);
	}
	motion->animTime = (u16)(state->animTime * 256);
	motion->animSpeed = (s16)(state->animSpeed * 256);
	motion->animId = state->animId;
	motion->animFlags = state->animFlags;
}

/*
 * NAME : unpackMotion
 * DESCRIPTION : Decode and validate only the presentation fields in a motion record.
 * NOTES : Critical state and health are never changed by motion packets.
 * ARGS : Received record and existing state.
 * RETURN : Nonzero for valid fields.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int unpackMotion(const objectMotion_t* motion, bladeballState_t* state)
{
	int i;
	if (motion->reserved)
		return 0;
	for (i = 0; i < 3; ++i) {
		state->position[i] = motion->position[i];
		state->rotation[i] = motion->rotation[i] * (1.0f / 4096);
	}
	state->animTime = motion->animTime * (1.0f / 256);
	state->animSpeed = motion->animSpeed * (1.0f / 256);
	state->animId = motion->animId;
	state->animFlags = motion->animFlags;
	return bladeballStateValid(state);
}

/*
 * NAME : replicaUpdate
 * DESCRIPTION : Interpolate one authoritative object and present its native animation.
 * NOTES : No guest AI, damage decision or reward callback is invoked.
 * ARGS : Local moby supplied by its normal update pass.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void replicaUpdate(void* pointer)
{
	u32 moby = (u32)pointer, now = objectTicks(), start, end;
	int i, j;
	if (!coopObjectsBounds(&start, &end))
		return;
	for (i = 0; i < OBJECT_LIMIT; ++i) {
		replicatedObject_t* object = &objects[i];
		bladeballState_t view;
		float fraction;
		if (object->moby != moby || !sameLifetime(object, start, end))
			continue;
		view = object->state;
		fraction = object->interval ? (float)(now - object->received) / object->interval : 1;
		if (fraction > 1)
			fraction = 1;
		for (j = 0; j < 3; ++j) {
			float angle = view.rotation[j] - object->previous.rotation[j];
			float delta = view.position[j] - object->previous.position[j];
			while (angle > 3.14159265f)
				angle -= 6.2831853f;
			while (angle < -3.14159265f)
				angle += 6.2831853f;
			/* Large corrections are spawn/teleport changes, not a path through geometry. */
			if (delta > -10 && delta < 10)
				view.position[j] = object->previous.position[j] + delta * fraction;
			view.rotation[j] = object->previous.rotation[j] + angle * fraction;
		}
		bladeballPresent(moby, &view, 1);
		return;
	}
}

/*
 * NAME : captureHost
 * DESCRIPTION : Discover Blade Ball lifetimes and retain each critical event until acknowledged.
 * NOTES : Native update/health/reward logic runs exclusively on the host.
 * ARGS : Pool bounds.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void captureHost(u32 start, u32 end)
{
	int i;
	u32 moby;
	for (i = 0; i < objectCount; ++i) {
		replicatedObject_t* object = &objects[i];
		bladeballState_t next = object->state;
		if (!sameLifetime(object, start, end))
			next.removed = 1;
		else if (*(s8*)(object->moby + 0x20) < 0)
			next.removed = 1;
		else if (!bladeballCapture(object->moby, &next))
			continue;
		if (next.removed != object->state.removed || next.state != object->state.state ||
		    next.health != object->state.health || next.collision != object->state.collision ||
		    (next.mode & 0xc7) != (object->state.mode & 0xc7)) {
			if (!++object->revision)
				++object->revision;
			object->acknowledged = 0;
			object->lastEvent = 0;
		}
		object->state = next;
	}
	for (moby = start; moby < end && objectCount < OBJECT_LIMIT; moby += 0x100) {
		if (*(u16*)(moby + 0xbc) != MOBY_ID_BLADEBALL || *(s8*)(moby + 0x20) < 0 ||
		    *(s16*)(moby + 0xb2) <= 0 || (*(u16*)(moby + 0x34) & 3) == 3)
			continue;
		trackHost(moby, start, end);
	}
}

/*
 * NAME : trackHost
 * DESCRIPTION : Register one native lifetime without missing a short-lived wake.
 * NOTES : Explicit wakes may already be inactive by the next discovery pass.
 * ARGS : Moby and current pool bounds.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void trackHost(u32 moby, u32 start, u32 end)
{
	replicatedObject_t* object;
	int i;
	if (objectCount >= OBJECT_LIMIT || moby < start || moby >= end || ((moby - start) & 255) ||
	    *(u16*)(moby + 0xbc) != MOBY_ID_BLADEBALL || *(s16*)(moby + 0xb2) <= 0)
		return;
	for (i = 0; i < objectCount; ++i)
		if (objects[i].moby == moby && sameLifetime(&objects[i], start, end))
			return;
	object = &objects[objectCount];
	if (!bladeballCapture(moby, &object->state))
		return;
	object->allocated = 1;
	object->moby = moby;
	object->uid = *(s16*)(moby + 0xb2);
	object->generation = *(u8*)(moby + 0x2d);
	object->revision = 1;
	++objectCount;
}

/*
 * NAME : coopObjectsWake
 * DESCRIPTION : Track the completed host encounter wake immediately.
 * NOTES : Never records guest decisions or an overlay from another epoch.
 * ARGS : Native moby after wake setup.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopObjectsWake(u32 moby)
{
	u32 start, end;
	if (currentRole == ONLINE_HOST && currentLevel == Level && currentMission == Mission->ActiveMission &&
	    !Mission->Loading && coopObjectsBounds(&start, &end))
		trackHost(moby, start, end);
}

/*
 * NAME : makeEvent
 * DESCRIPTION : Describe one registry lifetime or the session's identity handshake.
 * NOTES : Each critical revision includes a complete initial presentation state.
 * ARGS : Registry ID (zero for session sync) and destination.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void makeEvent(int id, objectEvent_t* event)
{
	replicatedObject_t* object = id ? &objects[id - 1] : 0;
	memset(event, 0, sizeof(*event));
	event->magic = EVENT_MAGIC;
	event->epoch = epoch;
	event->level = currentLevel;
	event->mission = currentMission;
	event->revision = object ? object->revision : 1;
	event->id = id;
	event->kind = object ? object->state.removed ? OBJECT_REMOVE : object->created ? OBJECT_STATE : OBJECT_CREATE : OBJECT_SYNC;
	if (!object)
		return;
	event->uid = object->uid;
	event->oClass = MOBY_ID_BLADEBALL;
	event->generation = object->generation;
	event->state = object->state.state;
	event->collision = object->state.collision;
	event->mode = object->state.mode;
	event->health = object->state.health;
	packMotion(id, &object->state, &event->motion);
}

/*
 * NAME : bindGuest
 * DESCRIPTION : Bind a create event to exactly one locally spawned Blade Ball.
 * NOTES : Missing local spawns are retried through unacknowledged events, never fabricated.
 * ARGS : Registry entry, authoritative identity and pool bounds.
 * RETURN : Nonzero after a safe binding.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int bindGuest(replicatedObject_t* object, const objectEvent_t* event, u32 start, u32 end)
{
	u32 moby, found = 0;
	int i;
	for (moby = start; moby < end; moby += 0x100) {
		if (*(u16*)(moby + 0xbc) != MOBY_ID_BLADEBALL || *(s16*)(moby + 0xb2) != event->uid ||
		    *(s8*)(moby + 0x20) < 0)
			continue;
		if (found)
			return 0;
		found = moby;
	}
	if (!found || WORD(found + 0xa8) < 0x00100000 || WORD(found + 0xa8) >= 0x02000000)
		return 0;
	for (i = 0; i < OBJECT_LIMIT; ++i)
		if (&objects[i] != object && sameLifetime(&objects[i], start, end) && objects[i].moby == found)
			return 0;
	object->moby = found;
	object->original = WORD(found + 0xa8);
	object->originalMode = *(u16*)(found + 0x34);
	object->originalDistance = *(u8*)(found + 0x30);
	object->generation = *(u8*)(found + 0x2d);
	object->uid = event->uid;
	object->allocated = 1;
	WORD(found + 0xa8) = (u32)replicaUpdate;
	/* Host motion must reach the native post-update even outside the guest's update radius. */
	*(u8*)(found + 0x30) = 255;
	return 1;
}

/*
 * NAME : receiveEvent
 * DESCRIPTION : Validate owner-directed critical events and acknowledge applied revisions.
 * NOTES : Motion cannot create objects; removed IDs remain tombstones until reset.
 * ARGS : Transport, pool bounds and current clock.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void receiveEvent(const onlineApi_t* online, u32 start, u32 end, u32 now)
{
	objectEvent_t event;
	replicatedObject_t* object;
	bladeballState_t state;
	int bytes = online->receive(OBJECT_CHANNEL, &event, sizeof(event));
	if (bytes != sizeof(event) || event.magic != EVENT_MAGIC || !event.epoch || !event.revision ||
	    event.level != currentLevel || event.mission != currentMission || event.id > OBJECT_LIMIT)
		return;
	if (currentRole == ONLINE_HOST) {
		if (event.kind != OBJECT_ACK || event.epoch != epoch)
			return;
		if (!event.id) {
			if (event.revision == 1)
				synchronized = 1;
		} else if (event.id <= objectCount && event.revision == objects[event.id - 1].revision) {
			objects[event.id - 1].acknowledged = 1;
			objects[event.id - 1].created = 1; /* Peer has bound this lifetime. */
		}
		return;
	}
	if (event.kind == OBJECT_SYNC && !event.id && event.revision == 1) {
		if (epoch != event.epoch) {
			int role = currentRole, level = currentLevel, mission = currentMission;
			coopObjectsReset();
			currentRole = role;
			currentLevel = level;
			currentMission = mission;
			epoch = event.epoch;
		}
		synchronized = 1;
	} else {
		if (!synchronized || event.epoch != epoch || !event.id || event.uid <= 0 ||
		    event.oClass != MOBY_ID_BLADEBALL || event.motion.id != event.id ||
		    (event.kind != OBJECT_CREATE && event.kind != OBJECT_STATE && event.kind != OBJECT_REMOVE))
			return;
		object = &objects[event.id - 1];
		if (object->revision && (int)(event.revision - object->revision) < 0)
			return;
		if (event.revision != object->revision) {
			memset(&state, 0, sizeof(state));
			state.state = event.state;
			state.collision = event.collision;
			state.mode = event.mode;
			state.health = event.health;
			state.removed = event.kind == OBJECT_REMOVE;
			if (!unpackMotion(&event.motion, &state) || (object->state.removed && !state.removed))
				return;
			if (!state.removed && !object->allocated && (event.kind != OBJECT_CREATE || !bindGuest(object, &event, start, end)))
				return;
			if (object->allocated && (!sameLifetime(object, start, end) || object->uid != event.uid)) {
				if (!state.removed)
					return;
				object->allocated = 0;
			}
			if (!object->revision || state.removed) {
				object->state = object->previous = state;
				object->received = now;
				object->interval = 0;
			} else {
				/* Critical events do not rewind newer motion or restart interpolation. */
				object->state.health = state.health;
				object->state.state = state.state;
				object->state.mode = state.mode;
				object->state.collision = state.collision;
			}
			object->revision = event.revision;
			if (object->allocated)
				bladeballPresent(object->moby, &object->state, 0);
		}
	}
	event.kind = OBJECT_ACK;
	online->sendEvent(OBJECT_CHANNEL, &event, sizeof(event));
}

/*
 * NAME : receiveMotion
 * DESCRIPTION : Apply replaceable motion only to created, live registry identities.
 * NOTES : Per-object sequencing tolerates snapshots containing different object subsets.
 * ARGS : Packet bytes, pool bounds and receive clock.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void receiveMotion(const objectMotionPacket_t* packet, int bytes, u32 start, u32 end, u32 now)
{
	int i;
	if (!synchronized || packet->magic != MOTION_MAGIC || packet->epoch != epoch || !packet->sequence ||
	    packet->count > MOTION_BATCH || packet->reserved || bytes != (int)(0x10 + packet->count * sizeof(objectMotion_t)))
		return;
	for (i = 0; i < packet->count; ++i) {
		const objectMotion_t* motion = &packet->objects[i];
		replicatedObject_t* object;
		bladeballState_t next;
		u32 interval;
		if (!motion->id || motion->id > OBJECT_LIMIT)
			continue;
		object = &objects[motion->id - 1];
		if (!object->revision || object->state.removed || !sameLifetime(object, start, end) ||
		    (object->motionSequence && (int)(packet->sequence - object->motionSequence) <= 0))
			continue;
		next = object->state;
		if (!unpackMotion(motion, &next))
			continue;
		interval = now - object->received;
		if (interval < INTERPOLATION_MIN)
			interval = INTERPOLATION_MIN;
		if (interval > INTERPOLATION_MAX)
			interval = INTERPOLATION_MAX;
		object->previous = object->state;
		object->state = next;
		object->interval = interval;
		object->received = now;
		object->motionSequence = packet->sequence;
	}
}

/*
 * NAME : coopObjectsReceive
 * DESCRIPTION : Stage the newest compact motion packet from the player channel.
 * NOTES : The old generic snapshot protocol is not accepted by this backend.
 * ARGS : Payload and byte count.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopObjectsReceive(const void* data, int bytes)
{
	if (!data || bytes < 0x10 || bytes > (int)sizeof(receivedMotion))
		return;
	memcpy(&receivedMotion, data, bytes);
	receivedBytes = bytes;
}

/*
 * NAME : coopObjectsWrite
 * DESCRIPTION : Attach the current replaceable motion batch to player input.
 * NOTES : Repetition is safe; each object records its last accepted sequence.
 * ARGS : Caller-owned 128-byte destination.
 * RETURN : Number of queued bytes.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopObjectsWrite(void* data)
{
	if (outgoingBytes)
		memcpy(data, &outgoingMotion, outgoingBytes);
	return outgoingBytes;
}

/*
 * NAME : coopObjectsReady
 * DESCRIPTION : Report the object epoch handshake without a global enemy acknowledgement barrier.
 * NOTES : One unavailable enemy must not stop player health or shared progression.
 * ARGS :
 * RETURN : Nonzero for a synchronized guest object session.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopObjectsReady(void)
{
	return currentRole == ONLINE_JOIN && synchronized;
}

/*
 * NAME : coopObjectsUpdate
 * DESCRIPTION : Run the single host-authoritative registry, event and motion backend.
 * NOTES : Experimental Catacrom Blade Ball adapter; no native Medius/Guber session is started.
 * ARGS : Loader, transport, enable/ready state and send permission.
 * RETURN : Nonzero when a critical event was queued.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopObjectsUpdate(const moduleApi_t* loader, const onlineApi_t* online, int enabled, int ready, int canSend)
{
	objectMotionPacket_t packet = receivedMotion;
	objectEvent_t event;
	u32 now = objectTicks(), start, end;
	int role, i, bytes = receivedBytes;
	receivedBytes = 0;
	if (!enabled || loader->context != DL_CONTEXT_CAMPAIGN || !online || !(role = online->role()) ||
	    Level != 22 || Mission->Loading) {
		coopObjectsReset();
		return 0;
	}
	if (role != currentRole || Level != currentLevel || Mission->ActiveMission != currentMission) {
		coopObjectsReset();
		currentRole = role;
		currentLevel = Level;
		currentMission = Mission->ActiveMission;
		epoch = role == ONLINE_HOST ? now | 1 : 0;
	}
	if (!ready || !coopObjectsBounds(&start, &end))
		return 0;
	receiveEvent(online, start, end, now);
	if (role == ONLINE_JOIN) {
		if (bytes)
			receiveMotion(&packet, bytes, start, end, now);
		return 0;
	}
	captureHost(start, end);
	if (!canSend)
		return 0;
	if (!synchronized) {
		if (!lastSync || now - lastSync >= EVENT_RETRY) {
			makeEvent(0, &event);
			if (!online->sendEvent(OBJECT_CHANNEL, &event, sizeof(event)))
				lastSync = now;
		}
		return 0;
	}
	if ((int)(now - nextMotion) >= 0) {
		memset(&outgoingMotion, 0, sizeof(outgoingMotion));
		outgoingMotion.magic = MOTION_MAGIC;
		outgoingMotion.epoch = epoch;
		if (!++sequence)
			++sequence;
		outgoingMotion.sequence = sequence;
		for (i = 0; i < objectCount && outgoingMotion.count < MOTION_BATCH; ++i) {
			int slot = motionCursor++;
			replicatedObject_t* object;
			if (motionCursor >= objectCount)
				motionCursor = 0;
			object = &objects[slot];
			if (!object->created || object->state.removed)
				continue;
			packMotion(slot + 1, &object->state, &outgoingMotion.objects[outgoingMotion.count++]);
		}
		outgoingBytes = outgoingMotion.count ? 0x10 + outgoingMotion.count * sizeof(objectMotion_t) : 0;
		nextMotion = now + MOTION_INTERVAL;
	}
	for (i = 0; i < objectCount; ++i) {
		int slot = eventCursor++;
		replicatedObject_t* object;
		if (eventCursor >= objectCount)
			eventCursor = 0;
		object = &objects[slot];
		if (object->acknowledged || (object->lastEvent && now - object->lastEvent < EVENT_RETRY))
			continue;
		makeEvent(slot + 1, &event);
		if (!online->sendEvent(OBJECT_CHANNEL, &event, sizeof(event))) {
			object->lastEvent = now;
			return 1;
		}
		break;
	}
	return 0;
}
