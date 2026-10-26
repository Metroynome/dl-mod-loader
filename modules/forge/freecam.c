/* Adapted from Horizon's patch/freecam.c by Daniel "Dnawrkshp" Gerendasy
 * and Connor "Badger41" Williams. Local Play only; no patch/DZO globals. */
#include "freecam.h"
#include <libdl/camera.h>
#include <libdl/game.h>
#include <libdl/gamesettings.h>
#include <libdl/graphics.h>
#include <libdl/hud.h>
#include <libdl/math.h>
#include <libdl/pad.h>
#include <libdl/player.h>
#include <libdl/string.h>

#define WORD(address) (*(volatile u32*)(address))
#define CAMERA_POST (0x4b2428)
#define CAMERA_RENDER (0x4b2010)
#define OCCLUSION_UPDATE (0x4c327c)
#define RETICLE_DRAW (0x4b3970)
#define OCCLUSION (0x222648)
#define OCCLUSION_UPDATE_CALL (0x0c1302aa)
#define RETICLE_DRAW_CALL (0x0c12cb28)
#define MOVE_SPEED (0.5f)
#define FAST_MOVE_SPEED (2.0f)
#define SLOW_MOVE_SPEED (0.12f)

/* EE pointers are 32-bit; VECTOR members have 16-byte alignment. */
typedef struct freecamPlayer {
	/* 0x00 */ Player* player;
	/* 0x04 */ int enabled;
	/* 0x08 */ int controlCharacter;
	/* 0x0c */ int ignoreCharacterCamera;
	/* 0x10 */ int hideControls;
	/* 0x14 */ int lockState;
	/* 0x18 */ u32 buttons;
	/* 0x1c */ u32 hud;
	/* 0x20 */ VECTOR position;
	/* 0x30 */ VECTOR lockedPosition;
	/* 0x40 */ VECTOR bookmark;
	/* 0x50 */ float yaw;
	/* 0x54 */ float pitch;
	/* 0x58 */ float characterYaw;
	/* 0x5c */ float characterPitch;
	/* 0x60 */ float cameraOffset;
	/* 0x64 */ int noInput;
	/* 0x68 */ float bookmarkYaw;
	/* 0x6c */ float bookmarkPitch;
	/* 0x70 */ int bookmarkValid;
} freecamPlayer_t;

static void cameraPost(void);

freecamSettings_t forgeFreecam;
static const moduleApi_t* loader;
static freecamPlayer_t players[GAME_MAX_LOCALS];
static int level = -1, lastTime = -1, patched;
static u32 originalOcclusion, changedOcclusion;
static int occlusionChanged;

static void flush(void)
{
	loader->flushCache(0);
	loader->flushCache(2);
}

int forgeFreecamAvailable(void)
{
	return loader && loader->mode == DL_MODE_MULTIPLAYER && loader->context == DL_CONTEXT_LOCAL &&
	       gameGetSettings() && isInGame();
}

static Player* localPlayer(int index)
{
	Player* player = playerGetFromSlot(index);
	return player && playerIsLocal(player) && player->LocalPlayerIndex == index ? player : 0;
}

static void rotation(MATRIX matrix, const freecamPlayer_t* data)
{
	matrix_unit(matrix);
	matrix_rotate_y(matrix, matrix, data->pitch);
	matrix_rotate_z(matrix, matrix, data->yaw);
}

static void cameraPost(void)
{
	int i;
	if (!forgeFreecamAvailable() || *(volatile int*)0x21de10 != level)
		return;
	/* The native camera update resets this flag each frame. */
	if (occlusionChanged)
		WORD(OCCLUSION) = changedOcclusion;
	for (i = 0; i < GAME_MAX_LOCALS; ++i) {
		freecamPlayer_t* data = &players[i];
		GameCamera* camera;
		MATRIX matrix, original;
		if (!data->enabled || localPlayer(i) != data->player)
			continue;
		camera = cameraGetGameCamera(i);
		memcpy(original, camera->uMtx, sizeof(float) * 12);
		vector_copy(camera->pos, data->position);
		camera->rot[0] = 0;
		camera->rot[1] = data->pitch;
		camera->rot[2] = data->yaw;
		rotation(matrix, data);
		memcpy(camera->uMtx, matrix, sizeof(float) * 12);
		((void (*)(int))CAMERA_RENDER)(i);
		if (data->ignoreCharacterCamera)
			memcpy(camera->uMtx, original, sizeof(float) * 12);
	}
}

static void restoreHooks(void)
{
	u32 jump = 0x08000000 | ((u32)cameraPost >> 2);
	if (!patched)
		return;
	if (occlusionChanged && WORD(OCCLUSION) == changedOcclusion)
		WORD(OCCLUSION) = originalOcclusion;
	occlusionChanged = 0;
	/* Overlay replacement can reuse these addresses. Restore only our words. */
	if (WORD(CAMERA_POST) == jump && WORD(CAMERA_POST + 4) == 0x27bd0080) {
		WORD(CAMERA_POST) = 0x03e00008;
		if (!WORD(RETICLE_DRAW))
			WORD(RETICLE_DRAW) = RETICLE_DRAW_CALL;
		flush();
	}
	patched = 0;
}

static int installHooks(void)
{
	if (patched)
		return WORD(CAMERA_POST) == (0x08000000 | ((u32)cameraPost >> 2));
	/* These three native sites were checked in the NTSC-U gameplay overlay. */
	if (WORD(CAMERA_POST) != 0x03e00008 || WORD(CAMERA_POST + 4) != 0x27bd0080 ||
	    WORD(OCCLUSION_UPDATE) != OCCLUSION_UPDATE_CALL || WORD(RETICLE_DRAW) != RETICLE_DRAW_CALL)
		return 0;
	WORD(CAMERA_POST) = 0x08000000 | ((u32)cameraPost >> 2);
	/* Keep native occlusion updates: freezing them prevents Render All from
	 * rebuilding the visible terrain list when the flag changes. */
	WORD(RETICLE_DRAW) = 0;
	originalOcclusion = WORD(OCCLUSION);
	occlusionChanged = 0;
	patched = 1;
	flush();
	return 1;
}

static void disablePlayer(int index)
{
	freecamPlayer_t* data = &players[index];
	Player* player = data->player;
	/* Never restore saved values into a new level/player allocation. */
	if (data->enabled && forgeFreecamAvailable() && *(volatile int*)0x21de10 == level && localPlayer(index) == player) {
		PlayerHUDFlags* hud = hudGetPlayerFlags(index);
		player->timers.noInput = data->noInput;
		player->CameraOffset[0] = data->cameraOffset;
		player->CameraYaw.Value = data->characterYaw;
		player->CameraPitch.Value = data->characterPitch;
		if (hud)
			hud->Flags.Raw = data->hud;
	}
	data->enabled = 0;
	data->player = 0;
	data->lockState = -1;
}

void forgeFreecamReset(void)
{
	int i;
	for (i = 0; i < GAME_MAX_LOCALS; ++i)
		disablePlayer(i);
	restoreHooks();
	memset(players, 0, sizeof(players));
	level = lastTime = -1;
}

void forgeFreecamInit(const moduleApi_t* api)
{
	loader = api;
	memset(&forgeFreecam, 0, sizeof(forgeFreecam));
	forgeFreecam.enabled = 1;
	memset(players, 0, sizeof(players));
}

static int enablePlayer(Player* player, freecamPlayer_t* data)
{
	PlayerHUDFlags* hud;
	if (!installHooks()) {
		loader->print("forge: freecam unavailable; camera hooks are not retail\n");
		return 0;
	}
	data->player = player;
	data->enabled = 1;
	data->controlCharacter = data->ignoreCharacterCamera = data->hideControls = 0;
	data->lockState = -1;
	data->yaw = data->characterYaw = player->CameraYaw.Value;
	data->pitch = data->characterPitch = player->CameraPitch.Value;
	data->cameraOffset = player->CameraOffset[0];
	data->noInput = player->timers.noInput;
	vector_copy(data->position, player->CameraPos);
	vector_copy(data->lockedPosition, player->PlayerPosition);
	hud = hudGetPlayerFlags(player->LocalPlayerIndex);
	data->hud = hud ? hud->Flags.Raw : 0;
	return 1;
}

static float axis(u8 value)
{
	if (value > 143)
		return (value - 143) / 112.0f;
	if (value < 111)
		return (value - 111) / 111.0f;
	return 0;
}

static void moveCamera(freecamPlayer_t* data, const PadButtonStatus* pad, u32 buttons)
{
	MATRIX matrix;
	VECTOR direction;
	float movement[2] = {axis(pad->ljoy_v), axis(pad->ljoy_h)};
	float speed = MOVE_SPEED;
	if ((buttons & (PAD_L1 | PAD_R1)) == PAD_L1)
		speed = FAST_MOVE_SPEED;
	else if ((buttons & (PAD_L1 | PAD_R1)) == PAD_R1)
		speed = SLOW_MOVE_SPEED;
	float pitch = axis(pad->rjoy_v), yaw = axis(pad->rjoy_h);
	float limit = 89 * MATH_DEG2RAD;
	int i;
	rotation(matrix, data);
	for (i = 0; i < 2; ++i) {
		vector_copy(direction, &matrix[i * 4]);
		if (buttons & PAD_L3) {
			direction[2] = 0;
			if (vector_sqrmag(direction) < 0.0001f)
				continue;
			vector_normalize(direction, direction);
		}
		vector_scale(direction, direction, -movement[i] * speed);
		vector_add(data->position, data->position, direction);
	}
	if (buttons & PAD_L2)
		data->position[2] -= speed;
	if (buttons & PAD_R2)
		data->position[2] += speed;
	/* Squared look input gives the same fine control near center as Horizon. */
	data->pitch = clampAngle(data->pitch + pitch * fabsf(pitch) * 0.1f);
	data->pitch = clamp(data->pitch, -limit, limit);
	data->yaw = clampAngle(data->yaw - yaw * fabsf(yaw) * 0.1f);
}

static void controls(const freecamPlayer_t* data, int index)
{
	float y = SCREEN_HEIGHT - 8;
	float x = 8;
	float scale = playerGetNumLocals() > 1 ? 0.65f : 0.8f;
	if (!index && playerGetNumLocals() > 1)
		y = SCREEN_HEIGHT / 2 - 8;
	gfxScreenSpaceText(x, y, scale, scale, 0x80ffffff, "\x1c Controls   L1+\x1d Exit   Select+\x12 Save / \x13 Restore", -1, TEXT_ALIGN_BOTTOMLEFT);
	gfxScreenSpaceText(x, y - 15, scale, scale, 0x80ffffff,
	    data->controlCharacter ? "\x1a Lock Character" : "\x1a Control Character", -1, TEXT_ALIGN_BOTTOMLEFT);
	gfxScreenSpaceText(x, y - 30, scale, scale, 0x80ffffff,
	    data->ignoreCharacterCamera ? "\x1b Use Character Camera" : "\x1b Ignore Character Camera", -1, TEXT_ALIGN_BOTTOMLEFT);
	gfxScreenSpaceText(x, y - 45, scale, scale, 0x80ffffff,
	    (occlusionChanged ? changedOcclusion : WORD(OCCLUSION)) == 2 ? "\x13 Occlusion ON" : "\x13 Occlusion OFF (Render All)", -1, TEXT_ALIGN_BOTTOMLEFT);
	if (forgeFreecam.lockAnimation)
		gfxScreenSpaceText(x, y - 60, scale, scale, 0x80ffffff,
		    data->lockState < 0 ? "\x1d Lock Animation" : "\x1d Release Animation", -1, TEXT_ALIGN_BOTTOMLEFT);
}

static void updatePlayer(Player* player, freecamPlayer_t* data, const PadButtonStatus* pad, u32 buttons, u32 pressed)
{
	PlayerHUDFlags* hud = hudGetPlayerFlags(player->LocalPlayerIndex);
	if (pressed & PAD_DOWN)
		data->hideControls = !data->hideControls;
	if (pressed & PAD_RIGHT)
		data->ignoreCharacterCamera = !data->ignoreCharacterCamera;
	/* Select + Square restores the bookmark instead of toggling occlusion. */
	if ((pressed & PAD_SQUARE) && !(buttons & PAD_SELECT)) {
		changedOcclusion = (occlusionChanged ? changedOcclusion : WORD(OCCLUSION)) == 2 ? 0 : 2;
		WORD(OCCLUSION) = changedOcclusion;
		occlusionChanged = 1;
	}
	if ((buttons & PAD_SELECT) && (pressed & PAD_TRIANGLE)) {
		vector_copy(data->bookmark, data->position);
		data->bookmarkYaw = data->yaw;
		data->bookmarkPitch = data->pitch;
		data->bookmarkValid = 1;
	} else if ((buttons & PAD_SELECT) && (pressed & PAD_SQUARE) && data->bookmarkValid) {
		vector_copy(data->position, data->bookmark);
		data->yaw = data->bookmarkYaw;
		data->pitch = data->bookmarkPitch;
		player->CameraYaw.Value = data->yaw;
		player->CameraPitch.Value = data->pitch;
	}
	if (pressed & PAD_LEFT) {
		data->controlCharacter = !data->controlCharacter;
		if (!data->controlCharacter) {
			data->characterYaw = player->CameraYaw.Value;
			data->characterPitch = player->CameraPitch.Value;
		} else {
			player->CameraYaw.Value = data->characterYaw;
			player->CameraPitch.Value = data->characterPitch;
		}
	}
	if (forgeFreecam.lockPosition) {
		vector_copy(player->PlayerPosition, data->lockedPosition);
		player->timers.stuck = 0;
	} else {
		vector_copy(data->lockedPosition, player->PlayerPosition);
	}
	if (!forgeFreecam.lockAnimation)
		data->lockState = -1;
	else if (pressed & PAD_UP)
		data->lockState = data->lockState < 0 ? player->PlayerState : -1;
	if (data->lockState >= 0 && player->PlayerState != data->lockState)
		playerGetVTable(player)->UpdateState(player, data->lockState, 1, 1, 1);
	player->CameraOffset[0] = 0;
	player->timers.noInput = data->controlCharacter ? 0 : 0x7fff;
	if (!data->controlCharacter)
		moveCamera(data, pad, buttons);
	if (hud)
		hud->Flags.Raw = 0;
	if (!data->hideControls)
		controls(data, player->LocalPlayerIndex);
}

void forgeFreecamUpdate(int menuOpen, int loading)
{
	int i, any = 0, time;
	if (!forgeFreecamAvailable() || loading || *(volatile int*)0x21de10 != level) {
		forgeFreecamReset();
		if (forgeFreecamAvailable())
			level = *(volatile int*)0x21de10;
		return;
	}
	if (!forgeFreecam.enabled) {
		for (i = 0; i < GAME_MAX_LOCALS; ++i)
			disablePlayer(i);
		restoreHooks();
		return;
	}
	if (occlusionChanged)
		WORD(OCCLUSION) = changedOcclusion;
	time = gameGetTime();
	if (time == lastTime)
		return;
	lastTime = time;
	for (i = 0; i < GAME_MAX_LOCALS; ++i) {
		freecamPlayer_t* data = &players[i];
		Player* player = localPlayer(i);
		PadButtonStatus* pad = player ? playerGetPad(player) : 0;
		u32 buttons = pad ? (u16)~pad->btns : 0;
		u32 pressed = buttons & ~data->buttons;
		data->buttons = buttons;
		if (!player || player != data->player || playerIsDead(player))
			disablePlayer(i);
		if (!player || !pad || playerIsDead(player) || menuOpen || gameIsStartMenuOpen(i)) {
			any |= data->enabled;
			continue;
		}
		if (forgeFreecam.airwalk) {
			player->Ground.offAny = 0;
			player->Ground.offGood = 0;
		}
		/* Circle belongs to Forge's reload combo; it must not toggle freecam. */
		if ((buttons & (PAD_L1 | PAD_UP | PAD_CIRCLE)) == (PAD_L1 | PAD_UP) && (pressed & (PAD_L1 | PAD_UP))) {
			if (data->enabled)
				disablePlayer(i);
			else
				enablePlayer(player, data);
		} else if (data->enabled) {
			updatePlayer(player, data, pad, buttons, pressed);
		}
		any |= data->enabled;
	}
	if (!any)
		restoreHooks();
}
