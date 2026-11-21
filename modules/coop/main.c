#include <librac4/player.h>
#include "include/coop.h"
#include "world.h"
#include "objects.h"
#include "encounters.h"
#include "modules/online/include/online.h"
#include "modules/modmenu/include/modmenu.h"
#include <librac4/map.h>
#include <librac4/interop.h>
#include <librac4/pad.h>
#include <librac4/game.h>

#define WORD(address) (*(volatile u32*)(address))
#define INPUT_TIMEOUT (75000000)
#define MENU_BUTTONS (PAD_START | PAD_SELECT | PAD_L3 | PAD_R3)
#define HUD_DPAD (0x08)
#define PEER_READY (1)
#define PEER_SPAWNED (2)

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
	/* 0x28 */ u32 session;
	/* 0x2c */ u32 flags;
	/* 0x30 */ float spawn[3];
	/* 0x3c */ u32 spawnSession;
	/* 0x40 */ u32 objectBytes;
	/* 0x44 */ u8 objects[COOP_OBJECT_BYTES];
} coopInput_t;

static void rawPad(void* controller, u8* sample, int size, void* context);
static void drawView(int screen, int frustum, int first, int last);
static void applyView(int enabled);
static void setViewHooks(int enabled);
static void showPeer(u32 hero, int show);

volatile coopControl_t moduleExports DL_MODULE_EXPORTS;
static const moduleApi_t* loader;
static const onlineApi_t* online;
static const modMenuApi_t* menu;
static coopPad_t pads[2];
static u32 remoteTime;
static int active, allowInput, sourceValid, remoteValid, tab;
static int viewInstalled, viewWanted, viewHooks, viewPlayer, viewLevel;
static int localRole = ONLINE_HOST, connected, spawned, peerPresent;
static u32 session, peerSession, spawnSession, hiddenHero, hiddenMoby;
static u16 savedHidden;
static int savedCollision;
static float spawnPosition[3];
static u32 savedFrustums[20];
static u32 savedWideFrustums[2];
static u32 savedGuiLayout[6];
static u32 savedDpadPanels[2];
static u32 hudPanels[2];
static const u8 guiLayoutOffsets[] = {0x0c, 0x20, 0x24};

static VariableAddress_t vaDrawView = {
#if RAC4_PAL
	.Battledome = 0x52c048,
	.Catacrom = 0x4e2f48,
	.Sarathos = 0x4e4cc8,
	.Kronos = 0x508d88,
	.Shaar = 0x4dc688,
	.Valix = 0x4cd8c8,
	.Orxon = 0x4e5a48,
	.Torval = 0x4f0988,
	.Stygia = 0x4dd308,
	.Maraxus = 0x4f71c8,
	.GhostStation = 0x4ef9c8,
	.DreadZoneInterior = 0x4fff48
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x544d60,
	.Catacrom = 0x4fc020,
	.Sarathos = 0x4fdfa0,
	.Kronos = 0x521ea0,
	.Shaar = 0x4f5be0,
	.Valix = 0x4e6ca0,
	.Orxon = 0x4feb20,
	.Torval = 0x509aa0,
	.Stygia = 0x4f66e0,
	.Maraxus = 0x510220,
	.GhostStation = 0x508a60,
	.DreadZoneInterior = 0x5190e0
#else
	.Battledome = 0x529b88,
	.Catacrom = 0x4e1048,
	.Sarathos = 0x4e2f08,
	.Kronos = 0x506dc8,
	.Shaar = 0x4dabc8,
	.Valix = 0x4cc088,
	.Orxon = 0x4e3b08,
	.Torval = 0x4eeac8,
	.Stygia = 0x4db6c8,
	.Maraxus = 0x4f5208,
	.GhostStation = 0x4eda48,
	.DreadZoneInterior = 0x4fdf48
#endif
};

static VariableAddress_t vaViewCalls = {
#if RAC4_PAL
	.Battledome = 0x52c590,
	.Catacrom = 0x4e3490,
	.Sarathos = 0x4e5210,
	.Kronos = 0x5092d0,
	.Shaar = 0x4dcbd0,
	.Valix = 0x4cde10,
	.Orxon = 0x4e5f90,
	.Torval = 0x4f0ed0,
	.Stygia = 0x4dd850,
	.Maraxus = 0x4f7710,
	.GhostStation = 0x4eff10,
	.DreadZoneInterior = 0x500490
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5452a8,
	.Catacrom = 0x4fc568,
	.Sarathos = 0x4fe4e8,
	.Kronos = 0x5223e8,
	.Shaar = 0x4f6128,
	.Valix = 0x4e71e8,
	.Orxon = 0x4ff068,
	.Torval = 0x509fe8,
	.Stygia = 0x4f6c28,
	.Maraxus = 0x510768,
	.GhostStation = 0x508fa8,
	.DreadZoneInterior = 0x519628
#else
	.Battledome = 0x52a0d0,
	.Catacrom = 0x4e1590,
	.Sarathos = 0x4e3450,
	.Kronos = 0x507310,
	.Shaar = 0x4db110,
	.Valix = 0x4cc5d0,
	.Orxon = 0x4e4050,
	.Torval = 0x4ef010,
	.Stygia = 0x4dbc10,
	.Maraxus = 0x4f5750,
	.GhostStation = 0x4edf90,
	.DreadZoneInterior = 0x4fe490
#endif
};

static VariableAddress_t vaPausedViewCalls = {
#if RAC4_PAL
	.Battledome = 0x54c3e4,
	.Catacrom = 0x4fec24,
	.Sarathos = 0x4fee34,
	.Kronos = 0x522f3c,
	.Shaar = 0x4f67f4,
	.Valix = 0x4e7a34,
	.Orxon = 0x4ffbb4,
	.Torval = 0x50aaf4,
	.Stygia = 0x4f7474,
	.Maraxus = 0x511384,
	.GhostStation = 0x509b7c,
	.DreadZoneInterior = 0x51bd3c
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x564b5c,
	.Catacrom = 0x5177a4,
	.Sarathos = 0x517be4,
	.Kronos = 0x53bb2c,
	.Shaar = 0x50f824,
	.Valix = 0x5008e4,
	.Orxon = 0x518764,
	.Torval = 0x5236e4,
	.Stygia = 0x510324,
	.Maraxus = 0x529eb4,
	.GhostStation = 0x5226ec,
	.DreadZoneInterior = 0x534944
#else
	.Battledome = 0x549aac,
	.Catacrom = 0x4fc8f4,
	.Sarathos = 0x4fcc74,
	.Kronos = 0x520b7c,
	.Shaar = 0x4f4934,
	.Valix = 0x4e5df4,
	.Orxon = 0x4fd874,
	.Torval = 0x508834,
	.Stygia = 0x4f5434,
	.Maraxus = 0x50efc4,
	.GhostStation = 0x5077fc,
	.DreadZoneInterior = 0x5198d4
#endif
};

static VariableAddress_t vaFrustums = {
#if RAC4_PAL
	.Battledome = 0x3b65f0,
	.Catacrom = 0x3af1c0,
	.Sarathos = 0x3ae630,
	.Kronos = 0x3b0770,
	.Shaar = 0x3adff0,
	.Valix = 0x3ae530,
	.Orxon = 0x3ae330,
	.Torval = 0x3af7b0,
	.Stygia = 0x3ad870,
	.Maraxus = 0x3af470,
	.GhostStation = 0x3afbf0,
	.DreadZoneInterior = 0x3b3ed0
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x3d0d30,
	.Catacrom = 0x3c9880,
	.Sarathos = 0x3c8d70,
	.Kronos = 0x3caeb0,
	.Shaar = 0x3c8730,
	.Valix = 0x3c8c70,
	.Orxon = 0x3c8a70,
	.Torval = 0x3c9eb0,
	.Stygia = 0x3c7fb0,
	.Maraxus = 0x3c9bb0,
	.GhostStation = 0x3ca330,
	.DreadZoneInterior = 0x3ce610
#else
	.Battledome = 0x3b65b0,
	.Catacrom = 0x3af180,
	.Sarathos = 0x3ae5f0,
	.Kronos = 0x3b0730,
	.Shaar = 0x3ae030,
	.Valix = 0x3ae5f0,
	.Orxon = 0x3ae370,
	.Torval = 0x3af7f0,
	.Stygia = 0x3ad8b0,
	.Maraxus = 0x3af4b0,
	.GhostStation = 0x3afc30,
	.DreadZoneInterior = 0x3b3e90
#endif
};

static VariableAddress_t vaWideFrustums = {
#if RAC4_PAL
	.Battledome = 0x3b67d0,
	.Catacrom = 0x3af3a0,
	.Sarathos = 0x3ae810,
	.Kronos = 0x3b0950,
	.Shaar = 0x3ae1d0,
	.Valix = 0x3ae710,
	.Orxon = 0x3ae510,
	.Torval = 0x3af990,
	.Stygia = 0x3ada50,
	.Maraxus = 0x3af650,
	.GhostStation = 0x3afdd0,
	.DreadZoneInterior = 0x3b40b0
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x3d0f10,
	.Catacrom = 0x3c9a60,
	.Sarathos = 0x3c8f50,
	.Kronos = 0x3cb090,
	.Shaar = 0x3c8910,
	.Valix = 0x3c8e50,
	.Orxon = 0x3c8c50,
	.Torval = 0x3ca090,
	.Stygia = 0x3c8190,
	.Maraxus = 0x3c9d90,
	.GhostStation = 0x3ca510,
	.DreadZoneInterior = 0x3ce7f0
#else
	.Battledome = 0x3b6790,
	.Catacrom = 0x3af360,
	.Sarathos = 0x3ae7d0,
	.Kronos = 0x3b0910,
	.Shaar = 0x3ae210,
	.Valix = 0x3ae7d0,
	.Orxon = 0x3ae550,
	.Torval = 0x3af9d0,
	.Stygia = 0x3ada90,
	.Maraxus = 0x3af690,
	.GhostStation = 0x3afe10,
	.DreadZoneInterior = 0x3b4070
#endif
};

static VariableAddress_t vaGuiPlayers = {
#if RAC4_PAL
	.Battledome = 0x31b3f8,
	.Catacrom = 0x317f78,
	.Sarathos = 0x317ef8,
	.Kronos = 0x318338,
	.Shaar = 0x317db8,
	.Valix = 0x317d38,
	.Orxon = 0x317db8,
	.Torval = 0x3180b8,
	.Stygia = 0x317e38,
	.Maraxus = 0x3180b8,
	.GhostStation = 0x317f78,
	.DreadZoneInterior = 0x317f38
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x335af8,
	.Catacrom = 0x3325f8,
	.Sarathos = 0x3325f8,
	.Kronos = 0x332a38,
	.Shaar = 0x3324b8,
	.Valix = 0x332438,
	.Orxon = 0x3324f8,
	.Torval = 0x3327b8,
	.Stygia = 0x332538,
	.Maraxus = 0x3327b8,
	.GhostStation = 0x332678,
	.DreadZoneInterior = 0x332638
#else
	.Battledome = 0x31b2f8,
	.Catacrom = 0x317e78,
	.Sarathos = 0x317df8,
	.Kronos = 0x318238,
	.Shaar = 0x317d38,
	.Valix = 0x317cb8,
	.Orxon = 0x317d38,
	.Torval = 0x318038,
	.Stygia = 0x317db8,
	.Maraxus = 0x318038,
	.GhostStation = 0x317ef8,
	.DreadZoneInterior = 0x317e38
#endif
};

static VariableAddress_t vaGuiScreenMode = {
#if RAC4_PAL
	.Battledome = 0x5dcb98,
	.Catacrom = 0x58ce10,
	.Sarathos = 0x58c9b8,
	.Kronos = 0x5b2b48,
	.Shaar = 0x584b98,
	.Valix = 0x577b70,
	.Orxon = 0x58f578,
	.Torval = 0x599eb0,
	.Stygia = 0x586c50,
	.Maraxus = 0x5a04f8,
	.GhostStation = 0x59a560,
	.DreadZoneInterior = 0x5aada8
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5f3240,
	.Catacrom = 0x5a3938,
	.Sarathos = 0x5a3720,
	.Kronos = 0x5c96b0,
	.Shaar = 0x59bb80,
	.Valix = 0x58e9d8,
	.Orxon = 0x5a60e0,
	.Torval = 0x5b0a58,
	.Stygia = 0x59dab8,
	.Maraxus = 0x5b6fe0,
	.GhostStation = 0x5b1088,
	.DreadZoneInterior = 0x5c1950
#else
	.Battledome = 0x5d80e8,
	.Catacrom = 0x5889a0,
	.Sarathos = 0x5886c8,
	.Kronos = 0x5ae658,
	.Shaar = 0x580ba8,
	.Valix = 0x573e40,
	.Orxon = 0x58b148,
	.Torval = 0x595ac0,
	.Stygia = 0x582ae0,
	.Maraxus = 0x59c008,
	.GhostStation = 0x5960f0,
	.DreadZoneInterior = 0x5a67f8
#endif
};

static VariableAddress_t vaGuiCanvasOffsets = {
#if RAC4_PAL
	.Battledome = 0x5dcc20,
	.Catacrom = 0x58ce98,
	.Sarathos = 0x58ca40,
	.Kronos = 0x5b2bd0,
	.Shaar = 0x584c20,
	.Valix = 0x577bf8,
	.Orxon = 0x58f600,
	.Torval = 0x599f38,
	.Stygia = 0x586cd8,
	.Maraxus = 0x5a0580,
	.GhostStation = 0x59a5e8,
	.DreadZoneInterior = 0x5aae30
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x5f32c8,
	.Catacrom = 0x5a39c0,
	.Sarathos = 0x5a37a8,
	.Kronos = 0x5c9738,
	.Shaar = 0x59bc08,
	.Valix = 0x58ea60,
	.Orxon = 0x5a6168,
	.Torval = 0x5b0ae0,
	.Stygia = 0x59db40,
	.Maraxus = 0x5b7068,
	.GhostStation = 0x5b1110,
	.DreadZoneInterior = 0x5c19d8
#else
	.Battledome = 0x5d8170,
	.Catacrom = 0x588a28,
	.Sarathos = 0x588750,
	.Kronos = 0x5ae6e0,
	.Shaar = 0x580c30,
	.Valix = 0x573ec8,
	.Orxon = 0x58b1d0,
	.Torval = 0x595b48,
	.Stygia = 0x582b68,
	.Maraxus = 0x59c090,
	.GhostStation = 0x596178,
	.DreadZoneInterior = 0x5a6880
#endif
};

static VariableAddress_t vaHudPlayers = {
#if RAC4_PAL
	.Battledome = 0x32ee60,
	.Catacrom = 0x32b9e0,
	.Sarathos = 0x32b960,
	.Kronos = 0x32bda0,
	.Shaar = 0x32b820,
	.Valix = 0x32b7a0,
	.Orxon = 0x32b820,
	.Torval = 0x32bb20,
	.Stygia = 0x32b8a0,
	.Maraxus = 0x32bb20,
	.GhostStation = 0x32b9e0,
	.DreadZoneInterior = 0x32b9a0
#elif RAC4_NTSCJ || RAC4_NTSCK
	.Battledome = 0x349560,
	.Catacrom = 0x346060,
	.Sarathos = 0x346060,
	.Kronos = 0x3464a0,
	.Shaar = 0x345f20,
	.Valix = 0x345ea0,
	.Orxon = 0x345f60,
	.Torval = 0x346220,
	.Stygia = 0x345fa0,
	.Maraxus = 0x346220,
	.GhostStation = 0x3460e0,
	.DreadZoneInterior = 0x3460a0
#else
	.Battledome = 0x32ed60,
	.Catacrom = 0x32b8e0,
	.Sarathos = 0x32b860,
	.Kronos = 0x32bca0,
	.Shaar = 0x32b7a0,
	.Valix = 0x32b720,
	.Orxon = 0x32b7a0,
	.Torval = 0x32baa0,
	.Stygia = 0x32b820,
	.Maraxus = 0x32baa0,
	.GhostStation = 0x32b960,
	.DreadZoneInterior = 0x32b8a0
#endif
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
	if ((Level != 21 && Level != 22) || !data) {
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
	u32 address = GetAddressImmediate(&vaCoopGameMode);
	return (Level == 21 || Level == 22) && address && WORD(address) == GAME_MODE_NORMAL && !(menu && menu->isOpen());
}

/*
 * NAME : drawView
 * DESCRIPTION : Draw the assigned player's native camera and HUD full screen.
 * NOTES : Draw before the native inter-pass helper, even when viewing player 2.
 * ARGS : Native screen, frustum and first/last-pass flags.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void drawView(int screen, int frustum, int first, int last)
{
	if (first)
		coopWorldApplyPlayers();
	if (first && viewInstalled != viewWanted)
		applyView(viewWanted);
	if (!viewInstalled)
		((void (*)(int, int, int, int))GetAddressImmediate(&vaDrawView))(screen, frustum, first, last);
	else if (first)
		((void (*)(int, int, int, int))GetAddressImmediate(&vaDrawView))(viewPlayer, 0, 1, 1);
	if (last && !viewWanted && !viewInstalled)
		setViewHooks(0);
}

/*
 * NAME : setViewHooks
 * DESCRIPTION : Install or release the normal and paused view dispatchers.
 * NOTES : Dispatchers preserve native arguments until a frame boundary.
 * ARGS : Nonzero to install.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void setViewHooks(int enabled)
{
	u32 normal = GetAddressImmediate(&vaViewCalls);
	u32 paused = GetAddressImmediate(&vaPausedViewCalls);
	u32 mode = GetAddressImmediate(&vaGuiScreenMode);
	u32 offsets = GetAddressImmediate(&vaGuiCanvasOffsets);
	u32 calls[7] = {normal, normal + 0x1c, paused, paused + 0x14, mode, offsets, offsets + 0x0c};
	u32 native = 0x0c000000 | (GetAddressImmediate(&vaDrawView) >> 2);
	u32 replacement = 0x0c000000 | ((u32)drawView >> 2);
	u32 originals[7] = {native, native, native, native, 0x8c62000c, 0xc4800020, 0xc4610024};
	u32 replacements[7] = {replacement, replacement, replacement, replacement, 0x0000102d, 0x44800000, 0x44800800};
	int i;
	if (!normal || !paused || !mode || !offsets || enabled == viewHooks)
		return;
	if (enabled) {
		for (i = 0; i < 7; ++i)
			if (WORD(calls[i]) != originals[i])
				return;
	}
	/* Native GUI initialization can reset its player fields after activation.
	 * Keep the screen-mode and canvas getters full-screen until we release
	 * the view; invalid-player handling and panel ownership remain native. */
	for (i = 0; i < 7; ++i) {
		if (enabled)
			WORD(calls[i]) = replacements[i];
		else if (WORD(calls[i]) == replacements[i])
			WORD(calls[i]) = originals[i];
	}
	viewHooks = enabled;
	loader->flushCache(0);
	loader->flushCache(2);
}

/*
 * NAME : applyView
 * DESCRIPTION : Change the viewport and HUD layout before the first draw pass.
 * NOTES : Never change layout from an intermediate VSync inside a frame.
 * ARGS : Nonzero for the assigned full-screen view.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void applyView(int enabled)
{
	u32 table = GetAddressImmediate(&vaFrustums);
	u32 wide = GetAddressImmediate(&vaWideFrustums);
	u32 gui = GetAddressImmediate(&vaGuiPlayers);
	u32 hud = GetAddressImmediate(&vaHudPlayers);
	int i;
	if (!table || enabled == viewInstalled)
		return;
	if (enabled) {
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
			savedDpadPanels[i] = Level == 21 ? WORD(hud + i * 0x10 + 4) & HUD_DPAD : 0;
			if (Level == 21)
				WORD(hud + i * 0x10 + 4) &= ~HUD_DPAD;
		}
	} else {
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
}

/*
 * NAME : updateView
 * DESCRIPTION : Request a view change without interrupting an active draw pass.
 * NOTES : Keep the assigned player stable until restoration completes.
 * ARGS : Nonzero to enable the co-op view.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void updateView(int enabled)
{
	if (viewLevel != Level) {
		viewInstalled = viewHooks = 0;
		viewLevel = Level;
	}
	viewWanted = enabled;
	if ((Level != 21 && Level != 22)) {
		viewInstalled = viewHooks = viewWanted = 0;
		return;
	}
	if (enabled) {
		viewPlayer = localRole == ONLINE_JOIN ? 1 : 0;
		setViewHooks(1);
	}
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
	if (!active || loader->context != DL_CONTEXT_CAMPAIGN || (Level != 21 && Level != 22) || playerGetNumLocals() != 2)
		return 0;
	table = GetAddressImmediate(&vaCoopPlayers);
	lookup = GetAddressImmediate(&vaCoopPlayerLookup);
	if (!table || !lookup || WORD(lookup) != 0x240331f0 || (WORD(lookup + 4) & 0xffff0000) != 0x3c020000 ||
	    (WORD(lookup + 0x0c) & 0xffff0000) != 0x24420000)
		return 0;
	if (WORD(table) != ((WORD(lookup + 4) & 0xffff) << 16) + (s16)WORD(lookup + 0x0c))
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
		pads[i].pad->RawPadInputCallback = (Level == 21 || Level == 22) ? pads[i].original : 0;
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
		role = localRole;
		usable = active && allowInput && moduleExports.enabled && (Level == 21 || Level == 22);
		if (pads[i].pad->RawPadInputCallback == rawPad && !usable) {
			pads[i].pad->RawPadInputCallback = (Level == 21 || Level == 22) ? original : 0;
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
				sourceValid = 1;
			} else if (i == 0) {
				sourceValid = 0;
			}
			neutral(idle);
			if (!gameplay)
				replacement = i == 1 ? idle : 0;
			else if (role == ONLINE_HOST && i == 1)
				replacement = peerPresent && remoteValid && ticks() - remoteTime < INPUT_TIMEOUT ? moduleExports.remoteSample : idle;
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
		if (original && (Level == 21 || Level == 22))
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
 * DESCRIPTION : Show the co-op adapter status.
 * NOTES : This toggle is not persisted.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void registerMenu(void)
{
	modMenuItem_t status = {"Status", MODMENU_RANGE, (int*)&moduleExports.state, COOP_OFF, COOP_ACTIVE, 1, 0, 0};
	modMenuItem_t help = {"Host: player 1 / Guest: player 2", MODMENU_LABEL, 0, 0, 0, 0, 0, 0};
	modMenuItem_t scope = {"Station / Catacrom test", MODMENU_LABEL, 0, 0, 0, 0, 0, 0};
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
	coopEncountersReset(loader);
	coopObjectsReset();
	if (hiddenHero && (Level == 21 || Level == 22))
		showPeer(hiddenHero, 1);
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
 * DESCRIPTION : Initialize the enabled Station co-op adapter.
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
	moduleExports.enabled = 1;
	api->setLifecycle(activate, deactivate);
	return 0;
}

/*
 * NAME : stableState
 * DESCRIPTION : Permit joins only during normal gameplay or the native pause menu.
 * NOTES : Map, mission and hero validation is performed before this check.
 * ARGS :
 * RETURN : Nonzero when a spawn can be applied.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int stableState(void)
{
	u32 address = GetAddressImmediate(&vaCoopGameMode);
	return address && (WORD(address) == GAME_MODE_NORMAL || WORD(address) == 3);
}

/*
 * NAME : showPeer
 * DESCRIPTION : Hide the absent peer without releasing native co-op player storage.
 * NOTES : Preserve unrelated moby flags and restore only the hidden bit we own.
 * ARGS : Remote hero and whether the peer is present.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void showPeer(u32 hero, int show)
{
	u32 moby = WORD(hero + 0x2ef0);
	if (moby < 0x100000 || moby >= 0x2000000)
		return;
	if (hiddenHero && (hiddenHero != hero || hiddenMoby != moby)) {
		hiddenHero = hiddenMoby = 0;
	}
	if (!show) {
		if (!hiddenHero) {
			hiddenHero = hero;
			hiddenMoby = moby;
			savedHidden = *(u16*)(moby + 0x34) & 0x80;
			savedCollision = WORD(moby + 0x9c);
		}
		*(u16*)(moby + 0x34) |= 0x80;
		WORD(moby + 0x9c) = 0;
	} else if (hiddenHero) {
		*(u16*)(moby + 0x34) = (*(u16*)(moby + 0x34) & ~0x80) | savedHidden;
		WORD(moby + 0x9c) = savedCollision;
		hiddenHero = hiddenMoby = 0;
	}
}

/*
 * NAME : safeSpawn
 * DESCRIPTION : Validate ground using the native co-op respawn checks.
 * NOTES : Check a small footprint too, so a supported center cannot hide a ledge.
 * ARGS : Candidate position, updated by native spawn-checker objects.
 * RETURN : Nonzero when the position and its surrounding ground pass.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int safeSpawn(float* position)
{
	static const float offsets[4][2] = {{0.75f, 0}, {-0.75f, 0}, {0, 0.75f}, {0, -0.75f}};
	float point[4] __attribute__((aligned(16)));
	u32 address = GetAddressImmediate(&vaCoopPlayerCheckSpawn);
	int i, j;
	if (!address || WORD(address) != 0x27bdff70)
		return 0;
	for (j = 0; j < 3; ++j) {
		if (!(position[j] >= 2 && position[j] <= 1021))
			return 0;
		point[j] = position[j];
	}
	point[3] = 0;
	/* MF_CheckSpawnPoint rejects dangerous surfaces, distant ground and
	 * mission-specific hazards. Its native fallback is not assumed safe. */
	if (!((int (*)(float*))address)(point))
		return 0;
	for (j = 0; j < 3; ++j) {
		if (!(point[j] >= 2 && point[j] <= 1021))
			return 0;
		position[j] = point[j];
	}
	for (i = 0; i < 4; ++i) {
		for (j = 0; j < 3; ++j)
			point[j] = position[j];
		point[0] += offsets[i][0];
		point[1] += offsets[i][1];
		if (!((int (*)(float*))address)(point))
			return 0;
		/* A checker moving an edge probe elsewhere does not prove support. */
		if (point[0] != position[0] + offsets[i][0] || point[1] != position[1] + offsets[i][1] || point[2] != position[2])
			return 0;
	}
	return 1;
}

/*
 * NAME : selectSpawn
 * DESCRIPTION : Obtain a checked native co-op respawn point near the host.
 * NOTES : Wait instead of using the native unchecked/initial-position fallback.
 * ARGS : Joining hero, host hero and output position.
 * RETURN : Nonzero when a safe nearby spawn is available.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int selectSpawn(u32 hero, u32 host, float* position)
{
	float point[4] __attribute__((aligned(16)));
	float delta, distance = 0;
	u32 spawn, address = GetAddressImmediate(&vaCoopPlayerBestSpawn);
	int i;
	if (!stableState() || !address || WORD(address) != 0x27bdffd0)
		return 0;
	spawn = ((u32 (*)(u32))address)(hero);
	if (spawn < 0x100000 || spawn > 0x1ffffe0 || (spawn & 15))
		return 0;
	for (i = 0; i < 3; ++i)
		point[i] = ((float*)spawn)[i];
	point[3] = 0;
	if (!safeSpawn(point))
		return 0;
	for (i = 0; i < 3; ++i) {
		delta = point[i] - ((float*)(host + 0xa0))[i];
		distance += delta * delta;
	}
	if (!(distance <= 256))
		return 0;
	for (i = 0; i < 3; ++i)
		position[i] = point[i];
	return 1;
}

/*
 * NAME : spawnPeer
 * DESCRIPTION : Place a joining player at the validated native co-op spawn.
 * NOTES : Native Warp resets movement, collision history and the camera.
 * ARGS : Hero and host-assigned position.
 * RETURN : Nonzero when the warp was applied.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int spawnPeer(u32 hero, const float* position)
{
	float point[4] __attribute__((aligned(16)));
	float rotation[4] __attribute__((aligned(16)));
	u32 address = GetAddressImmediate(&vaCoopPlayerWarp);
	int i;
	if (!address || WORD(address) != 0x27bdff60 || WORD(hero + 0x2ef4) || !stableState())
		return 0;
	for (i = 0; i < 3; ++i) {
		if (!(position[i] >= 2 && position[i] <= 1021))
			return 0;
		point[i] = position[i];
		rotation[i] = ((float*)(hero + 0xb0))[i];
	}
	point[3] = rotation[3] = 0;
	if (!safeSpawn(point))
		return 0;
	((void (*)(u32, float*, float*, int, int, int, int, int))address)(hero, point, rotation, 0, 1, 1, 1, 1);
	return 1;
}

/*
 * NAME : moduleUpdate
 * DESCRIPTION : Exchange current pad snapshots through the generic Online API.
 * NOTES : Peers supply input/position; the host supplies world state and health.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void moduleUpdate(void)
{
	coopInput_t frame;
	struct PAD* pad;
	u32 original;
	int i, j, size, local, role, ready, worldSent;
	u32 heroes, localHero, remoteHero;
	refreshHud(0);
	registerMenu();
	online = onlineFind(loader);
	worldSent = coopEncountersUpdate(loader, online, moduleExports.enabled);
	worldSent |= coopWorldUpdate(loader, online, moduleExports.enabled);
	coopObjectsUpdate(loader, online, moduleExports.enabled, coopWorldReady(), 1);
	if (!moduleExports.enabled || !supported()) {
		moduleExports.state = !moduleExports.enabled ? COOP_OFF : COOP_UNSUPPORTED;
		connected = spawned = peerPresent = 0;
		hiddenHero = hiddenMoby = 0;
		releasePads();
		return;
	}
	if (menu && menu->isOpen()) {
		allowInput = 0;
		sourceValid = 0;
	} else {
		original = GetAddressImmediate(&vaCoopPadRead);
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
	role = online ? online->role() : 0;
	if (role && (!connected || role != localRole)) {
		if (hiddenHero)
			showPeer(hiddenHero, 1);
		localRole = role;
		session = ticks() | 1;
		peerSession = spawnSession = 0;
		spawned = peerPresent = remoteValid = 0;
	}
	connected = role != 0;
	moduleExports.state = connected ? COOP_ACTIVE : COOP_WAITING;
	updateView(1);
	if (!gameplayInput())
		sourceValid = 0;
	local = localRole == ONLINE_JOIN ? 1 : 0;
	heroes = GetAddressImmediate(&vaCoopPlayers);
	localHero = WORD(heroes + local * 4);
	remoteHero = WORD(heroes + (1 - local) * 4);
	ready = stableState() && !WORD(localHero + 0x2ef4);
	size = connected ? online->receive(COOP_INPUT_CHANNEL, &frame, sizeof(frame)) : 0;
	if (size >= 0x44 && frame.objectBytes <= sizeof(frame.objects) && size == (int)(0x44 + frame.objectBytes) && frame.level == (u32)Level && frame.mission == Mission->ActiveMission && frame.sample[0] == 0 && frame.session) {
		coopObjectsReceive(frame.objects, frame.objectBytes);
		if (peerSession != frame.session) {
			peerSession = frame.session;
			spawnSession = spawned = peerPresent = 0;
		}
		if (localRole == ONLINE_HOST && ready && (frame.flags & PEER_READY)) {
			if (spawnSession != peerSession) {
				if (selectSpawn(remoteHero, localHero, spawnPosition) && spawnPeer(remoteHero, spawnPosition))
					spawnSession = peerSession;
			}
			peerPresent = spawnSession == peerSession && (frame.flags & PEER_SPAWNED) && frame.spawnSession == peerSession;
		} else if (localRole == ONLINE_JOIN && ready && (frame.flags & PEER_READY)) {
			if (!spawned && frame.spawnSession == session && spawnPeer(localHero, frame.spawn))
				spawned = 1;
			peerPresent = spawned;
		}
		for (j = 0; j < COOP_SAMPLE_BYTES; ++j)
			moduleExports.remoteSample[j] = frame.sample[j];
		remoteTime = ticks();
		remoteValid = 1;
		++moduleExports.received;
		if (peerPresent && allowInput && gameplayInput() && *(float*)(remoteHero + 0x2e20) > 0)
			correctPosition(&frame, remoteHero);
	}
	moduleExports.stale = !remoteValid || ticks() - remoteTime >= INPUT_TIMEOUT;
	if (!connected || moduleExports.stale)
		peerPresent = 0;
	showPeer(remoteHero, peerPresent);
	if (!connected)
		return;
	frame.session = session;
	frame.flags = (ready ? PEER_READY : 0) | (spawned ? PEER_SPAWNED : 0);
	frame.spawnSession = localRole == ONLINE_HOST ? spawnSession : spawned ? session : 0;
	for (j = 0; j < 3; ++j)
		frame.spawn[j] = spawnPosition[j];
	frame.level = Level;
	frame.mission = Mission->ActiveMission;
	for (j = 0; j < 3; ++j)
		frame.position[j] = ((float*)(WORD(GetAddressImmediate(&vaCoopPlayers) + local * 4) + 0xa0))[j];
	if (!sourceValid) {
		neutral(frame.sample);
	} else {
		for (j = 0; j < COOP_SAMPLE_BYTES; ++j)
			frame.sample[j] = moduleExports.sourceSample[j];
	}
	frame.objectBytes = coopObjectsWrite(frame.objects);
	if (!worldSent && online->send(COOP_INPUT_CHANNEL, &frame, 0x44 + frame.objectBytes) == 0) {
		++moduleExports.sent;
	}
}
