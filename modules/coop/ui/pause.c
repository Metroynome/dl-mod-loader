#include "pause.h"
#include "pause-addresses.h"
#include "connection.h"
#include "session.h"
#include <librac4/game.h>

#define WORD(address) (*(volatile u32*)(address))
#define NATIVE(address, type) ((type)GetAddressImmediate(&(address)))
#define MENU_ROWS (8)

typedef struct coopPauseButton {
	/* 0x00 */ u32 enabled;
	/* 0x04 */ const char* text;
	/* 0x08 */ void (*callback)(void*);
} coopPauseButton_t;

static void activateRow(void* unused);
static void disconnect(void* unused);
static void more(void* unused);
static void back(void* unused);
static void hostOptions(void* unused);
static void quit(void* unused);
static void confirmQuit(void* selection);
static void setButtons(int player, int count, coopPauseButton_t* buttons);
static void render(int reopen);

static const onlineApi_t* transport;
static u32 hookCall, originalCall;
static int ownerLevel, menuPlayer, originalCount, page, currentRole, pendingQuit;
static coopPauseButton_t originalRows[MENU_ROWS], displayRows[MENU_ROWS];
static int originalIndex[MENU_ROWS], displayCount, disconnectEnabled;

/*
 * NAME : sessionRole
 * DESCRIPTION : Include a listening host when deciding which controls to show.
 * NOTES : Starting connections can also be disconnected before a peer arrives.
 * ARGS :
 * RETURN : Host, Join or zero for an inactive session.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int sessionRole(void)
{
	const volatile onlineControl_t* state = transport ? transport->control : 0;
	if (!state || state->status == ONLINE_IDLE || state->status == ONLINE_FAILED || state->command == ONLINE_STOP)
		return 0;
	return state->command == ONLINE_HOST ? ONLINE_HOST : ONLINE_JOIN;
}

/*
 * NAME : appendRow
 * DESCRIPTION : Append one bounded native pause row.
 * NOTES : Eight native widgets are available; extra actions use a second page.
 * ARGS : Source row, native index or minus one for a module action.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void appendRow(const coopPauseButton_t* row, int index)
{
	if (displayCount >= MENU_ROWS)
		return;
	displayRows[displayCount] = *row;
	originalIndex[displayCount] = index;
	if (index >= 0)
		displayRows[displayCount].callback = activateRow;
	++displayCount;
}

/*
 * NAME : render
 * DESCRIPTION : Keep every original pause action reachable within eight rows.
 * NOTES : Host Options is its own submenu, ready for future host controls.
 * ARGS : Nonzero to replace an already open page and its Back callback.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void render(int reopen)
{
	static const coopPauseButton_t host = {1, "HOST OPTIONS", hostOptions};
	coopPauseButton_t leave = {0, "DISCONNECT", disconnect};
	static const coopPauseButton_t next = {1, "MORE...", more};
	static const coopPauseButton_t previous = {1, "BACK", back};
	int i, extra = currentRole == ONLINE_HOST ? 2 : 1;
	int split = originalCount + extra > MENU_ROWS;
	int first = split ? MENU_ROWS - extra - 2 : originalCount - 1;
	if (originalCount < 1 || originalCount > MENU_ROWS)
		return;
	disconnectEnabled = coopConnectionCanDisconnect();
	leave.enabled = disconnectEnabled;
	displayCount = 0;
	if (!currentRole)
		page = 0;
	if (page == 2) {
		appendRow(&previous, -1);
	} else if (page == 1) {
		for (i = first; i < originalCount - 1; ++i)
			appendRow(&originalRows[i], i);
		appendRow(&previous, -1);
	} else {
		for (i = 0; i < first; ++i)
			appendRow(&originalRows[i], i);
		if (currentRole == ONLINE_HOST)
			appendRow(&host, -1);
		appendRow(&leave, -1);
		if (split)
			appendRow(&next, -1);
		appendRow(&originalRows[originalCount - 1], originalCount - 1);
	}
	if (reopen) {
		NATIVE(vaCoopPauseClose, void (*)(int))(menuPlayer);
		NATIVE(vaCoopPauseCursor, void (*)(int, int))(menuPlayer, 0);
	}
	NATIVE(vaCoopPauseTitle, void (*)(int, const char*))(menuPlayer, page == 2 ? "HOST OPTIONS" : 0);
	NATIVE(vaCoopPauseButtons, void (*)(int, int, coopPauseButton_t*))(menuPlayer, displayCount, displayRows);
	if (reopen)
		NATIVE(vaCoopPauseShow, void (*)(int, void (*)(void*)))(menuPlayer,
		    page ? back : originalRows[originalCount - 1].callback);
}

/*
 * NAME : activateRow
 * DESCRIPTION : Restore the native cursor index before invoking its action.
 * NOTES : Original menu callbacks keep their normal remembered selection.
 * ARGS : Native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void activateRow(void* context)
{
	int row = NATIVE(vaCoopPauseCursorGet, int (*)(int))(menuPlayer);
	int index;
	if ((u32)row >= (u32)displayCount)
		return;
	index = originalIndex[row];
	if ((u32)index >= (u32)originalCount || !originalRows[index].enabled || !originalRows[index].callback)
		return;
	NATIVE(vaCoopPauseCursor, void (*)(int, int))(menuPlayer, index);
	originalRows[index].callback(context);
}

/*
 * NAME : disconnect
 * DESCRIPTION : Disconnect the current peer without leaving the loaded level.
 * NOTES : The normal neutral-input behavior takes over after Stop completes.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void disconnect(void* unused)
{
	(void)unused;
	if (coopConnectionCanDisconnect())
		coopConnectionDisconnect();
}

/*
 * NAME : more
 * DESCRIPTION : Open the remaining original pause actions.
 * NOTES :
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void more(void* unused)
{
	(void)unused;
	page = 1;
	render(1);
}

/*
 * NAME : back
 * DESCRIPTION : Return from a module submenu to the main pause page.
 * NOTES :
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void back(void* unused)
{
	(void)unused;
	page = 0;
	render(1);
}

/*
 * NAME : hostOptions
 * DESCRIPTION : Open the host-only options submenu.
 * NOTES : No host settings are implemented yet.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void hostOptions(void* unused)
{
	(void)unused;
	if (sessionRole() != ONLINE_HOST)
		return;
	page = 2;
	render(1);
}

/*
 * NAME : quit
 * DESCRIPTION : Ask for confirmation before disconnecting and quitting.
 * NOTES : Cancel retains the session; Yes waits for transport shutdown.
 * ARGS : Unused native callback context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void quit(void* unused)
{
	(void)unused;
	NATIVE(vaCoopPauseDialog, void (*)(int, const char*, void (*)(void*), int, int, int, float, int))
	    (menuPlayer, "Quit to the main menu?", confirmQuit, 3, 0, 0, -1.0f, 0);
}

/*
 * NAME : confirmQuit
 * DESCRIPTION : Defer confirmed native Quit until the socket has closed.
 * NOTES : No leaves the session intact and uses the native cancel sound.
 * ARGS : Zero for No, nonzero for Yes.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void confirmQuit(void* selection)
{
	if (!selection) {
		NATIVE(vaCoopPauseQuitConfirm, void (*)(void*))(0);
		return;
	}
	pendingQuit = 1;
	coopConnectionDisconnect();
}

/*
 * NAME : setButtons
 * DESCRIPTION : Extend Main and protect Load/Quit in the Options menu.
 * NOTES : Runs after Mod Menu has supplied its normal native row.
 * ARGS : Native player, row count and native row array.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void setButtons(int player, int count, coopPauseButton_t* buttons)
{
	int i, screen = coopSessionScreen();
	currentRole = sessionRole();
	if (screen == 1 && count > 0 && count <= MENU_ROWS) {
		menuPlayer = player;
		originalCount = count;
		page = 0;
		for (i = 0; i < count; ++i)
			originalRows[i] = buttons[i];
		render(0);
		return;
	}
	if (currentRole && screen == 6 && count > 0 && count <= MENU_ROWS) {
		menuPlayer = player;
		for (i = 0; i < count; ++i) {
			if ((u32)buttons[i].callback == GetAddressImmediate(&vaCoopPauseLoad))
				buttons[i].enabled = 0;
			if ((u32)buttons[i].callback == GetAddressImmediate(&vaCoopPauseQuit))
				buttons[i].callback = quit;
		}
	}
	NATIVE(vaCoopPauseButtons, void (*)(int, int, coopPauseButton_t*))(player, count, buttons);
}

/*
 * NAME : coopPauseReset
 * DESCRIPTION : Restore only this module's native button-list call site.
 * NOTES : Does not undo Mod Menu's separate AddButton call hook.
 * ARGS : Loader API.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopPauseReset(const moduleApi_t* loader)
{
	if (hookCall && WORD(hookCall) == (0x0c000000 | ((u32)setButtons >> 2))) {
		WORD(hookCall) = originalCall;
		loader->flushCache(0);
		loader->flushCache(2);
	}
	hookCall = 0;
	ownerLevel = originalCount = pendingQuit = 0;
}

/*
 * NAME : coopPauseUpdate
 * DESCRIPTION : Install menu edits and finish asynchronous disconnect/quit.
 * NOTES : Overlay changes invalidate cached native rows before reuse.
 * ARGS : Loader and optional transport API.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopPauseUpdate(const moduleApi_t* loader, const onlineApi_t* online)
{
	u32 call, buttons;
	int role;
	transport = online;
	if (ownerLevel && ownerLevel != Level)
		coopPauseReset(loader);
	if (Level != 21 && Level != 22)
		return;
	call = GetAddressImmediate(&vaCoopPauseButtonsCall);
	buttons = GetAddressImmediate(&vaCoopPauseButtons);
	if (!hookCall && call && buttons && WORD(call) == (0x0c000000 | (buttons >> 2))) {
		hookCall = call;
		ownerLevel = Level;
		originalCall = WORD(call);
		WORD(call) = 0x0c000000 | ((u32)setButtons >> 2);
		loader->flushCache(0);
		loader->flushCache(2);
	}
	if (!hookCall)
		return;
	if (pendingQuit && coopConnectionStopped()) {
		pendingQuit = 0;
		NATIVE(vaCoopPauseQuitConfirm, void (*)(void*))((void*)1);
		return;
	}
	role = sessionRole();
	if (originalCount && (currentRole != role || disconnectEnabled != coopConnectionCanDisconnect()) &&
	    coopSessionScreen() == 1) {
		currentRole = role;
		page = 0;
		render(1);
	}
}
