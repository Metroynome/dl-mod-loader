#include "connection.h"
#include "numpad.h"
#include "modules/modmenu/include/modmenu.h"

static int editorModal(u32 pressed);
static void registerMenu(void);

static const moduleApi_t* loader;
static const onlineApi_t* transport;
static const modMenuApi_t* menu;
static int ip[4], hostPort = 25565, localPort = 25566;
static int field = -1, tab, ownsModal, configStatus, status, pending, requestError;
static u32 pendingId;
static int disconnectPending;
static int profiles[8][5] = {
	{0, 0, 0, 0, 25565}, {0, 0, 0, 0, 25565}, {0, 0, 0, 0, 25565}, {0, 0, 0, 0, 25565},
	{0, 0, 0, 0, 25565}, {0, 0, 0, 0, 25565}, {0, 0, 0, 0, 25565}, {0, 0, 0, 0, 25565}
};
static int profileVersion;
static int editBackup[5], profileEditing;
static const moduleConfigField_t settings[] = {
	{"ip0", &ip[0], 0, 255},
	{"ip1", &ip[1], 0, 255},
	{"ip2", &ip[2], 0, 255},
	{"ip3", &ip[3], 0, 255},
	{"hostPort", &hostPort, 1, 65535},
	{"localPort", &localPort, 1, 65535},
	{"profileVersion", &profileVersion, 0, 1},
	{"join0ip0", &profiles[0][0], 0, 255},
	{"join0ip1", &profiles[0][1], 0, 255},
	{"join0ip2", &profiles[0][2], 0, 255},
	{"join0ip3", &profiles[0][3], 0, 255},
	{"join0port", &profiles[0][4], 1, 65535},
	{"join1ip0", &profiles[1][0], 0, 255},
	{"join1ip1", &profiles[1][1], 0, 255},
	{"join1ip2", &profiles[1][2], 0, 255},
	{"join1ip3", &profiles[1][3], 0, 255},
	{"join1port", &profiles[1][4], 1, 65535},
	{"join2ip0", &profiles[2][0], 0, 255},
	{"join2ip1", &profiles[2][1], 0, 255},
	{"join2ip2", &profiles[2][2], 0, 255},
	{"join2ip3", &profiles[2][3], 0, 255},
	{"join2port", &profiles[2][4], 1, 65535},
	{"join3ip0", &profiles[3][0], 0, 255},
	{"join3ip1", &profiles[3][1], 0, 255},
	{"join3ip2", &profiles[3][2], 0, 255},
	{"join3ip3", &profiles[3][3], 0, 255},
	{"join3port", &profiles[3][4], 1, 65535},
	{"hostIp0", &profiles[4][0], 0, 255},
	{"hostIp1", &profiles[4][1], 0, 255},
	{"hostIp2", &profiles[4][2], 0, 255},
	{"hostIp3", &profiles[4][3], 0, 255},
	{"listenPort", &profiles[4][4], 1, 65535}
};

static const moduleConfigField_t hostSettings[] = {
	{"profile0ip0", &profiles[4][0], 0, 255},
	{"profile0ip1", &profiles[4][1], 0, 255},
	{"profile0ip2", &profiles[4][2], 0, 255},
	{"profile0ip3", &profiles[4][3], 0, 255},
	{"profile0port", &profiles[4][4], 1, 65535},
	{"profile1ip0", &profiles[5][0], 0, 255},
	{"profile1ip1", &profiles[5][1], 0, 255},
	{"profile1ip2", &profiles[5][2], 0, 255},
	{"profile1ip3", &profiles[5][3], 0, 255},
	{"profile1port", &profiles[5][4], 1, 65535},
	{"profile2ip0", &profiles[6][0], 0, 255},
	{"profile2ip1", &profiles[6][1], 0, 255},
	{"profile2ip2", &profiles[6][2], 0, 255},
	{"profile2ip3", &profiles[6][3], 0, 255},
	{"profile2port", &profiles[6][4], 1, 65535},
	{"profile3ip0", &profiles[7][0], 0, 255},
	{"profile3ip1", &profiles[7][1], 0, 255},
	{"profile3ip2", &profiles[7][2], 0, 255},
	{"profile3ip3", &profiles[7][3], 0, 255},
	{"profile3port", &profiles[7][4], 1, 65535}
};

/*
 * NAME : addressValue
 * DESCRIPTION : Build the transport's four-byte IPv4 value.
 * NOTES :
 * ARGS :
 * RETURN : Zero when no address is selected.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 addressValue(void)
{
	return ip[0] | (ip[1] << 8) | (ip[2] << 16) | ((u32)ip[3] << 24);
}

/*
 * NAME : valueLabel
 * DESCRIPTION : Display one editable connection value.
 * NOTES : Zero addresses show none until their editor opens.
 * ARGS : Field index and unused menu value.
 * RETURN : Formatted address or port.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static const char* valueLabel(void* context, int unused)
{
	static char text[16];
	int which = (int)context;
	u32 value = which == COOP_CONNECTION_ADDRESS ? addressValue() :
	    (u32)(which == COOP_CONNECTION_HOST_PORT ? hostPort : localPort);
	(void)unused;
	if (which == COOP_CONNECTION_ADDRESS && !value)
		return "[none]";
	coopNumpadFormat(which == COOP_CONNECTION_ADDRESS ? COOP_NUMPAD_ADDRESS : COOP_NUMPAD_PORT, value, text);
	return text;
}

/*
 * NAME : coopConnectionValue
 * DESCRIPTION : Format a connection field for the native boot menu.
 * NOTES : Copy the returned text before requesting another field.
 * ARGS : Address, host port or local port.
 * RETURN : Shared text buffer or the unset-address label.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
const char* coopConnectionValue(coopConnectionField_e which)
{
	return (u32)which <= COOP_CONNECTION_LOCAL_PORT ? valueLabel((void*)which, 0) : "";
}

/*
 * NAME : coopConnectionProfileLabel
 * DESCRIPTION : Show a connection name without changing the active endpoint.
 * NOTES : Names use the placeholder until a boot-compatible keyboard is available.
 * ARGS : Slot index.
 * RETURN : Profile name or the empty-slot label.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
const char* coopConnectionProfileLabel(int slot)
{
	if ((u32)slot >= 8)
		return "";
	if (!(profiles[slot][0] || profiles[slot][1] || profiles[slot][2] || profiles[slot][3]))
		return "[EMPTY]";
	return COOP_CONNECTION_DEFAULT_NAME;
}

/*
 * NAME : coopConnectionProfileSelect
 * DESCRIPTION : Apply a saved endpoint for joining.
 * NOTES :
 * ARGS : Slot index.
 * RETURN : Nonzero for a populated slot.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopConnectionProfileSelect(int slot)
{
	int i;
	if ((u32)slot >= 8)
		return 0;
	for (i = 0; i < 4; ++i)
		ip[i] = profiles[slot][i];
	hostPort = profiles[slot][4];
	return addressValue() != 0;
}

/*
 * NAME : coopConnectionProfileBegin
 * DESCRIPTION : Stage an endpoint in the existing keypad fields.
 * NOTES :
 * ARGS : Slot index.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopConnectionProfileBegin(int slot)
{
	int i;
	if (profileEditing || (u32)slot >= 8)
		return;
	for (i = 0; i < 4; ++i)
		editBackup[i] = ip[i];
	editBackup[4] = hostPort;
	profileEditing = 1;
	coopConnectionProfileSelect(slot);
}

/*
 * NAME : coopConnectionProfileChanged
 * DESCRIPTION : Compare the open profile draft against its saved values.
 * NOTES : Returning fields to their original values clears the unsaved state.
 * ARGS : Profile slot.
 * RETURN : Nonzero when address or port edits would be lost.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopConnectionProfileChanged(int slot)
{
	int i;
	if (!profileEditing || (u32)slot >= 8)
		return 0;
	for (i = 0; i < 4; ++i)
		if (ip[i] != profiles[slot][i])
			return 1;
	return hostPort != profiles[slot][4];
}

/*
 * NAME : coopConnectionProfileEnd
 * DESCRIPTION : Save an edited slot or discard its draft.
 * NOTES :
 * ARGS : Slot and nonzero to save.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopConnectionProfileEnd(int slot, int save)
{
	int i;
	if (!profileEditing || (u32)slot >= 8)
		return;
	if (save) {
		for (i = 0; i < 4; ++i)
			profiles[slot][i] = ip[i];
		profiles[slot][4] = hostPort;
	}
	for (i = 0; i < 4; ++i)
		ip[i] = editBackup[i];
	hostPort = editBackup[4];
	profileEditing = 0;
	if (save)
		coopConnectionSave();
}

/*
 * NAME : coopConnectionProfileDelete
 * DESCRIPTION : Clear only the selected saved endpoint.
 * NOTES :
 * ARGS : Slot index.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopConnectionProfileDelete(int slot)
{
	int i;
	if ((u32)slot >= 8)
		return;
	for (i = 0; i < 4; ++i)
		profiles[slot][i] = 0;
	profiles[slot][4] = 25565;
	coopConnectionSave();
}

/*
 * NAME : stateLabel
 * DESCRIPTION : Label the transport or settings status.
 * NOTES :
 * ARGS : Nonzero selects settings status; current value is ignored.
 * RETURN : Status text.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static const char* stateLabel(void* context, int unused)
{
	static const char* states[] = {"Idle", "Loading drivers", "Obtaining IP", "Hosting", "Connecting", "Connected", "Failed"};
	static const char* configs[] = {"Defaults", "Loaded", "Saved", "No saved settings", "Invalid settings", "File error"};
	(void)unused;
	if (context)
		return configs[configStatus];
	if (requestError)
		return requestError == 1 ? "Choose an IP address" : "Request not accepted";
	return transport && status >= ONLINE_IDLE && status <= ONLINE_FAILED ? states[status] : "Online not loaded";
}

/*
 * NAME : coopConnectionOpen
 * DESCRIPTION : Open the shared editor without depending on Mod Menu.
 * NOTES : The caller must supply capture, input edges and drawing.
 * ARGS : Address, host port or local port.
 * RETURN : Nonzero opened, zero invalid or already editing.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopConnectionOpen(coopConnectionField_e which)
{
	char text[16];
	coopNumpadKind_e kind = which == COOP_CONNECTION_ADDRESS ? COOP_NUMPAD_ADDRESS : COOP_NUMPAD_PORT;
	if (field >= 0 || (u32)which > COOP_CONNECTION_LOCAL_PORT)
		return 0;
	requestError = 0;
	field = which;
	coopNumpadFormat(kind, which == COOP_CONNECTION_ADDRESS ? addressValue() :
	    (u32)(which == COOP_CONNECTION_HOST_PORT ? hostPort : localPort), text);
	coopNumpadOpen(kind, text, 1, 65535);
	return 1;
}

/*
 * NAME : coopConnectionInput
 * DESCRIPTION : Commit the edited value only after a valid Done action.
 * NOTES : Cancelling leaves the configured defaults unchanged.
 * ARGS : Newly pressed and held PAD bits from the owning UI.
 * RETURN : Zero editing, one accepted, negative cancelled.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopConnectionInput(u32 pressed, u32 held)
{
	u32 value;
	int result, i;
	if (field < 0)
		return -1;
	result = coopNumpadInput(pressed, held);
	if (result > 0) {
		value = coopNumpadValue();
		if (field == COOP_CONNECTION_ADDRESS) {
			for (i = 0; i < 4; ++i)
				ip[i] = (value >> (i * 8)) & 255;
		} else if (field == COOP_CONNECTION_HOST_PORT) {
			hostPort = value;
		} else {
			localPort = value;
		}
	}
	if (result)
		field = -1;
	return result;
}

/*
 * NAME : coopConnectionDraw
 * DESCRIPTION : Draw the editor from the owning UI's native render pass.
 * NOTES : Can be called with Mod Menu absent.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopConnectionDraw(void)
{
	if (field >= 0)
		coopNumpadDraw();
}

/*
 * NAME : editorModal
 * DESCRIPTION : Adapt Mod Menu's captured input and safe drawing callback.
 * NOTES : Mod Menu keeps closing buttons blocked until released.
 * ARGS : Newly pressed buttons.
 * RETURN : Nonzero while the modal remains open.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int editorModal(u32 pressed)
{
	u32 stick = menu->getRightStick();
	coopNumpadMove(stick & 255, (stick >> 8) & 255);
	stick = menu->getLeftStick();
	coopNumpadNavigate(stick & 255, (stick >> 8) & 255);
	if (coopConnectionInput(pressed, menu->getButtons())) {
		ownsModal = 0;
		return 0;
	}
	coopConnectionDraw();
	return 1;
}

/*
 * NAME : openField
 * DESCRIPTION : Open a normal highlighted field with X.
 * NOTES : Never steal another module's modal.
 * ARGS : Field index.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void openField(void* context)
{
	if (field < 0 && menu && menu->setModal(editorModal)) {
		if (coopConnectionOpen((coopConnectionField_e)context))
			ownsModal = 1;
		else
			menu->setModal(0);
	}
}

/*
 * NAME : coopConnectionLoad
 * DESCRIPTION : Load saved connection defaults through the loader.
 * NOTES : Each file loads transactionally; missing files preserve current defaults.
 * ARGS :
 * RETURN : Loader configuration result.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopConnectionLoad(void)
{
	int hostResult;
	int result = loader->loadConfig("coop-online", settings, sizeof(settings) / sizeof(settings[0]));
	if ((result == DL_CONFIG_OK || result == DL_CONFIG_MISSING) && !profileVersion) {
		int i, j;
		for (i = 0; i < 4; ++i)
			profiles[i][4] = 25565;
		for (j = 0; j < 4; ++j)
			profiles[0][j] = ip[j];
		profiles[0][4] = hostPort;
		for (j = 0; j < 4; ++j)
			profiles[4][j] = ip[j];
		profiles[4][4] = hostPort;
		profileVersion = 1;
	}
	if (result == DL_CONFIG_OK || result == DL_CONFIG_MISSING) {
		/* An older file supplies host slot zero until the separate list is saved. */
		hostResult = loader->loadConfig("coop-host", hostSettings, sizeof(hostSettings) / sizeof(hostSettings[0]));
		if (hostResult < DL_CONFIG_MISSING || hostResult == DL_CONFIG_OK)
			result = hostResult;
	}
	configStatus = result == DL_CONFIG_OK ? 1 : result == DL_CONFIG_MISSING ? 3 : result == DL_CONFIG_INVALID ? 4 : 5;
	return result;
}

/*
 * NAME : coopConnectionSave
 * DESCRIPTION : Save address and port defaults independently of Mod Menu.
 * NOTES : Only connection settings are saved, never live session state.
 * ARGS :
 * RETURN : Loader configuration result.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopConnectionSave(void)
{
	int result = loader->saveConfig("coop-online", settings, sizeof(settings) / sizeof(settings[0]));
	if (result == DL_CONFIG_OK)
		result = loader->saveConfig("coop-host", hostSettings, sizeof(hostSettings) / sizeof(hostSettings[0]));
	configStatus = result == DL_CONFIG_OK ? 2 : 5;
	return result;
}

/*
 * NAME : configAction
 * DESCRIPTION : Handle the optional side-by-side Load and Save buttons.
 * NOTES :
 * ARGS : Zero loads; nonzero saves.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void configAction(void* save)
{
	if (save)
		coopConnectionSave();
	else
		coopConnectionLoad();
}

/*
 * NAME : coopConnectionRequest
 * DESCRIPTION : Submit connection defaults to the independent transport.
 * NOTES : Edits do not change the current session until Host or Join.
 * ARGS : Host, join or disconnect command.
 * RETURN : Zero queued, negative unavailable or busy.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopConnectionRequest(u32 command)
{
	int result;
	if (!transport)
		return -1;
	result = transport->request(command, addressValue(), hostPort, localPort);
	requestError = result ? (command == ONLINE_JOIN && !addressValue() ? 1 : 2) : 0;
	if (!result) {
		pending = command;
		pendingId = transport->control->requestId;
	}
	return result;
}

/*
 * NAME : coopConnectionDisconnect
 * DESCRIPTION : Queue disconnect without overwriting an in-flight request.
 * NOTES : Update submits Stop when the transport acknowledges its current command.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopConnectionDisconnect(void)
{
	disconnectPending = transport != 0;
}

/*
 * NAME : coopConnectionCanDisconnect
 * DESCRIPTION : Enable Disconnect only for a live peer or listening host.
 * NOTES : Idle, failed, starting and already stopping sessions stay disabled.
 * ARGS :
 * RETURN : Nonzero when Disconnect is available.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopConnectionCanDisconnect(void)
{
	return !disconnectPending && transport && transport->control->command != ONLINE_STOP &&
	    (transport->control->status == ONLINE_HOSTING || transport->control->status == ONLINE_CONNECTED);
}

/*
 * NAME : coopConnectionStopped
 * DESCRIPTION : Check that a queued disconnect has completed.
 * NOTES : Stop acknowledgement alone is insufficient; the socket must be closed.
 * ARGS :
 * RETURN : Nonzero when no session or outstanding command remains.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopConnectionStopped(void)
{
	return !disconnectPending && (!transport ||
	    (transport->control->requestId == transport->control->acceptedId &&
	    transport->control->status == ONLINE_IDLE));
}

/*
 * NAME : connectionAction
 * DESCRIPTION : Submit the highlighted transport action.
 * NOTES :
 * ARGS : Transport command.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void connectionAction(void* context)
{
	if ((u32)context != ONLINE_STOP || coopConnectionCanDisconnect())
		coopConnectionRequest((u32)context);
}

/*
 * NAME : registerMenu
 * DESCRIPTION : Expose optional connection fields in the Online tab.
 * NOTES : The editor and persistence remain usable without this tab.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void registerMenu(void)
{
	static const modMenuButton_t actions[] = {
		{"Host", connectionAction, (void*)ONLINE_HOST},
		{"Join", connectionAction, (void*)ONLINE_JOIN},
		{"Disconnect", connectionAction, (void*)ONLINE_STOP}
	};
	static const modMenuButton_t configs[] = {
		{"Load Settings", configAction, 0},
		{"Save Settings", configAction, (void*)1}
	};
	static const char* labels[] = {"IP Address", "Host Port", "Local Port"};
	modMenuItem_t row = {"", MODMENU_BUTTON_ROW, 0, 0, 0, 0, 0, 0};
	modMenuItem_t connection = {"Connection", MODMENU_RANGE, &status, ONLINE_IDLE, ONLINE_FAILED, 1, 0, 0};
	modMenuItem_t config = {"Settings", MODMENU_RANGE, &configStatus, 0, 5, 1, 0, (void*)1};
	int i, item;
	if (tab)
		return;
	menu = modmenuFind(loader);
	if (!menu)
		return;
	tab = menu->addTab("online", "Online", DL_MODE_CAMPAIGN);
	if (tab <= 0) {
		tab = 0;
		return;
	}
	menu->setTabContexts(tab, DL_CONTEXT_CAMPAIGN);
	menu->setItemButtons(menu->addItem(tab, &row), actions, 3);
	item = menu->addItem(tab, &connection);
	menu->setItemValueLabel(item, stateLabel);
	menu->setItemEnabled(item, 0);
	for (i = 0; i < 3; ++i) {
		modMenuItem_t entry = {labels[i], MODMENU_FIELD, i == 0 ? 0 : i == 1 ? &hostPort : &localPort,
		    1, 65535, 1, openField, (void*)i};
		item = menu->addItem(tab, &entry);
		if (i == 0)
			menu->setItemValueLabel(item, valueLabel);
	}
	menu->setItemButtons(menu->addItem(tab, &row), configs, 2);
	item = menu->addItem(tab, &config);
	menu->setItemValueLabel(item, stateLabel);
	menu->setItemEnabled(item, 0);
}

/*
 * NAME : coopConnectionInit
 * DESCRIPTION : Load connection defaults once per game start.
 * NOTES : No drivers or transport commands run during initialization.
 * ARGS : Loader API.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopConnectionInit(const moduleApi_t* api)
{
	loader = api;
	coopConnectionLoad();
	if (configStatus == 3)
		configStatus = 0;
}

/*
 * NAME : coopConnectionUpdate
 * DESCRIPTION : Discover optional UI and monitor requested connections.
 * NOTES : Return-to-game uses Mod Menu's normal input-release barrier.
 * ARGS : Optional Online transport API.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopConnectionUpdate(const onlineApi_t* online)
{
	transport = online;
	status = transport ? transport->control->status : ONLINE_IDLE;
	registerMenu();
	if (disconnectPending && transport && transport->control->requestId == transport->control->acceptedId) {
		if (status == ONLINE_IDLE || !coopConnectionRequest(ONLINE_STOP))
			disconnectPending = 0;
	}
	if (pending && transport && transport->control->acceptedId == pendingId) {
		if (transport->control->error || status == ONLINE_FAILED || pending == ONLINE_STOP)
			pending = 0;
		else if ((pending == ONLINE_HOST && status == ONLINE_HOSTING) || status == ONLINE_CONNECTED) {
			pending = 0;
			if (menu && menu->isOpen())
				menu->returnToGame();
		}
	}
}

/*
 * NAME : coopConnectionDeactivate
 * DESCRIPTION : Release only UI owned by this module.
 * NOTES : Cancels uncommitted input and keeps saved/current defaults.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopConnectionDeactivate(void)
{
	if (menu && ownsModal)
		menu->setModal(0);
	if (menu && tab)
		menu->removeTab(tab);
	coopConnectionProfileEnd(0, 0);
	field = -1;
	menu = 0;
	transport = 0;
	tab = ownsModal = pending = 0;
}
