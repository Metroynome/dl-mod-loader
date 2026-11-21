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
static const moduleConfigField_t settings[] = {
	{"ip0", &ip[0], 0, 255},
	{"ip1", &ip[1], 0, 255},
	{"ip2", &ip[2], 0, 255},
	{"ip3", &ip[3], 0, 255},
	{"hostPort", &hostPort, 1, 65535},
	{"localPort", &localPort, 1, 65535}
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
 * NOTES : A missing file preserves current values; loading is transactional.
 * ARGS :
 * RETURN : Loader configuration result.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopConnectionLoad(void)
{
	int result = loader->loadConfig("coop-online", settings, sizeof(settings) / sizeof(settings[0]));
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
 * NAME : connectionAction
 * DESCRIPTION : Submit the highlighted transport action.
 * NOTES :
 * ARGS : Transport command.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void connectionAction(void* context)
{
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
	if (pending && transport && transport->control->acceptedId == pendingId) {
		if (transport->control->error || status == ONLINE_FAILED || pending == ONLINE_STOP)
			pending = 0;
		else if ((pending == ONLINE_HOST && status == ONLINE_HOSTING) || status == ONLINE_CONNECTED) {
			pending = 0;
			if (menu)
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
	field = -1;
	menu = 0;
	transport = 0;
	tab = ownsModal = pending = 0;
}
