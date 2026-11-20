#include "requirements.h"
#include "config.h"
#include "strings.h"
#include "modules/modmenu/include/modmenu.h"
#include "modules/modmenu/backend.h"

#define REQUIREMENT_NAME_BYTES (48)

typedef struct requirementNotice {
	/* 0x00 */ char name[REQUIREMENT_NAME_BYTES];
	/* 0x30 */ u32 required;
	/* 0x34 */ u32 shown;
} requirementNotice_t;

static int drawRequirements(u32 pressed);
static void updateRequirements(void);

static requirementNotice_t notices[DL_MODULE_LIMIT];
static const modMenuApi_t* menu;
static int noticeCount, currentNotice, extraRamAvailable;
static int ownsModal, ownsFallback, backendActive;
static u32 previousButtons, closingButtons, previousMode;

/*
 * NAME : moduleRequirementsReset
 * DESCRIPTION : Initialize requirement notices for this boot.
 * NOTES : Expanded RAM means the loader actually selected its safe extra-RAM pool.
 * ARGS : Nonzero if the expanded module pool is available.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void moduleRequirementsReset(int extraRam)
{
	memset(notices, 0, sizeof(notices));
	noticeCount = 0;
	currentNotice = -1;
	extraRamAvailable = extraRam;
	menu = 0;
	ownsModal = ownsFallback = backendActive = 0;
	previousButtons = closingButtons = previousMode = 0;
}

/*
 * NAME : moduleRequirementsMissing
 * DESCRIPTION : Return only requirements unavailable in the current context.
 * NOTES : Mod Menu must publish its service in the module's current mode.
 * ARGS : Required feature bits.
 * RETURN : Missing feature bits.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
u32 moduleRequirementsMissing(u32 required)
{
	const moduleApi_t* api = moduleConfigApi();
	u32 missing = 0;
	if ((required & DL_REQUIRE_EXTRA_RAM) && !extraRamAvailable)
		missing |= DL_REQUIRE_EXTRA_RAM;
	if ((required & DL_REQUIRE_MODMENU) && !modmenuFind(api))
		missing |= DL_REQUIRE_MODMENU;
	return missing;
}

/*
 * NAME : moduleRequirementsNotice
 * DESCRIPTION : Queue one warning per module for this game boot.
 * NOTES : Only the base module name is displayed.
 * ARGS : Module filename and required feature bits.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void moduleRequirementsNotice(const char* name, u32 required)
{
	char label[REQUIREMENT_NAME_BYTES];
	int i, n = 0;
	while (name[n] && name[n] != '.' && n < REQUIREMENT_NAME_BYTES - 1) {
		label[n] = name[n];
		++n;
	}
	label[n] = 0;
	for (i = 0; i < noticeCount; ++i) {
		for (n = 0; label[n] && label[n] == notices[i].name[n]; ++n) {
		}
		if (!label[n] && !notices[i].name[n])
			return;
	}
	if (noticeCount == DL_MODULE_LIMIT)
		return;
	strcpy(notices[noticeCount].name, label);
	notices[noticeCount++].required = required;
}

/*
 * NAME : drawRequirements
 * DESCRIPTION : Draw a BIOS-warning-style list of missing module requirements.
 * NOTES : Mod Menu supplies input capture when available.
 * ARGS : Newly pressed controller buttons.
 * RETURN : Nonzero while the warning remains open.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int drawRequirements(u32 pressed)
{
	void (*box)(float, float, float, float, u32) = menu ? menu->drawBox : menuBackendBox;
	void (*text)(float, float, u32, const char*) = menu ? menu->drawText : menuBackendText;
	requirementNotice_t* notice = &notices[currentNotice];
	u32 missing = moduleRequirementsMissing(notice->required);
	float y = .50f;
	if (!missing || (pressed & 0x4000)) {
		notice->shown = 1;
		ownsModal = 0;
		return 0;
	}
	box(0, 0, 1, 1, 0x50000000);
	box(.10f, .27f, .80f, .46f, 0x80404040);
	box(.11f, .28f, .78f, .44f, 0x80202020);
	text(.15f, .32f, 0x80ffffff, "MISSING MODULE REQUIREMENTS");
	text(.15f, .40f, 0x80ffffff, notice->name);
	text(.15f, .45f, 0x80ffffff, "Enable these items, then restart:");
	if (missing & DL_REQUIRE_MODMENU) {
		text(.15f, y, 0x80ffffff, "- Mod Menu (in this mode)");
		y += .05f;
	}
	if (missing & DL_REQUIRE_EXTRA_RAM)
		text(.15f, y, 0x80ffffff, "- 128 MiB RAM + compatible BIOS");
	text(.15f, .66f, 0x80ffffff, "\x10 Okay");
	return 1;
}

/*
 * NAME : updateRequirements
 * DESCRIPTION : Display pending warnings without requiring Mod Menu itself.
 * NOTES : Reuses the verified backend; wait until boot movies finish.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void updateRequirements(void)
{
	const moduleApi_t* api = moduleConfigApi();
	u32 buttons, missing;
	int i;
	if (ownsModal) {
		if (menu == modmenuFind(api))
			return;
		menu->setModal(0);
		menu = 0;
		ownsModal = 0;
	}
	if (previousMode && previousMode != api->mode) {
		menuBackendRelease();
		ownsFallback = 0;
		closingButtons = previousButtons = 0;
	}
	previousMode = api->mode;
	if (!ownsFallback && !closingButtons) {
		for (i = 0; i < noticeCount; ++i) {
			missing = moduleRequirementsMissing(notices[i].required);
			if (missing && !notices[i].shown)
				break;
		}
		if (i == noticeCount)
			return;
		currentNotice = i;
		menu = modmenuFind(api);
		if (menu) {
			if (menu->setModal(drawRequirements))
				ownsModal = 1;
			return;
		}
	}
	if (!menuBackendReady(api->mode) ||
	    (!ownsFallback && !closingButtons && !menuBackendPopupReady(api->mode))) {
		menuBackendRelease();
		ownsFallback = 0;
		previousButtons = closingButtons = 0;
		return;
	}
	buttons = menuBackendButtons(api->mode);
	if (closingButtons) {
		if (buttons & closingButtons)
			menuBackendCapture(api->mode);
		else {
			closingButtons = 0;
			menuBackendRelease();
		}
		return;
	}
	if (!ownsFallback) {
		previousButtons = buttons;
		ownsFallback = 1;
		menu = 0;
	}
	menuBackendCapture(api->mode);
	if (!drawRequirements(buttons & ~previousButtons)) {
		closingButtons = buttons;
		ownsFallback = 0;
		if (!buttons)
			menuBackendRelease();
	}
	previousButtons = buttons;
}

/*
 * NAME : moduleRequirementsTick
 * DESCRIPTION : Schedule requirement drawing at the correct DL frame boundary.
 * NOTES : Campaign uses the same pre-submission backend as Mod Menu.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void moduleRequirementsTick(void)
{
	const moduleApi_t* api = moduleConfigApi();
	int i, pending = 0;
	for (i = 0; i < noticeCount; ++i)
		if (!notices[i].shown && moduleRequirementsMissing(notices[i].required))
			pending = 1;
	if ((!pending && !ownsModal && !ownsFallback && !closingButtons) ||
	    (modmenuFind(api) && !ownsFallback && !closingButtons)) {
		if (backendActive) {
			menuBackendDeactivate();
			backendActive = 0;
		}
		updateRequirements();
		return;
	}
	backendActive = 1;
	menuBackendUpdate(api->mode, updateRequirements);
}
