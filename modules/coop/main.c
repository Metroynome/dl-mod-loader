#include "include/coop.h"
#include "modules/online/include/online.h"
#include "modules/modmenu/include/modmenu.h"
#include <librac4/interop.h>
#include <librac4/pad.h>

#define LEVEL (*(volatile int*)0x0021de10)
#define MISSION (*(volatile int*)0x001711a8)
#define LOCAL_COUNT (*(volatile int*)0x00172174)
#define WORD(address) (*(volatile u32*)(address))
#define INPUT_TIMEOUT (75000000)
#define MENU_BUTTONS (PAD_START | PAD_SELECT | PAD_L3 | PAD_R3)

typedef void (*rawPadCallback_t)(void*, u8*, int, void*);

typedef struct coopPad {
	/* 0x00 */ struct PAD* pad;
	/* 0x04 */ rawPadCallback_t original;
} coopPad_t;

typedef struct coopInput {
	/* 0x00 */ u32 level;
	/* 0x04 */ int mission;
	/* 0x08 */ u8 sample[COOP_SAMPLE_BYTES];
} coopInput_t;

static void rawPad(void* controller, u8* sample, int size, void* context);

volatile coopControl_t moduleExports DL_MODULE_EXPORTS;
static const moduleApi_t* loader;
static const onlineApi_t* online;
static const modMenuApi_t* menu;
static coopPad_t pads[2];
static u32 remoteTime, sourceSequence, sentSequence;
static int active, allowInput, sourceValid, remoteValid, tab;
static VariableAddress_t vaHeroes = {
	.Battledome = 0x003660f8
};
static VariableAddress_t vaHeroLookup = {
	.Battledome = 0x006658c0
};
static VariableAddress_t vaRawPad = {
	.Battledome = 0x006b5cb8
};

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
	int i, j, role, usable;
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
			if (i == 0) {
				for (j = 0; j < COOP_SAMPLE_BYTES; ++j)
					moduleExports.sourceSample[j] = j < size ? sample[j] : 0;
				moduleExports.sourceSample[2] |= MENU_BUTTONS & 255;
				moduleExports.sourceSample[3] |= MENU_BUTTONS >> 8;
				++sourceSequence;
				sourceValid = 1;
			}
			neutral(idle);
			if (role == ONLINE_HOST && i == 1)
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
	modMenuItem_t scope = {"Station input test; no world sync", MODMENU_LABEL, 0, 0, 0, 0, 0, 0};
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
 * NOTES : This does not correct positions or synchronize enemies/objectives.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void moduleUpdate(void)
{
	coopInput_t frame;
	struct PAD* pad;
	u32 original;
	int i, j, size;
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
	size = online->receive(COOP_INPUT_CHANNEL, &frame, sizeof(frame));
	if (size == sizeof(frame) && frame.level == 21 && frame.mission == MISSION && frame.sample[0] == 0) {
		for (j = 0; j < COOP_SAMPLE_BYTES; ++j)
			moduleExports.remoteSample[j] = frame.sample[j];
		remoteTime = ticks();
		remoteValid = 1;
		++moduleExports.received;
	}
	moduleExports.stale = !remoteValid || ticks() - remoteTime >= INPUT_TIMEOUT;
	frame.level = 21;
	frame.mission = MISSION;
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
