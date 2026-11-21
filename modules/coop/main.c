#include "include/coop.h"
#include "modules/online/include/online.h"
#include "modules/modmenu/include/modmenu.h"
#include <librac4/interop.h>
#include <librac4/pad.h>
#include <librac4/game.h>

#define LEVEL (*(volatile int*)0x0021de10)
#define MISSION (*(volatile int*)0x001711a8)
#define LOCAL_COUNT (*(volatile int*)0x00172174)
#define WORD(address) (*(volatile u32*)(address))
#define INPUT_TIMEOUT (75000000)
#define MENU_BUTTONS (PAD_START | PAD_SELECT | PAD_L3 | PAD_R3)
#define HUD_DPAD (0x08)

typedef void (*rawPadCallback_t)(void*, u8*, int, void*);

typedef struct coopPad {
	/* 0x00 */ struct PAD* pad;
	/* 0x04 */ rawPadCallback_t original;
} coopPad_t;

typedef struct coopInput {
	/* 0x00 */ u32 level;
	/* 0x04 */ int mission;
	/* 0x08 */ u8 sample[COOP_SAMPLE_BYTES];
	/* 0x1c */ float position[3];
} coopInput_t;

static void rawPad(void* controller, u8* sample, int size, void* context);
static void drawView(int screen, int frustum, int first, int last);

volatile coopControl_t moduleExports DL_MODULE_EXPORTS;
static const moduleApi_t* loader;
static const onlineApi_t* online;
static const modMenuApi_t* menu;
static coopPad_t pads[2];
static u32 remoteTime, sourceSequence, sentSequence;
static int active, allowInput, sourceValid, remoteValid, tab;
static int viewInstalled;
static u32 savedFrustums[20];
static u32 savedWideFrustums[2];
static u32 savedGuiLayout[6];
static u32 savedDpadPanels[2];
static u32 hudPanels[2];
static const u8 guiLayoutOffsets[] = {0x0c, 0x20, 0x24};
static VariableAddress_t vaHeroes = {
	.Battledome = 0x003660f8
};
static VariableAddress_t vaHeroLookup = {
	.Battledome = 0x006658c0
};
static VariableAddress_t vaRawPad = {
	.Battledome = 0x006b5cb8
};
static VariableAddress_t vaDrawView = {
	.Battledome = 0x00529b88
};
static VariableAddress_t vaViewCalls = {
	.Battledome = 0x0052a0d0
};
static VariableAddress_t vaPausedViewCalls = {
	.Battledome = 0x00549aac
};
static VariableAddress_t vaFrustums = {
	.Battledome = 0x003b65b0
};
static VariableAddress_t vaWideFrustums = {
	.Battledome = 0x003b6790
};
static VariableAddress_t vaGameMode = {
	.Battledome = 0x0021ddb4
};
static VariableAddress_t vaGuiPlayers = {
	.Battledome = 0x0031b2f8
};
static VariableAddress_t vaHudPlayers = {
	.Battledome = 0x0032ed60
};

/*
 * NAME : refreshHud
 * DESCRIPTION : Recreate cached HUD panels using the current player layout.
 * NOTES : Use each panel's original full-screen template, including the D-pad.
 * ARGS : Nonzero to request one refresh after a layout change.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void refreshHud(int rebuild)
{
	u32 data = GetAddressImmediate(&vaHudPlayers);
	int i;
	if (LEVEL != 21 || !data) {
		hudPanels[0] = hudPanels[1] = 0;
		return;
	}
	for (i = 0; i < 2; ++i) {
		u32 player = data + i * 0x10;
		if (rebuild && !hudPanels[i] && WORD(player + 4)) {
			hudPanels[i] = WORD(player + 4);
			WORD(player + 4) = 0;
		} else if (hudPanels[i] && !WORD(player + 8) && !WORD(player + 0x0c)) {
			if (!WORD(player + 4))
				WORD(player + 4) = hudPanels[i];
			hudPanels[i] = 0;
		}
	}
}

/*
 * NAME : gameplayInput
 * DESCRIPTION : Allow player controls only during native normal gameplay.
 * NOTES : Pause, vendors, scenes and Mod Menu keep their input local.
 * ARGS :
 * RETURN : Nonzero when gameplay owns the controls.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int gameplayInput(void)
{
	u32 address = GetAddressImmediate(&vaGameMode);
	return LEVEL == 21 && address && WORD(address) == GAME_MODE_NORMAL && !(menu && menu->isOpen());
}

/*
 * NAME : drawView
 * DESCRIPTION : Draw the assigned player's native camera and HUD full screen.
 * NOTES : Both heroes continue updating normally outside the rendering path.
 * ARGS : Native screen, frustum and first/last-pass flags.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void drawView(int screen, int frustum, int first, int last)
{
	int selected = online && online->role() == ONLINE_JOIN ? 1 : 0;
	if (screen == selected)
		((void (*)(int, int, int, int))GetAddressImmediate(&vaDrawView))(screen, 0, 1, 1);
	(void)frustum;
	(void)first;
	(void)last;
}

/*
 * NAME : updateView
 * DESCRIPTION : Install or restore normal and paused co-op view calls.
 * NOTES : Frustums and per-player GUI records supply full-screen HUD bounds.
 * ARGS : Whether the supported co-op view is active.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void updateView(int enabled)
{
	u32 normal = GetAddressImmediate(&vaViewCalls);
	u32 paused = GetAddressImmediate(&vaPausedViewCalls);
	u32 calls[4] = {normal, normal + 0x1c, paused, paused + 0x14};
	u32 table = GetAddressImmediate(&vaFrustums);
	u32 wide = GetAddressImmediate(&vaWideFrustums);
	u32 gui = GetAddressImmediate(&vaGuiPlayers);
	u32 hud = GetAddressImmediate(&vaHudPlayers);
	u32 native = 0x0c000000 | (GetAddressImmediate(&vaDrawView) >> 2);
	u32 replacement = 0x0c000000 | ((u32)drawView >> 2);
	int i;
	if (LEVEL != 21) {
		viewInstalled = 0;
		return;
	}
	if (!normal || !paused || !table || enabled == viewInstalled)
		return;
	if (enabled) {
		for (i = 0; i < 4; ++i)
			if (WORD(calls[i]) != native)
				return;
		for (i = 0; i < 20; ++i) {
			savedFrustums[i] = WORD(table + 0x28 + i * 4);
			WORD(table + 0x28 + i * 4) = WORD(table + (i % 10) * 4);
		}
		for (i = 0; i < 2; ++i) {
			savedWideFrustums[i] = WORD(wide + 4 + i * 4);
			WORD(wide + 4 + i * 4) = 0;
		}
		/* Native GuiMain_PlayerData stride is 0x54. Change only screen mode
		 * and canvas offsets; preserve each player's menu/HUD ownership. */
		for (i = 0; i < 6; ++i) {
			u32 field = gui + (i / 3) * 0x54 + guiLayoutOffsets[i % 3];
			savedGuiLayout[i] = WORD(field);
			WORD(field) = 0;
		}
		/* Station has no usable D-pad actions. Keep the original full-screen
		 * template for other levels; suppress only this verified overlay. */
		for (i = 0; i < 2; ++i) {
			savedDpadPanels[i] = WORD(hud + i * 0x10 + 4) & HUD_DPAD;
			WORD(hud + i * 0x10 + 4) &= ~HUD_DPAD;
		}
		for (i = 0; i < 4; ++i)
			WORD(calls[i]) = replacement;
	} else {
		for (i = 0; i < 4; ++i)
			if (WORD(calls[i]) == replacement)
				WORD(calls[i]) = native;
		for (i = 0; i < 20; ++i)
			WORD(table + 0x28 + i * 4) = savedFrustums[i];
		for (i = 0; i < 2; ++i)
			WORD(wide + 4 + i * 4) = savedWideFrustums[i];
		for (i = 0; i < 6; ++i)
			WORD(gui + (i / 3) * 0x54 + guiLayoutOffsets[i % 3]) = savedGuiLayout[i];
		for (i = 0; i < 2; ++i)
			WORD(hud + i * 0x10 + 4) |= savedDpadPanels[i];
	}
	viewInstalled = enabled;
	refreshHud(1);
	loader->flushCache(0);
	loader->flushCache(2);
}

/*
 * NAME : correctPosition
 * DESCRIPTION : Reconcile the remote hero with its owner's position snapshot.
 * NOTES : Preserve native movement history; large errors snap, small errors blend.
 * ARGS : Validated peer snapshot and remote hero address.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void correctPosition(const coopInput_t* frame, u32 hero)
{
	float delta[3], distance = 0, scale;
	u32 moby = WORD(hero + 0x2ef0);
	int i;
	if (moby < 0x100000 || moby >= 0x2000000)
		return;
	for (i = 0; i < 3; ++i) {
		/* Ordered comparisons reject NaNs and infinities as well. */
		if (!(frame->position[i] >= 2 && frame->position[i] <= 1021))
			return;
		delta[i] = frame->position[i] - ((float*)(hero + 0xa0))[i];
		distance += delta[i] * delta[i];
	}
	if (!(distance > 0.0025f))
		return;
	scale = distance > 100 ? 1 : 0.35f;
	for (i = 0; i < 3; ++i) {
		delta[i] *= scale;
		((float*)(hero + 0xa0))[i] += delta[i];
		((float*)(hero + 0x110))[i] += delta[i];
		((float*)(hero + 0x2f04))[i] += delta[i];
		((float*)(moby + 0x10))[i] += delta[i];
	}
}

/*
 * NAME : ticks
 * DESCRIPTION : Read the local EE clock for stale-input detection.
 * NOTES :
 * ARGS :
 * RETURN : Wrapping EE count.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 ticks(void)
{
	u32 value;
	__asm__ volatile("mfc0 %0, $9" : "=r"(value));
	return value;
}

/*
 * NAME : neutral
 * DESCRIPTION : Fill a neutral analog-controller sample.
 * NOTES : Status/mode bytes are preserved when applying a sample to native pads.
 * ARGS : Sample destination.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void neutral(u8* sample)
{
	int i;
	for (i = 0; i < COOP_SAMPLE_BYTES; ++i)
		sample[i] = i == 1 ? 0x79 : i == 2 || i == 3 ? 255 : i >= 4 && i < 8 ? 127 : 0;
}

/*
 * NAME : supported
 * DESCRIPTION : Verify the known Cooperative Station overlay and pad ownership.
 * NOTES : Other planets and scripted PadStream playback remain untouched.
 * ARGS :
 * RETURN : Nonzero only for the verified normal-control state.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int supported(void)
{
	u32 table, lookup, hero;
	struct PAD* pad;
	int i;
	if (!active || loader->context != DL_CONTEXT_CAMPAIGN || LEVEL != 21 || LOCAL_COUNT != 2)
		return 0;
	table = GetAddressImmediate(&vaHeroes);
	lookup = GetAddressImmediate(&vaHeroLookup);
	if (!table || !lookup || WORD(lookup) != 0x240331f0 || WORD(lookup + 4) != 0x3c020037)
		return 0;
	for (i = 0; i < 2; ++i) {
		hero = WORD(table + i * 4);
		pad = padGetPad(i);
		if (hero < 0x100000 || hero >= 0x2000000 || !pad || pad->disconnected || pad->state != 6 ||
		    WORD(hero + 0x2f00) != (u32)pad || WORD(hero + 0x2f10) != (u32)i)
			return 0;
	}
	return 1;
}

/*
 * NAME : releasePads
 * DESCRIPTION : Release owned callbacks without breaking an active menu's chain.
 * NOTES : A deferred wrapper removes itself when it becomes the top callback again.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void releasePads(void)
{
	int i;
	updateView(0);
	allowInput = sourceValid = remoteValid = 0;
	for (i = 0; i < 2; ++i) {
		if (!pads[i].pad || pads[i].pad->RawPadInputCallback != rawPad)
			continue;
		pads[i].pad->RawPadInputCallback = LEVEL == 21 ? pads[i].original : 0;
		pads[i].pad = 0;
		pads[i].original = 0;
	}
}

/*
 * NAME : rawPad
 * DESCRIPTION : Capture local input and map the peer to its native co-op slot.
 * NOTES : Menu capture stays in control; menu buttons are never sent remotely.
 * ARGS : Native controller, raw sample, size and original callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void rawPad(void* controller, u8* sample, int size, void* context)
{
	rawPadCallback_t original;
	const volatile u8* replacement = 0;
	u8 idle[COOP_SAMPLE_BYTES];
	u16 localMenu;
	int i, j, role, usable, gameplay;
	for (i = 0; i < 2; ++i) {
		if (pads[i].pad != controller)
			continue;
		original = pads[i].original;
		role = online ? online->role() : 0;
		usable = active && allowInput && moduleExports.enabled && role && LEVEL == 21;
		if (pads[i].pad->RawPadInputCallback == rawPad && !usable) {
			pads[i].pad->RawPadInputCallback = LEVEL == 21 ? original : 0;
			pads[i].pad = 0;
			pads[i].original = 0;
		}
		if (usable && pads[i].pad && pads[i].pad->RawPadInputCallback == rawPad && size >= 8 && sample[0] == 0) {
			localMenu = (sample[2] | (sample[3] << 8)) & MENU_BUTTONS;
			gameplay = gameplayInput();
			if (i == 0 && gameplay && (localMenu & PAD_START)) {
				for (j = 0; j < COOP_SAMPLE_BYTES; ++j)
					moduleExports.sourceSample[j] = j < size ? sample[j] : 0;
				moduleExports.sourceSample[2] |= MENU_BUTTONS & 255;
				moduleExports.sourceSample[3] |= MENU_BUTTONS >> 8;
				++sourceSequence;
				sourceValid = 1;
			} else if (i == 0) {
				sourceValid = 0;
			}
			neutral(idle);
			if (!gameplay)
				replacement = i == 1 ? idle : 0;
			else if (role == ONLINE_HOST && i == 1)
				replacement = remoteValid && ticks() - remoteTime < INPUT_TIMEOUT ? moduleExports.remoteSample : idle;
			else if (role == ONLINE_JOIN)
				replacement = i == 1 ? (sourceValid ? moduleExports.sourceSample : idle) :
				    remoteValid && ticks() - remoteTime < INPUT_TIMEOUT ? moduleExports.remoteSample : idle;
			if (replacement) {
				for (j = 2; j < size && j < COOP_SAMPLE_BYTES; ++j)
					sample[j] = replacement[j];
				sample[2] |= MENU_BUTTONS & 255;
				sample[3] |= MENU_BUTTONS >> 8;
				if (i == 0) {
					sample[2] = (sample[2] & ~(MENU_BUTTONS & 255)) | (localMenu & 255);
					sample[3] = (sample[3] & ~(MENU_BUTTONS >> 8)) | (localMenu >> 8);
				}
			}
		}
		if (original && LEVEL == 21)
			original(controller, sample, size, context);
		return;
	}
}

/*
 * NAME : stateLabel
 * DESCRIPTION : Explain whether input sharing is available.
 * NOTES :
 * ARGS : Unused menu arguments.
 * RETURN : Status label.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static const char* stateLabel(void* unused, int value)
{
	(void)unused;
	(void)value;
	if (moduleExports.state == COOP_ACTIVE)
		return moduleExports.stale ? "Waiting for input" : "Sharing input";
	return moduleExports.state == COOP_OFF ? "Off" : moduleExports.state == COOP_WAITING ? "Connect Online first" : "Unsupported map/state";
}

/*
 * NAME : registerMenu
 * DESCRIPTION : Add an opt-in co-op input test tab.
 * NOTES : This toggle is not persisted.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void registerMenu(void)
{
	modMenuItem_t toggle = {"Co-op Input Test", MODMENU_TOGGLE, (int*)&moduleExports.enabled, 0, 1, 1, 0, 0};
	modMenuItem_t status = {"Status", MODMENU_RANGE, (int*)&moduleExports.state, COOP_OFF, COOP_ACTIVE, 1, 0, 0};
	modMenuItem_t help = {"Host: player 1 / Guest: player 2", MODMENU_LABEL, 0, 0, 0, 0, 0, 0};
	modMenuItem_t scope = {"Station test; no enemy/objective sync", MODMENU_LABEL, 0, 0, 0, 0, 0, 0};
	int item;
	if (tab)
		return;
	menu = modmenuFind(loader);
	if (!menu)
		return;
	tab = menu->addTab("coop", "Co-op", DL_MODE_CAMPAIGN);
	if (tab <= 0) {
		tab = 0;
		return;
	}
	menu->setTabContexts(tab, DL_CONTEXT_CAMPAIGN);
	menu->addItem(tab, &toggle);
	item = menu->addItem(tab, &status);
	menu->setItemValueLabel(item, stateLabel);
	menu->setItemEnabled(item, 0);
	menu->addItem(tab, &help);
	menu->addItem(tab, &scope);
}

/*
 * NAME : deactivate
 * DESCRIPTION : Restore native input when the module becomes inactive.
 * NOTES : Original callbacks remain available for a menu-owned deferred chain.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void deactivate(void)
{
	active = 0;
	releasePads();
	if (menu && tab)
		menu->removeTab(tab);
	menu = 0;
	tab = 0;
	moduleExports.state = COOP_OFF;
}

/*
 * NAME : activate
 * DESCRIPTION : Re-enable the optional campaign adapter.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void activate(void)
{
	active = 1;
}

/*
 * NAME : moduleInit
 * DESCRIPTION : Initialize the opt-in Station input adapter.
 * NOTES : Online and Mod Menu are discovered independently at runtime.
 * ARGS : Loader API.
 * RETURN : Zero success.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int moduleInit(const moduleApi_t* api)
{
	loader = api;
	moduleExports.magic = COOP_MAGIC;
	moduleExports.size = sizeof(moduleExports);
	api->setLifecycle(activate, deactivate);
	return 0;
}

/*
 * NAME : moduleUpdate
 * DESCRIPTION : Exchange current pad snapshots through the generic Online API.
 * NOTES : Each peer owns its local hero; enemies/objectives remain independent.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void moduleUpdate(void)
{
	coopInput_t frame;
	struct PAD* pad;
	u32 original;
	int i, j, size, local;
	refreshHud(0);
	registerMenu();
	online = onlineFind(loader);
	if (!moduleExports.enabled || !online || !online->role() || !supported()) {
		moduleExports.state = !moduleExports.enabled ? COOP_OFF : !online || !online->role() ? COOP_WAITING : COOP_UNSUPPORTED;
		releasePads();
		return;
	}
	if (menu && menu->isOpen()) {
		allowInput = 0;
		sourceValid = 0;
	} else {
		original = GetAddressImmediate(&vaRawPad);
		for (i = 0; i < 2; ++i) {
			pad = padGetPad(i);
			if (pad->RawPadInputCallback != rawPad && (u32)pad->RawPadInputCallback != original)
				break;
			pads[i].pad = pad;
			pads[i].original = (rawPadCallback_t)original;
			pad->RawPadInputCallback = rawPad;
		}
		allowInput = i == 2;
	}
	moduleExports.state = COOP_ACTIVE;
	updateView(1);
	if (!gameplayInput())
		sourceValid = 0;
	local = online->role() == ONLINE_JOIN ? 1 : 0;
	size = online->receive(COOP_INPUT_CHANNEL, &frame, sizeof(frame));
	if (size == sizeof(frame) && frame.level == 21 && frame.mission == MISSION && frame.sample[0] == 0) {
		for (j = 0; j < COOP_SAMPLE_BYTES; ++j)
			moduleExports.remoteSample[j] = frame.sample[j];
		remoteTime = ticks();
		remoteValid = 1;
		++moduleExports.received;
		if (allowInput && gameplayInput())
			correctPosition(&frame, WORD(GetAddressImmediate(&vaHeroes) + (1 - local) * 4));
	}
	moduleExports.stale = !remoteValid || ticks() - remoteTime >= INPUT_TIMEOUT;
	frame.level = 21;
	frame.mission = MISSION;
	for (j = 0; j < 3; ++j)
		frame.position[j] = ((float*)(WORD(GetAddressImmediate(&vaHeroes) + local * 4) + 0xa0))[j];
	if (!sourceValid) {
		neutral(frame.sample);
	} else {
		for (j = 0; j < COOP_SAMPLE_BYTES; ++j)
			frame.sample[j] = moduleExports.sourceSample[j];
	}
	if (!sourceValid || sentSequence != sourceSequence) {
		if (online->send(COOP_INPUT_CHANNEL, &frame, sizeof(frame)) == 0) {
			sentSequence = sourceSequence;
			++moduleExports.sent;
		}
	}
}
