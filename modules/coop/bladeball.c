#include "bladeball.h"
#include <librac4/moby.h>
#include <librac4/interop.h>

#define WORD(address) (*(volatile u32*)(address))

/*
 * NAME : healthAddress
 * DESCRIPTION : Follow the verified native target-variable interface.
 * NOTES : Never uses a pointer supplied by the peer.
 * ARGS : Local Blade Ball address.
 * RETURN : Health address or zero when this instance has no target interface.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 healthAddress(u32 moby)
{
	u32 vars = WORD(moby + 0xac), target;
	if (!(*(u16*)(moby + 0x34) & 0x20) || vars < 0x00100000 || vars > 0x01fffffc)
		return 0;
	target = WORD(vars);
	return target >= 0x00100000 && target <= 0x01ffff70 ? target : 0;
}

/*
 * NAME : bladeballStateValid
 * DESCRIPTION : Reject nonfinite or out-of-range presentation fields.
 * NOTES : Health minus one denotes an instance without a health interface.
 * ARGS : Captured or decoded state.
 * RETURN : Nonzero for valid values.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int bladeballStateValid(const bladeballState_t* state)
{
	int i;
	if (!(state->health >= -1 && state->health <= 10000000) ||
	    !(state->animTime >= 0 && state->animTime <= 255) ||
	    !(state->animSpeed >= -32 && state->animSpeed <= 32) || state->removed > 1 || state->collision > 1)
		return 0;
	for (i = 0; i < 3; ++i)
		if (!(state->position[i] >= 0 && state->position[i] <= 1024) ||
		    !(state->rotation[i] >= -100 && state->rotation[i] <= 100))
			return 0;
	return 1;
}

/*
 * NAME : bladeballCapture
 * DESCRIPTION : Capture the verified Blade Ball presentation and critical fields.
 * NOTES : Native host update remains responsible for all enemy decisions.
 * ARGS : Local instance and output state.
 * RETURN : Nonzero for valid state.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int bladeballCapture(u32 moby, bladeballState_t* state)
{
	int i;
	u32 health = healthAddress(moby);
	for (i = 0; i < 3; ++i) {
		state->position[i] = ((float*)(moby + 0x10))[i];
		state->rotation[i] = ((float*)(moby + 0xf0))[i];
	}
	state->health = health ? *(float*)health : -1;
	state->mode = *(u16*)(moby + 0x34);
	state->state = *(s8*)(moby + 0x20);
	state->collision = WORD(moby + 0x9c) != 0;
	state->animId = WORD(moby + 0x40) ? *(u8*)(moby + 0x54) : 255;
	state->animFlags = *(u8*)(moby + 0x55);
	state->animTime = state->animId == 255 ? 0 : *(float*)(moby + 0x44);
	state->animSpeed = state->animId == 255 ? 0 : *(float*)(moby + 0x48);
	state->removed = state->state < 0;
	state->reserved = 0;
	return bladeballStateValid(state);
}

/*
 * NAME : presentAnimation
 * DESCRIPTION : Change native animation sequences only in the moby update pass.
 * NOTES : Uses the guest model; never transmits sequence pointers or decodes on receipt.
 * ARGS : Local instance and host animation state.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void presentAnimation(u32 moby, const bladeballState_t* state)
{
	u32 model = WORD(moby + 0x24), sequence;
	u32 snap = GetAddressImmediate(&vaCoopMobySnapAnimation);
	if (state->animId == 255 || !snap || WORD(snap) != 0x27bdfff0 ||
	    model < 0x00100000 || model > 0x01fffb00 || state->animId >= *(u8*)(model + 0x0c))
		return;
	sequence = WORD(model + 0x48 + state->animId * 4);
	if (sequence < 0x00100000 || sequence > 0x01ffffe0 || state->animTime >= *(u8*)(sequence + 0x10))
		return;
	if (*(u8*)(moby + 0x54) != state->animId || WORD(moby + 0x40) != sequence)
		((void (*)(void*, int, float))snap)((void*)moby, state->animId, state->animTime);
	else if (state->animSpeed == 0)
		*(float*)(moby + 0x44) = state->animTime;
	*(float*)(moby + 0x48) = state->animSpeed;
	*(u8*)(moby + 0x55) = state->animFlags;
}

/*
 * NAME : bladeballPresent
 * DESCRIPTION : Present authoritative state without invoking guest enemy AI.
 * NOTES : Native post-update still owns model transforms and collision-grid maintenance.
 * ARGS : Local instance, interpolated state and safe-animation-pass flag.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void bladeballPresent(u32 moby, const bladeballState_t* state, int animate)
{
	int i;
	u32 health = healthAddress(moby);
	if (state->removed || (state->mode & 3) == 3) {
		*(u16*)(moby + 0x34) |= 0x80;
		WORD(moby + 0x9c) = 0;
		if (health)
			*(float*)health = 0;
		return;
	}
	*(s8*)(moby + 0x20) = state->state;
	/* Dormant local copies can retain no-animation after the host has awakened them. */
	*(u16*)(moby + 0x34) = (*(u16*)(moby + 0x34) & ~0xc7) | (state->mode & 0xc4);
	WORD(moby + 0x9c) = state->collision != 0;
	if (health && state->health >= 0)
		*(float*)health = state->health;
	for (i = 0; i < 3; ++i) {
		((float*)(moby + 0x10))[i] = state->position[i];
		if (animate)
			((float*)(moby + 0xf0))[i] = state->rotation[i];
	}
	if (animate)
		presentAnimation(moby, state);
}
