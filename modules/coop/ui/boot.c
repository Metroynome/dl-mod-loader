#include "boot.h"
#include "boot-addresses.h"
#include "connection.h"
#include "numpad.h"
#include "keyboard.h"
#include "sound.h"
#include "../world.h"
#include <librac4/game.h>
#include <librac4/graphics.h>
#include <librac4/map.h>
#include <librac4/pad.h>
#include <string.h>

#define BOOT_DISCONNECT_FRAMES (12)
#define WORD(address) (*(volatile u32*)(address))
#define NATIVE(address, type) ((type)(address))

typedef struct bootButton {
	/* 0x00 */ u32 enabled;
	/* 0x04 */ const char* text;
	/* 0x08 */ void (*callback)(void*);
} bootButton_t;

typedef enum bootPage {
	BOOT_CHOICES,
	BOOT_SAVE,
	BOOT_JOIN,
	BOOT_EDIT,
	BOOT_HOST
} bootPage_e;

typedef enum bootJoinCheck {
	JOIN_CHECK_NONE,
	JOIN_CHECK_REQUEST,
	JOIN_CHECK_STOPPING,
	JOIN_CHECK_CONNECTING
} bootJoinCheck_e;

typedef enum profileAction {
	PROFILE_CONNECT,
	PROFILE_EDIT,
	PROFILE_DELETE
} profileAction_e;

typedef void (*bootRawCallback_t)(void*, u8*, int, void*);

static void startMenu(int player);
static void showChoices(void* unused);
static void showJoin(void* unused);
static void chooseJoin(void* unused);
static void leaveProfiles(void* unused);
static void editName(void* unused);
static void showEndpoint(void* unused);
static void selectEndpoint(void* unused);
static void editEndpoint(void* unused);
static void deleteEndpoint(void* unused);
static void saveEndpoint(void* unused);
static void cancelEndpoint(void* unused);
static void chooseSplit(void* unused);
static void showChoicesNow(void* unused);
static void backFromSave(void* unused);
static void returnAfterDisconnect(void (*destination)(void*));
static void updateDisconnect(void);
static void enterMultiplayer(void* unused);
static void enterSingleplayer(void* unused);
static void chooseHost(void* unused);
static void showHostProfiles(void* unused);
static void showProfiles(void);
static void openProfileEditor(void);
static void removeProfile(void);
static void profileMenuInput(int player);
static void drawProfileHelp(void);
static int profileSlot(void);
static u32 profileHeld(void);
static void editAddress(void* unused);
static void editField(coopConnectionField_e which);
static void editPort(void* unused);
static void confirmJoin(void* unused);
static void beginConnection(int command);
static void showDisclaimer(void);
static void updateJoinCheck(void);
static void newGameReady(void);
static int loadedGameReady(void);
static void updateHostLoad(void);
static void submitFrame(void);
static void releasePad(void);

static const moduleApi_t* loader;
static volatile coopControl_t* control;
static bootPage_e page;
static int intent, pending, installed, editing, releasing;
static bootJoinCheck_e joinCheck;
static int joinStep, joinError, joinRendered, stopJoin;
static int connectionCommand;
static void (*afterDisconnect)(void*);
static int disconnectFrames;
static u32 joinRequest;
static char joinMessage[192];
static u32 startAddress, frameCall, savedStart[2], savedFrame;
static u32 trampoline[4] __attribute__((aligned(16)));
static u32 saveAddress[2], saveWords[2][2];
static u32 saveTrampoline[2][4] __attribute__((aligned(16)));
static u32 switchLevelAddress;
static int waitingForHostLoad;
static int bootLevel;
static struct PAD* capturedPad;
static bootRawCallback_t originalPad;
static u8 sample[32];
static u32 previousButtons;
static u32 multiplayerAddress, multiplayerWords[2];
static u32 multiplayerTrampoline[4] __attribute__((aligned(16)));
static int pendingBootAction;
static u32 singleplayerAddress, singleplayerWords[2];
static u32 singleplayerTrampoline[4] __attribute__((aligned(16)));
static char addressLabel[40], portLabel[32], nameLabel[32];
static char endpointLabels[4][32];
static int selectedEndpoint, profileRole = ONLINE_JOIN;
static profileAction_e profileAction;
static u32 lastProfileButtons;
static int deleteSlot = -1;
static char deleteMessage[96];
static u32 profileInputAddress, profileInputWords[2];
static u32 profileInputTrampoline[4] __attribute__((aligned(16)));
static bootButton_t choices[] = {
	{1, "SPLIT SCREEN", chooseSplit},
	{1, "HOST", chooseHost},
	{1, "JOIN", chooseJoin}
};
static bootButton_t savedButtons[] = {
	{1, endpointLabels[0], selectEndpoint},
	{1, endpointLabels[1], selectEndpoint},
	{1, endpointLabels[2], selectEndpoint},
	{1, endpointLabels[3], selectEndpoint},
	{1, "EDIT", editEndpoint},
	{1, "DELETE", deleteEndpoint}
};
static bootButton_t joinButtons[] = {
	{1, nameLabel, editName},
	{1, addressLabel, editAddress},
	{1, portLabel, editPort},
	{1, "CREATE", saveEndpoint}
};

/*
 * NAME : bootPresent
 * DESCRIPTION : Verify that the native boot overlay is loaded.
 * NOTES : The first mission shares level 1, so its mission sentinel matters.
 * ARGS :
 * RETURN : Nonzero for the boot overlay only.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int bootPresent(void)
{
	u32 address;
	if (Level > 1 || (Level == 1 && Mission->ActiveMission != -1))
		return 0;
	address = BOOT_START;
	return address && (WORD(address) == 0x27bdffd0 ||
	    (installed && WORD(address) == (0x08000000 | ((u32)startMenu >> 2))));
}

/*
 * NAME : capturePad
 * DESCRIPTION : Keep native menus from receiving keypad input.
 * NOTES : The original callback still runs with neutral buttons and sticks.
 * ARGS : Native raw-controller callback arguments.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void capturePad(void* pad, u8* data, int size, void* context)
{
	int i;
	for (i = 0; i < 32; ++i)
		sample[i] = i < size ? data[i] : 0;
	for (i = 2; i < size; ++i)
		data[i] = i < 4 ? 255 : i < 8 ? 127 : 0;
	if (originalPad)
		originalPad(pad, data, size, context);
}

/*
 * NAME : releasePad
 * DESCRIPTION : Restore only the controller callback owned by this popup.
 * NOTES : Never replace a callback installed by another module afterward.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void releasePad(void)
{
	if (capturedPad && capturedPad->RawPadInputCallback == capturePad)
		capturedPad->RawPadInputCallback = originalPad;
	capturedPad = 0;
	originalPad = 0;
	editing = releasing = 0;
}

/*
 * NAME : nativeMenu
 * DESCRIPTION : Replace this screen's buttons using the game's normal menu.
 * NOTES : Closing first updates the native back callback as well as the rows.
 * ARGS : Buttons, count and back callback.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void nativeMenu(bootButton_t* buttons, int count, void (*back)(void*))
{
	NATIVE(BOOT_CLOSE, void (*)(int))(0);
	NATIVE(BOOT_CURSOR, void (*)(int, int))(0, 0);
	NATIVE(BOOT_TARGET, void (*)(int, float, float, float, float))(0, .685f, .28f,
	    (buttons == joinButtons || buttons == savedButtons) ? .52f : .37109375f, .048076924f);
	NATIVE(BOOT_HEADER, void (*)(int, int))(0, 0);
	NATIVE(BOOT_BUTTONS, void (*)(int, int, bootButton_t*))(0, count, buttons);
	NATIVE(BOOT_SHOW, void (*)(int, void (*)(void*)))(0, back);
}

/*
 * NAME : showChoices
 * DESCRIPTION : Return to the three Cooperative connection choices.
 * NOTES : Cancels a connection intent before a save has loaded.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void showChoices(void* unused)
{
	(void)unused;
	returnAfterDisconnect(showChoicesNow);
}

/*
 * NAME : showChoicesNow
 * DESCRIPTION : Draw Cooperative choices after any session has shut down.
 * NOTES :
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void showChoicesNow(void* unused)
{
	(void)unused;
	page = BOOT_CHOICES;
	intent = pending = 0;
	nativeMenu(choices, 3, NATIVE(BOOT_EXIT, void (*)(void*)));
}

/*
 * NAME : loadMenu
 * DESCRIPTION : Open the original New Game and Load Game choices.
 * NOTES : Native save dialogs and their loading transitions remain intact.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void loadMenu(void)
{
	int cursor;
	page = BOOT_SAVE;
	NATIVE(BOOT_CLOSE, void (*)(int))(0);
	((void (*)(int))trampoline)(0);
	/* Preserve the original buttons, replacing only Back. */
	cursor = NATIVE(BOOT_CURSOR_GET, int (*)(int))(0);
	NATIVE(BOOT_CLOSE, void (*)(int))(0);
	NATIVE(BOOT_CURSOR, void (*)(int, int))(0, cursor < 0 ? 0 : cursor);
	NATIVE(BOOT_SHOW, void (*)(int, void (*)(void*)))(0,
	    backFromSave);
}

/*
 * NAME : returnAfterDisconnect
 * DESCRIPTION : Show progress before backing out of an active boot session.
 * NOTES : Native noninteractive dialog input blocks repeated Triangle presses.
 * ARGS : Screen to open once the socket is closed.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void returnAfterDisconnect(void (*destination)(void*))
{
	if (afterDisconnect)
		return;
	if (coopConnectionStopped()) {
		destination(0);
		return;
	}
	afterDisconnect = destination;
	disconnectFrames = 0;
	intent = pending = joinCheck = waitingForHostLoad = stopJoin = pendingBootAction = 0;
	coopConnectionDisconnect();
	NATIVE(BOOT_DIALOG, void (*)(int, const char*, void (*)(void*), int, int, int, float, int))
	    (0, "Disconnecting...", 0, 1, 0, 0, .30f, 1);
}

/*
 * NAME : backFromSave
 * DESCRIPTION : Disconnect before returning from save selection to Host or Join.
 * NOTES : The save is not loaded or changed by this Back action.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void backFromSave(void* unused)
{
	(void)unused;
	returnAfterDisconnect(intent == ONLINE_JOIN ? showJoin : intent == ONLINE_HOST ? chooseHost : showChoicesNow);
}

/*
 * NAME : updateDisconnect
 * DESCRIPTION : Close the progress popup only after transport shutdown completes.
 * NOTES : Allows the native popup opening animation to finish before returning.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void updateDisconnect(void)
{
	void (*destination)(void*);
	if (!afterDisconnect || disconnectFrames < BOOT_DISCONNECT_FRAMES || !coopConnectionStopped())
		return;
	destination = afterDisconnect;
	afterDisconnect = 0;
	NATIVE(BOOT_ABORT_DIALOG, void (*)(int))(0);
	destination(0);
}

/*
 * NAME : enterSingleplayer
 * DESCRIPTION : Disconnect before opening the native Single Player save flow.
 * NOTES : Cooperative has a separate native callback and remains connected.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void enterSingleplayer(void* unused)
{
	(void)unused;
	intent = pending = 0;
	pendingBootAction = 1;
	coopConnectionDisconnect();
}

/*
 * NAME : enterMultiplayer
 * DESCRIPTION : Disconnect before entering the native Multiplayer boot flow.
 * NOTES : The original callback runs only after Stop has been acknowledged.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void enterMultiplayer(void* unused)
{
	(void)unused;
	intent = pending = 0;
	pendingBootAction = 2;
	coopConnectionDisconnect();
}

/*
 * NAME : chooseSplit
 * DESCRIPTION : Keep ordinary two-controller split-screen Cooperative play.
 * NOTES : The online adapter is disabled for this selection.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void chooseSplit(void* unused)
{
	(void)unused;
	control->enabled = 0;
	coopConnectionDisconnect();
	intent = pending = 0;
	loadMenu();
}

/*
 * NAME : chooseHost
 * DESCRIPTION : Open the four saved Host profiles.
 * NOTES :
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void chooseHost(void* unused)
{
	if (page == BOOT_CHOICES)
		coopUiPlaySound(COOP_UI_OPEN);
	showHostProfiles(unused);
}

/*
 * NAME : fieldLabel
 * DESCRIPTION : Join a fixed field label and its bounded connection value.
 * NOTES : Destinations hold the prefix plus at most 15 address characters.
 * ARGS : Destination, label and connection field.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void fieldLabel(char* text, const char* prefix, coopConnectionField_e which)
{
	const char* value = coopConnectionValue(which);
	while (*prefix)
		*text++ = *prefix++;
	while (*value)
		*text++ = *value++;
	*text = 0;
}

/*
 * NAME : showHostProfiles
 * DESCRIPTION : Return to Host connection profiles without changing saved endpoints.
 * NOTES :
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void showHostProfiles(void* unused)
{
	(void)unused;
	profileRole = ONLINE_HOST;
	profileAction = PROFILE_CONNECT;
	intent = pending = 0;
	showProfiles();
}

/*
 * NAME : chooseJoin
 * DESCRIPTION : Play the native open sound when entering saved connections.
 * NOTES : Refreshing the list after edits does not replay this sound.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void chooseJoin(void* unused)
{
	coopUiPlaySound(COOP_UI_OPEN);
	showJoin(unused);
}

/*
 * NAME : leaveProfiles
 * DESCRIPTION : Back out of a profile action or return to Cooperative choices.
 * NOTES : Leaving a live session still uses the disconnect popup.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void leaveProfiles(void* unused)
{
	coopUiPlaySound(COOP_UI_CLOSE);
	if (profileAction != PROFILE_CONNECT) {
		profileAction = PROFILE_CONNECT;
		showProfiles();
	} else {
		showChoices(unused);
	}
}

/*
 * NAME : editName
 * DESCRIPTION : Edit the selected profile name with the standalone keyboard.
 * NOTES : Uses the same native input capture as the numeric fields.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void editName(void* unused)
{
	(void)unused;
	editField(COOP_CONNECTION_NAME);
}

/*
 * NAME : showJoin
 * DESCRIPTION : Return to the four saved Join profiles.
 * NOTES :
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void showJoin(void* unused)
{
	(void)unused;
	profileRole = ONLINE_JOIN;
	profileAction = PROFILE_CONNECT;
	intent = pending = 0;
	showProfiles();
}

/*
 * NAME : profileSlot
 * DESCRIPTION : Resolve the selected Host or Join profile.
 * NOTES :
 * ARGS :
 * RETURN : Absolute profile index, Join zero through three or Host four through seven.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int profileSlot(void)
{
	return selectedEndpoint + (profileRole == ONLINE_HOST ? 4 : 0);
}

/*
 * NAME : profileHeld
 * DESCRIPTION : Read native pad buttons without owning another UI input hook.
 * NOTES :
 * ARGS :
 * RETURN : Held PAD bits, or zero without a connected controller.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 profileHeld(void)
{
	struct PAD* pad = padGetPad(0);
	return pad && !pad->disconnected ? (~(pad->rdata[2] | (pad->rdata[3] << 8))) & 0xffff : 0;
}

/*
 * NAME : showProfiles
 * DESCRIPTION : Draw the common four-slot Host/Join list.
 * NOTES : Edit and Delete selection modes hide the two action buttons.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void showProfiles(void)
{
	int i, j;
	page = profileRole == ONLINE_HOST ? BOOT_HOST : BOOT_JOIN;
	for (i = 0; i < 4; ++i) {
		const char* text = coopConnectionProfileLabel(i + (profileRole == ONLINE_HOST ? 4 : 0));
		j = 0;
		while (text[j] && j < 31) {
			endpointLabels[i][j] = text[j];
			++j;
		}
		endpointLabels[i][j] = 0;
	}
	lastProfileButtons = profileHeld();
	nativeMenu(savedButtons, profileAction == PROFILE_CONNECT ? 6 : 4, leaveProfiles);
	NATIVE(BOOT_CURSOR, void (*)(int, int))(0, selectedEndpoint);
}

/*
 * NAME : openProfileEditor
 * DESCRIPTION : Open the shared editor with the selected profile values.
 * NOTES : Empty slots use the same editor and default port.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void openProfileEditor(void)
{
	joinButtons[3].text = !strcmp(coopConnectionProfileLabel(profileSlot()), "[EMPTY]") ? "CREATE" : "SAVE";
	coopConnectionProfileBegin(profileSlot());
	coopUiPlaySound(COOP_UI_OPEN);
	showEndpoint(0);
}

/*
 * NAME : confirmProfileDelete
 * DESCRIPTION : Delete the named profile only after an affirmative response.
 * NOTES : No and Triangle leave the profile unchanged.
 * ARGS : Native Yes/No selection.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void confirmProfileDelete(void* selection)
{
	if ((int)selection == 1 && deleteSlot >= 0) {
		coopConnectionProfileDelete(deleteSlot);
		coopUiPlaySound(COOP_UI_DELETE);
	}
	deleteSlot = -1;
	profileAction = PROFILE_CONNECT;
	showProfiles();
}

/*
 * NAME : removeProfile
 * DESCRIPTION : Confirm the selected profile before deleting its saved endpoint.
 * NOTES : Identifies the list, slot and profile name; empty slots need no dialog.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void removeProfile(void)
{
	if (!strcmp(coopConnectionProfileLabel(profileSlot()), "[EMPTY]")) {
		profileAction = PROFILE_CONNECT;
		showProfiles();
		return;
	}
	deleteSlot = profileSlot();
	{
		const char* prefix = profileRole == ONLINE_HOST ? "Delete Host profile " : "Delete Join profile ";
		const char* name = coopConnectionProfileLabel(deleteSlot);
		int length = 0;
		while (*prefix)
			deleteMessage[length++] = *prefix++;
		deleteMessage[length++] = '1' + selectedEndpoint;
		deleteMessage[length++] = '?';
		deleteMessage[length++] = 1;
		while (*name && length < (int)sizeof(deleteMessage) - 1)
			deleteMessage[length++] = *name++;
		deleteMessage[length] = 0;
	}
	coopUiPlaySound(COOP_UI_OPEN);
	NATIVE(BOOT_DIALOG, void (*)(int, const char*, void (*)(void*), int, int, int, float, int))
	    (0, deleteMessage, confirmProfileDelete, 3, 0, 0, -1.0f, 0);
}

/*
 * NAME : showEndpoint
 * DESCRIPTION : Refresh the selected endpoint editor.
 * NOTES :
 * ARGS : Unused callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void showEndpoint(void* unused)
{
	(void)unused;
	page = BOOT_EDIT;
	fieldLabel(nameLabel, "NAME: ", COOP_CONNECTION_NAME);
	fieldLabel(addressLabel, "IP ADDRESS: ", COOP_CONNECTION_ADDRESS);
	fieldLabel(portLabel, "PORT: ", COOP_CONNECTION_HOST_PORT);
	nativeMenu(joinButtons, 4, cancelEndpoint);
}

/*
 * NAME : editEndpoint
 * DESCRIPTION : Choose a profile to edit from a profiles-only list.
 * NOTES :
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void editEndpoint(void* unused)
{
	(void)unused;
	profileAction = PROFILE_EDIT;
	coopUiPlaySound(COOP_UI_OPEN);
	showProfiles();
}

/*
 * NAME : discardEndpoint
 * DESCRIPTION : Leave the editor only when discarding changes is confirmed.
 * NOTES : No or Triangle keeps the current draft open.
 * ARGS : Native Yes/No selection.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void discardEndpoint(void* selection)
{
	if ((int)selection != 1)
		return;
	coopConnectionProfileEnd(profileSlot(), 0);
	coopUiPlaySound(COOP_UI_CLOSE);
	profileAction = PROFILE_CONNECT;
	showProfiles();
}

/*
 * NAME : cancelEndpoint
 * DESCRIPTION : Warn before leaving a profile with unsaved changes.
 * NOTES : Unchanged profiles close immediately.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void cancelEndpoint(void* unused)
{
	(void)unused;
	if (!coopConnectionProfileChanged(profileSlot())) {
		discardEndpoint((void*)1);
		return;
	}
	coopUiPlaySound(COOP_UI_OPEN);
	NATIVE(BOOT_DIALOG, void (*)(int, const char*, void (*)(void*), int, int, int, float, int))
	    (0, "You have unsaved changes.\x01" "Quit without saving?", discardEndpoint, 3, 0, 0, -1.0f, 0);
}

/*
 * NAME : saveEndpoint
 * DESCRIPTION : Save profile edits and return to the correct connection list.
 * NOTES :
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void saveEndpoint(void* unused)
{
	(void)unused;
	coopConnectionProfileEnd(profileSlot(), 1);
	coopUiPlaySound(COOP_UI_CLOSE);
	profileAction = PROFILE_CONNECT;
	showProfiles();
}

/*
 * NAME : deleteEndpoint
 * DESCRIPTION : Choose a profile to delete from a profiles-only list.
 * NOTES :
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void deleteEndpoint(void* unused)
{
	(void)unused;
	profileAction = PROFILE_DELETE;
	coopUiPlaySound(COOP_UI_OPEN);
	showProfiles();
}

/*
 * NAME : selectEndpoint
 * DESCRIPTION : Connect, edit or delete the selected profile.
 * NOTES : Host and Join use separate stored endpoints.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void selectEndpoint(void* unused)
{
	int cursor = NATIVE(BOOT_CURSOR_GET, int (*)(int))(0);
	(void)unused;
	if ((u32)cursor >= 4)
		return;
	selectedEndpoint = cursor;
	if (profileAction == PROFILE_DELETE) {
		removeProfile();
	} else if (profileAction == PROFILE_EDIT || !strcmp(coopConnectionProfileLabel(profileSlot()), "[EMPTY]")) {
		openProfileEditor();
	} else {
		coopConnectionProfileSelect(profileSlot());
		if (profileRole == ONLINE_HOST)
			beginConnection(ONLINE_HOST);
		else
			confirmJoin(0);
	}
}

/*
 * NAME : profileMenuInput
 * DESCRIPTION : Handle Square/Circle shortcuts before native menu commands.
 * NOTES : Held shortcut buttons are consumed once; dialogs and keypad input are untouched.
 * ARGS : Native menu player index.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void profileMenuInput(int player)
{
	u32 held = profileHeld(), pressed = held & ~lastProfileButtons;
	int cursor;
	lastProfileButtons = held;
	if (player == 0 && (page == BOOT_HOST || page == BOOT_JOIN) && !editing && !releasing &&
	    !joinCheck && !afterDisconnect && !NATIVE(BOOT_DIALOG_SHOWN, int (*)(int))(0)) {
		cursor = NATIVE(BOOT_CURSOR_GET, int (*)(int))(0);
		if ((u32)cursor < 4)
			selectedEndpoint = cursor;
		if (held & (PAD_SQUARE | PAD_CIRCLE)) {
			if ((u32)cursor < 4) {
				if (pressed & PAD_SQUARE)
					openProfileEditor();
				else if (pressed & PAD_CIRCLE)
					removeProfile();
			}
			return;
		}
	}
	((void (*)(int))profileInputTrampoline)(player);
}

/*
 * NAME : drawProfileHelp
 * DESCRIPTION : Draw controller glyphs below the connection-profile lists.
 * NOTES : Uses the safe boot graphics-submission pass; no Mod Menu dependency.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void drawProfileHelp(void)
{
	const char* text;
	float width, scale;
	if ((page != BOOT_HOST && page != BOOT_JOIN) || editing || releasing || joinCheck || afterDisconnect ||
	    NATIVE(BOOT_DIALOG_SHOWN, int (*)(int))(0))
		return;
	text = profileAction == PROFILE_EDIT ? "\x10 EDIT   \x12 BACK" :
	    profileAction == PROFILE_DELETE ? "\x10 DELETE   \x12 BACK" :
	    "\x10 CONNECT   \x13 EDIT   \x11 DELETE";
	width = gfxGetFontWidth(text, -1, 1);
	scale = width > SCREEN_WIDTH - 24 ? (SCREEN_WIDTH - 24) / width : 1;
	gfxScreenSpaceBox(0, (SCREEN_HEIGHT - 40.0f) / SCREEN_HEIGHT, 1, 40.0f / SCREEN_HEIGHT, 0x60000000);
	gfxScreenSpaceText((SCREEN_WIDTH - width * scale) * .5f, SCREEN_HEIGHT - 27.0f,
	    scale, scale, 0x80d0d0d0, text, -1, 0);
}

/*
 * NAME : editField
 * DESCRIPTION : Open the standalone keypad over the native profile editor.
 * NOTES : Blocks native controls until the closing buttons are released.
 * ARGS : IP address or host port field.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void editField(coopConnectionField_e which)
{
	struct PAD* pad = padGetPad(0);
	if (!pad || capturedPad || !coopConnectionOpen(which))
		return;
	capturedPad = pad;
	originalPad = (bootRawCallback_t)pad->RawPadInputCallback;
	memcpy(sample, pad->rdata, sizeof(sample));
	previousButtons = (~(sample[2] | (sample[3] << 8))) & 0xffff;
	editing = which == COOP_CONNECTION_NAME ? 2 : 1;
	pad->RawPadInputCallback = capturePad;
}

/*
 * NAME : editAddress
 * DESCRIPTION : Edit the saved host IP address.
 * NOTES :
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void editAddress(void* unused)
{
	(void)unused;
	editField(COOP_CONNECTION_ADDRESS);
}

/*
 * NAME : editPort
 * DESCRIPTION : Edit the saved host port.
 * NOTES :
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void editPort(void* unused)
{
	(void)unused;
	editField(COOP_CONNECTION_HOST_PORT);
}

/*
 * NAME : acceptJoin
 * DESCRIPTION : Require confirmation before choosing the guest save.
 * NOTES : Cancel disconnects; Continue keeps the checked connection while choosing a save.
 * ARGS : Native dialog selection, zero Cancel and one Continue.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void acceptJoin(void* selection)
{
	if (!(int)selection) {
		returnAfterDisconnect(showJoin);
		return;
	}
	control->enabled = 1;
	intent = ONLINE_JOIN;
	pending = 1;
	loadMenu();
}

/*
 * NAME : cancelHostLoad
 * DESCRIPTION : Cancel a selected-save handoff before loading a level.
 * NOTES : The native save operation already completed; this stops only the join.
 * ARGS : Unused native dialog selection.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void cancelHostLoad(void* unused)
{
	(void)unused;
	waitingForHostLoad = 0;
	intent = pending = 0;
	stopJoin = 1;
	page = BOOT_CHOICES;
	loader->print("coop: host level load cancelled\n");
	NATIVE(BOOT_EXIT, void (*)(void*))(0);
}

/*
 * NAME : holdHostLoad
 * DESCRIPTION : Keep the native save setup but hold its first level transition.
 * NOTES : Restores the boot level identity until a fresh host mission is available.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void holdHostLoad(void)
{
	if (intent != ONLINE_JOIN || pending != 1 || !WORD(BOOT_NEW_LEVEL_FLAG))
		return;
	WORD(BOOT_NEW_LEVEL_FLAG) = 0;
	/* Reading a save can already have replaced Level before its completion callback. */
	Level = bootLevel;
	waitingForHostLoad = 1;
	loader->print("coop: guest save selected; waiting for host mission\n");
	NATIVE(BOOT_DIALOG, void (*)(int, const char*, void (*)(void*), int, int, int, float, int))
	    (0, "Loading host session...\x01" "Waiting for the host session to finish loading.",
	    cancelHostLoad, 2, 0, 0, .30f, 1);
	NATIVE(BOOT_TEXT, void (*)(u32, const char*))(0x130006, "CANCEL");
}

/*
 * NAME : newGameReady
 * DESCRIPTION : Redirect only a joining guest after native New Game setup.
 * NOTES : Split Screen and Host keep the original transition.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void newGameReady(void)
{
	((void (*)(void))saveTrampoline[0])();
	holdHostLoad();
}

/*
 * NAME : loadedGameReady
 * DESCRIPTION : Redirect only a joining guest after native save loading succeeds.
 * NOTES : A failed native load has no transition request and remains unchanged.
 * ARGS :
 * RETURN : Original native callback result.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int loadedGameReady(void)
{
	int result = ((int (*)(void))saveTrampoline[1])();
	holdHostLoad();
	return result;
}

/*
 * NAME : updateHostLoad
 * DESCRIPTION : Cold-load the host's current supported mission as the guest's first level.
 * NOTES : No extra save is requested; host menus and stale snapshots keep waiting.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void updateHostLoad(void)
{
	coopHostSession_t session;
	const onlineApi_t* online;
	if (!waitingForHostLoad)
		return;
	online = onlineFind(loader);
	if (!online || online->control->status != ONLINE_CONNECTED) {
		if (online && online->control->status == ONLINE_FAILED && online->control->error == -6 &&
		    !coopConnectionRequest(ONLINE_JOIN))
			loader->print("coop: reconnecting while waiting for the host level\n");
		return;
	}
	if (!coopWorldGetHostSession(&session))
		return;
	waitingForHostLoad = 0;
	loader->print("coop: direct host load: level=%d mission=%d link=%d\n",
	    session.level, session.mission, session.missionLink);
	/* The native cold-load path initializes both co-op players and the selected slot. */
	((void (*)(int, int, int))switchLevelAddress)(session.level, 0, session.mission);
}

/*
 * NAME : showDisclaimer
 * DESCRIPTION : Warn after a host connection has been established.
 * NOTES : Uses the native confirmation dialog with explicit button labels.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void showDisclaimer(void)
{
	NATIVE(BOOT_DIALOG, void (*)(int, const char*, void (*)(void*), int, int, int, float, int))
	    (0, "Choose a New Game or Load Game next. The selected save will be overwritten by the host's session. Continue?",
	    acceptJoin, 7, 0, 0, -1.0f, 1);
	NATIVE(BOOT_TEXT, void (*)(u32, const char*))(0x130006, "Cancel");
	NATIVE(BOOT_TEXT, void (*)(u32, const char*))(0x130008, "Continue");
}

/*
 * NAME : appendText
 * DESCRIPTION : Append a fixed diagnostic string to the popup buffer.
 * NOTES : All callers use bounded static strings; the buffer holds 192 bytes.
 * ARGS : Destination and source text.
 * RETURN : The terminating byte.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static char* appendText(char* output, const char* text)
{
	while (*text)
		*output++ = *text++;
	*output = 0;
	return output;
}

/*
 * NAME : setJoinStep
 * DESCRIPTION : Record the last connection step for both the popup and log.
 * NOTES : Native line breaks are 0x01; C newlines select an inline font color.
 * ARGS : Step 1 through 4 and zero or a transport failure code.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void setJoinStep(int step, int error)
{
	static const char* steps[] = {
		"Preparing connection...", "Loading network drivers...",
		"Obtaining a local IP address...", "Contacting the host..."
	};
	char* text;
	char number[16];
	if (joinStep == step && joinError == error)
		return;
	joinStep = step;
	joinError = error;
	text = appendText(joinMessage, connectionCommand == ONLINE_HOST ?
	    (error ? "Unable to create host session.\x01" "Step " : "Creating host session...\x01" "Step ") :
	    (error ? "Unable to join host session.\x01" "Step " : "Joining host session...\x01" "Step "));
	*text++ = '0' + step;
	text = appendText(text, "/4: ");
	text = appendText(text, connectionCommand == ONLINE_HOST && step == 4 ? "Opening host port..." : steps[step - 1]);
	if (error) {
		text = appendText(text, "\x01" "Error: ");
		if (error < 0)
			*text++ = '-';
		coopNumpadFormat(COOP_NUMPAD_PORT, error < 0 ? 0 - (u32)error : (u32)error, number);
		appendText(text, number);
	}
	loader->print("coop: join step %d/4: %s error=%d\n", step, steps[step - 1], error);
}

/*
 * NAME : cancelJoinCheck
 * DESCRIPTION : Close the progress/failure popup and stop this connection.
 * NOTES : Cleanup waits for an outstanding transport request to be acknowledged.
 * ARGS : Unused native dialog selection.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void cancelJoinCheck(void* unused)
{
	(void)unused;
	joinCheck = JOIN_CHECK_NONE;
	stopJoin = 0;
	loader->print("coop: join check closed at step %d/4\n", joinStep);
	returnAfterDisconnect(connectionCommand == ONLINE_HOST ? showHostProfiles : showJoin);
}

/*
 * NAME : failJoinCheck
 * DESCRIPTION : Leave the failed step and error visible until acknowledged.
 * NOTES : Does not retry or advance to the save disclaimer on failure.
 * ARGS : Transport failure code.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void failJoinCheck(int error)
{
	joinCheck = JOIN_CHECK_NONE;
	stopJoin = 1;
	setJoinStep(joinStep, error);
	NATIVE(BOOT_DIALOG, void (*)(int, const char*, void (*)(void*), int, int, int, float, int))
	    (0, joinMessage, cancelJoinCheck, 2, 0, 0, .30f, 1);
	NATIVE(BOOT_TEXT, void (*)(u32, const char*))(0x130006, "OKAY");
}

/*
 * NAME : confirmJoin
 * DESCRIPTION : Show connection progress before contacting the host.
 * NOTES : Waits for the first rendered frame before queuing driver startup.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void confirmJoin(void* unused)
{
	(void)unused;
	if (!strcmp(coopConnectionValue(COOP_CONNECTION_ADDRESS), "[none]")) {
		editAddress(0);
		return;
	}
	beginConnection(ONLINE_JOIN);
}

/*
 * NAME : beginConnection
 * DESCRIPTION : Show progress before creating or joining a session.
 * NOTES : Save selection opens only after the requested transport is ready.
 * ARGS : Host or Join command.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void beginConnection(int command)
{
	connectionCommand = command;
	joinCheck = JOIN_CHECK_REQUEST;
	joinRendered = joinStep = joinError = 0;
	setJoinStep(1, 0);
	NATIVE(BOOT_DIALOG, void (*)(int, const char*, void (*)(void*), int, int, int, float, int))
	    (0, joinMessage, cancelJoinCheck, 2, 0, 0, .30f, 0);
	NATIVE(BOOT_TEXT, void (*)(u32, const char*))(0x130006, "CANCEL");
}

/*
 * NAME : updateJoinCheck
 * DESCRIPTION : Advance the boot-menu handshake without blocking native UI.
 * NOTES : Only an acknowledged successful connection opens the disclaimer.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void updateJoinCheck(void)
{
	const onlineApi_t* online = onlineFind(loader);
	const volatile onlineControl_t* state = online ? online->control : 0;
	if (stopJoin && state && state->requestId == state->acceptedId) {
		if (state->status == ONLINE_IDLE || !coopConnectionRequest(ONLINE_STOP))
			stopJoin = 0;
	}
	if (!joinCheck || !joinRendered)
		return;
	if (!state) {
		failJoinCheck(-1);
		return;
	}
	if (joinCheck == JOIN_CHECK_REQUEST) {
		if (stopJoin || state->requestId != state->acceptedId)
			return;
		if (state->status != ONLINE_IDLE && state->status != ONLINE_FAILED) {
			if (!coopConnectionRequest(ONLINE_STOP))
				joinCheck = JOIN_CHECK_STOPPING;
			return;
		}
		if (coopConnectionRequest(connectionCommand)) {
			failJoinCheck(-2);
			return;
		}
		joinRequest = state->requestId;
		joinCheck = JOIN_CHECK_CONNECTING;
	} else if (joinCheck == JOIN_CHECK_STOPPING) {
		if (state->requestId == state->acceptedId && state->status == ONLINE_IDLE)
			joinCheck = JOIN_CHECK_REQUEST;
		else if (state->requestId == state->acceptedId && state->status == ONLINE_FAILED)
			failJoinCheck(state->error ? state->error : -3);
	} else if (state->acceptedId == joinRequest) {
		if (state->error || state->status == ONLINE_FAILED) {
			failJoinCheck(state->error ? state->error : -3);
			return;
		}
		if (state->status == ONLINE_CONNECTED ||
		    (connectionCommand == ONLINE_HOST && state->status == ONLINE_HOSTING)) {
			joinCheck = JOIN_CHECK_NONE;
			if (connectionCommand == ONLINE_HOST) {
				control->enabled = 1;
				intent = ONLINE_HOST;
				pending = 1;
				loader->print("coop: host session ready; choosing save\n");
				NATIVE(BOOT_ABORT_DIALOG, void (*)(int))(0);
				loadMenu();
				return;
			}
			loader->print("coop: host connected; awaiting save confirmation\n");
			showDisclaimer();
			return;
		}
		if (state->status == ONLINE_STARTING)
			setJoinStep(2, 0);
		else if (state->status == ONLINE_ADDRESS)
			setJoinStep(3, 0);
		else if (state->status == ONLINE_CONNECTING)
			setJoinStep(4, 0);
	}
	NATIVE(BOOT_TEXT, void (*)(u32, const char*))(0x130004, joinMessage);
}

/*
 * NAME : startMenu
 * DESCRIPTION : Add connection choices only to the Cooperative boot submenu.
 * NOTES : Single-player Campaign retains its original behavior.
 * ARGS : Native local player index.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void startMenu(int player)
{
	if (NATIVE(BOOT_GET_MODE, int (*)(void))() != 1) {
		coopConnectionDisconnect();
		intent = pending = 0;
		page = BOOT_CHOICES;
		((void (*)(int))trampoline)(player);
		return;
	}
	if (page == BOOT_SAVE)
		loadMenu();
	else {
		((void (*)(int))trampoline)(player);
		showChoices(0);
	}
}

/*
 * NAME : submitFrame
 * DESCRIPTION : Draw the standalone keypad before graphics-chain submission.
 * NOTES : Hooks a boot call site, leaving Mod Menu's frame hook untouched.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void submitFrame(void)
{
	int canvas = ((int (*)(void))BOOT_CANVAS_GET)();
	u32 held = (~(sample[2] | (sample[3] << 8))) & 0xffff;
	((void (*)(int))BOOT_CANVAS_SET)(0);
	if ((page == BOOT_JOIN || page == BOOT_HOST) && joinCheck == JOIN_CHECK_NONE) {
		int cursor = NATIVE(BOOT_CURSOR_GET, int (*)(int))(0);
		if ((u32)cursor < 4)
			selectedEndpoint = cursor;
	}
	updateDisconnect();
	updateJoinCheck();
	updateHostLoad();
	if (editing) {
		u32 pressed = held & ~previousButtons;
		previousButtons = held;
		(editing == 2 ? coopKeyboardMove : coopNumpadMove)((sample[1] & 0xf0) == 0x70 ? sample[4] : 127,
		    (sample[1] & 0xf0) == 0x70 ? sample[5] : 127);
		(editing == 2 ? coopKeyboardNavigate : coopNumpadNavigate)((sample[1] & 0xf0) == 0x70 ? sample[6] : 127,
		    (sample[1] & 0xf0) == 0x70 ? sample[7] : 127);
		if (coopConnectionInput(pressed, held)) {
			editing = 0;
			releasing = 1;
			showEndpoint(0);
		} else {
			coopConnectionDraw();
		}
	} else if (releasing && !held) {
		releasePad();
	}
	drawProfileHelp();
	((void (*)(int))BOOT_CANVAS_SET)(canvas);
	((void (*)(void))BOOT_FRAME_SUBMIT)();
	joinRendered = 1;
	if (afterDisconnect && disconnectFrames < BOOT_DISCONNECT_FRAMES)
		++disconnectFrames;
}

/*
 * NAME : coopBootInit
 * DESCRIPTION : Retain module-owned APIs for the native boot UI.
 * NOTES : No menus or networking are opened during initialization.
 * ARGS : Loader and this module's control record.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopBootInit(const moduleApi_t* api, volatile coopControl_t* state)
{
	loader = api;
	control = state;
}

/*
 * NAME : coopBootDeactivate
 * DESCRIPTION : Restore only native boot hooks installed by this module.
 * NOTES : Overlay replacement is detected before restoring code.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopBootDeactivate(void)
{
	int i;
	releasePad();
	if (installed && bootPresent()) {
		if (WORD(startAddress) == (0x08000000 | ((u32)startMenu >> 2))) {
			WORD(startAddress) = savedStart[0];
			WORD(startAddress + 4) = savedStart[1];
		}
		if (WORD(profileInputAddress) == (0x08000000 | ((u32)profileMenuInput >> 2))) {
			WORD(profileInputAddress) = profileInputWords[0];
			WORD(profileInputAddress + 4) = profileInputWords[1];
		}
		if (WORD(singleplayerAddress) == (0x08000000 | ((u32)enterSingleplayer >> 2))) {
			WORD(singleplayerAddress) = singleplayerWords[0];
			WORD(singleplayerAddress + 4) = singleplayerWords[1];
		}
		if (WORD(multiplayerAddress) == (0x08000000 | ((u32)enterMultiplayer >> 2))) {
			WORD(multiplayerAddress) = multiplayerWords[0];
			WORD(multiplayerAddress + 4) = multiplayerWords[1];
		}
		if (WORD(frameCall) == (0x0c000000 | ((u32)submitFrame >> 2)))
			WORD(frameCall) = savedFrame;
		for (i = 0; i < 2; ++i) {
			u32 callback = i ? (u32)loadedGameReady : (u32)newGameReady;
			if (WORD(saveAddress[i]) == (0x08000000 | (callback >> 2))) {
				WORD(saveAddress[i]) = saveWords[i][0];
				WORD(saveAddress[i] + 4) = saveWords[i][1];
			}
		}
		loader->flushCache(0);
		loader->flushCache(2);
	}
	installed = intent = pending = joinCheck = stopJoin = 0;
	waitingForHostLoad = pendingBootAction = 0;
	afterDisconnect = 0;
	page = BOOT_CHOICES;
}

/*
 * NAME : coopBootUpdate
 * DESCRIPTION : Install native boot hooks and monitor deferred session startup.
 * NOTES : Unsupported maps keep the request deferred rather than touching actors.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopBootUpdate(void)
{
	const onlineApi_t* online;
	int i;
	if (!bootPresent()) {
		/* Save reading changes Level before the boot overlay itself is replaced. */
		if (!startAddress || WORD(startAddress) != (0x08000000 | ((u32)startMenu >> 2)))
			installed = 0;
		releasePad();
		online = onlineFind(loader);
		if (pending == 2 && online) {
			if (!control->enabled || online->control->command == ONLINE_STOP ||
			    online->control->status == ONLINE_CONNECTED ||
			    (intent == ONLINE_HOST && online->control->status == ONLINE_HOSTING))
				pending = 0;
			else if (intent == ONLINE_JOIN && online->control->status == ONLINE_FAILED &&
			    online->control->error == -6)
				pending = 1; /* The host may still be loading its chosen save. */
		}
		if (pending == 1 && coopWorldReady()) {
			if (online && ((intent == ONLINE_JOIN && online->control->status == ONLINE_CONNECTED) ||
			    (intent == ONLINE_HOST && (online->control->status == ONLINE_HOSTING ||
			    online->control->status == ONLINE_CONNECTED)))) {
				pending = 0;
				loader->print("coop: save loaded; using prepared connection\n");
			} else if (!coopConnectionRequest(intent)) {
				pending = 2;
				loader->print("coop: boot session requested (%d)\n", intent);
			}
		}
		return;
	}
	if (installed) {
		if (pendingBootAction && coopConnectionStopped()) {
			int action = pendingBootAction;
			pendingBootAction = 0;
			((void (*)(void*))(action == 1 ? singleplayerTrampoline : multiplayerTrampoline))(0);
			return;
		}
		online = onlineFind(loader);
		if (NATIVE(BOOT_GET_MODE, int (*)(void))() != 1 && online &&
		    online->control->status != ONLINE_IDLE && online->control->status != ONLINE_FAILED &&
		    online->control->command != ONLINE_STOP)
			coopConnectionDisconnect();
		return;
	}
	startAddress = BOOT_START;
	profileInputAddress = BOOT_MENU_INPUT;
	multiplayerAddress = BOOT_MULTIPLAYER;
	singleplayerAddress = BOOT_SINGLEPLAYER;
	bootLevel = Level;
	frameCall = BOOT_FRAME_CALL;
	switchLevelAddress = GetAddressImmediate(&vaSwitchToLevelFunc);
	saveAddress[0] = BOOT_NEW_GAME;
	saveAddress[1] = BOOT_LOAD_DONE;
	savedFrame = 0x0c000000 | (BOOT_FRAME_SUBMIT >> 2);
	/* Displaced instructions contain no PC-relative control flow. */
	if (WORD(startAddress) != 0x27bdffd0 ||
	    (WORD(startAddress + 4) & 0xffff0000) != 0x3c020000 || WORD(frameCall) != savedFrame || !profileInputAddress ||
	    WORD(profileInputAddress) != 0x27bdffe0 || (WORD(profileInputAddress + 4) & 0xffff0000) != 0x3c020000)
		return;
	for (i = 0; i < 2; ++i)
		if (WORD(saveAddress[i]) != 0x27bdfff0 || WORD(saveAddress[i] + 4) != 0xffb00000)
			return;
	if (!switchLevelAddress || WORD(switchLevelAddress) != 0x27bdffd0 ||
	    !multiplayerAddress || WORD(multiplayerAddress) != 0x27bdfff0 || WORD(multiplayerAddress + 4) != 0x0000202d ||
	    !singleplayerAddress || WORD(singleplayerAddress) != 0x27bdfff0 || WORD(singleplayerAddress + 4) != 0x2404000b)
		return;
	profileInputWords[0] = profileInputTrampoline[0] = WORD(profileInputAddress);
	profileInputWords[1] = profileInputTrampoline[1] = WORD(profileInputAddress + 4);
	profileInputTrampoline[2] = 0x08000000 | ((profileInputAddress + 8) >> 2);
	profileInputTrampoline[3] = 0;
	WORD(profileInputAddress) = 0x08000000 | ((u32)profileMenuInput >> 2);
	WORD(profileInputAddress + 4) = 0;
	savedStart[0] = trampoline[0] = WORD(startAddress);
	savedStart[1] = trampoline[1] = WORD(startAddress + 4);
	trampoline[2] = 0x08000000 | ((startAddress + 8) >> 2);
	trampoline[3] = 0;
	for (i = 0; i < 2; ++i) {
		u32 callback = i ? (u32)loadedGameReady : (u32)newGameReady;
		saveWords[i][0] = saveTrampoline[i][0] = WORD(saveAddress[i]);
		saveWords[i][1] = saveTrampoline[i][1] = WORD(saveAddress[i] + 4);
		saveTrampoline[i][2] = 0x08000000 | ((saveAddress[i] + 8) >> 2);
		saveTrampoline[i][3] = 0;
		WORD(saveAddress[i]) = 0x08000000 | (callback >> 2);
		WORD(saveAddress[i] + 4) = 0;
	}
	singleplayerWords[0] = singleplayerTrampoline[0] = WORD(singleplayerAddress);
	singleplayerWords[1] = singleplayerTrampoline[1] = WORD(singleplayerAddress + 4);
	singleplayerTrampoline[2] = 0x08000000 | ((singleplayerAddress + 8) >> 2);
	singleplayerTrampoline[3] = 0;
	WORD(singleplayerAddress) = 0x08000000 | ((u32)enterSingleplayer >> 2);
	WORD(singleplayerAddress + 4) = 0;
	multiplayerWords[0] = multiplayerTrampoline[0] = WORD(multiplayerAddress);
	multiplayerWords[1] = multiplayerTrampoline[1] = WORD(multiplayerAddress + 4);
	multiplayerTrampoline[2] = 0x08000000 | ((multiplayerAddress + 8) >> 2);
	multiplayerTrampoline[3] = 0;
	WORD(multiplayerAddress) = 0x08000000 | ((u32)enterMultiplayer >> 2);
	WORD(multiplayerAddress + 4) = 0;
	WORD(startAddress) = 0x08000000 | ((u32)startMenu >> 2);
	WORD(startAddress + 4) = 0;
	WORD(frameCall) = 0x0c000000 | ((u32)submitFrame >> 2);
	installed = 1;
	page = BOOT_CHOICES;
	loader->flushCache(0);
	loader->flushCache(2);
	loader->print("coop: Cooperative boot menu installed\n");
}
