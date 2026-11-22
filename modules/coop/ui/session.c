#include "session.h"
#include "session-addresses.h"
#include <librac4/game.h>

#define WORD(address) (*(volatile u32*)(address))
#define NATIVE(address, type) ((type)GetAddressImmediate(&(address)))

static int watching, watchingLevel;

/*
 * NAME : planetAddress
 * DESCRIPTION : Resolve the native mission menu's selected planet global.
 * NOTES : The verified instruction uses the game's preserved small-data register.
 * ARGS :
 * RETURN : Address of the selected planet, or zero for an unknown overlay.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 planetAddress(void)
{
	u32 address = GetAddressImmediate(&vaSessionUpdateMission), gp;
	if (!address || (WORD(address + 0x78) & 0xffff0000) != 0x8f840000)
		return 0;
	__asm__ volatile("move %0, $gp" : "=r"(gp));
	return gp + (s16)WORD(address + 0x78);
}

/*
 * NAME : menuData
 * DESCRIPTION : Resolve one supported native mission or planet list.
 * NOTES : Offsets match the retail menu structs and CtlList layout.
 * ARGS : Native screen ID, two for missions or three for planets.
 * RETURN : Player-zero menu data, or zero for another screen.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 menuData(int screen)
{
	return screen == 2 ? GetAddressImmediate(&vaSessionMissionData) :
	    screen == 3 ? GetAddressImmediate(&vaSessionPlanetData) : 0;
}

/*
 * NAME : coopSessionScreen
 * DESCRIPTION : Read the visible native menu screen in a verified co-op overlay.
 * NOTES : Returns no screen while gameplay or another GUI state owns the canvas.
 * ARGS :
 * RETURN : Native screen ID, or minus one when no menu is visible.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopSessionScreen(void)
{
	u32 menus;
	if ((Level != 21 && Level != 22) ||
	    WORD(GetAddressImmediate(&vaSessionGetState)) != 0x24030054 ||
	    WORD(GetAddressImmediate(&vaSessionGetScreen)) != 0x2402003c)
		return -1;
	menus = GetAddressImmediate(&vaSessionMenuData);
	if (!menus || !*(u8*)menus || NATIVE(vaSessionGetState, int (*)(int))(0) != 2)
		return -1;
	return NATIVE(vaSessionGetScreen, int (*)(int))(0);
}

/*
 * NAME : coopSessionRead
 * DESCRIPTION : Read only a visible mission or planet selection screen.
 * NOTES : Pause/options and vendor screens are deliberately excluded.
 * ARGS : Optional snapshot destination.
 * RETURN : Nonzero for a supported selection screen.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopSessionRead(coopSessionMenu_t* frame)
{
	u32 data, list, planet;
	int screen = coopSessionScreen();
	if (screen < 0)
		return 0;
	data = menuData(screen);
	planet = planetAddress();
	if (!data || !planet || WORD(data) != 0)
		return 0;
	list = data + (screen == 2 ? 0x4c8 : 0x1ec);
	if (!WORD(list + 0x50) || WORD(list + 0x50) > (u32)(screen == 2 ? 50 : 19) ||
	    WORD(list + 0x54) >= WORD(list + 0x50) || WORD(planet) >= 19)
		return 0;
	if (frame) {
		frame->screen = screen;
		frame->focus = WORD(list + 0x54);
		frame->planet = WORD(planet);
	}
	return 1;
}

/*
 * NAME : coopSessionApply
 * DESCRIPTION : Mirror host selection without activating any mission command.
 * NOTES : Native screen transitions build the widgets; local guest input is blocked.
 * ARGS : Validated host selection, or a zero screen after the host leaves it.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopSessionApply(const coopSessionMenu_t* frame)
{
	u32 data, list, planet, menus;
	int state, screen;
	if ((Level != 21 && Level != 22) ||
	    WORD(GetAddressImmediate(&vaSessionGetState)) != 0x24030054 ||
	    WORD(GetAddressImmediate(&vaSessionGetScreen)) != 0x2402003c) {
		coopSessionReset();
		return;
	}
	if (watchingLevel != Level)
		watching = 0;
	watchingLevel = Level;
	if (!frame || (frame->screen != 2 && frame->screen != 3)) {
		if (watching && coopSessionRead(0))
			NATIVE(vaSessionCloseMenu, void (*)(int, int))(0, -1);
		watching = 0;
		return;
	}
	if ((u32)frame->planet >= 19 || (u32)frame->focus >= (u32)(frame->screen == 2 ? 50 : 19))
		return;
	state = NATIVE(vaSessionGetState, int (*)(int))(0);
	/* Wait for an independently opened vendor or other non-menu state. */
	if (state != 0 && state != 2)
		return;
	watching = 1;
	if (state != 2) {
		NATIVE(vaSessionPushState, void (*)(int, int))(0, 2);
		return;
	}
	menus = GetAddressImmediate(&vaSessionMenuData);
	if (!menus || !*(u8*)menus)
		return;
	screen = NATIVE(vaSessionGetScreen, int (*)(int))(0);
	planet = planetAddress();
	if (!planet)
		return;
	if (WORD(planet) != (u32)frame->planet) {
		WORD(planet) = frame->planet;
		data = menuData(frame->screen);
		WORD(data + 4) |= 2;
	}
	if (screen != frame->screen) {
		NATIVE(vaSessionGoScreen, void (*)(int, int))(0, frame->screen);
		return;
	}
	data = menuData(screen);
	if (WORD(data) != 0)
		return;
	list = data + (screen == 2 ? 0x4c8 : 0x1ec);
	if ((u32)frame->focus < WORD(list + 0x50) && WORD(list + 0x54) != (u32)frame->focus) {
		NATIVE(vaSessionSetFocus, void (*)(u32, int))(list, frame->focus);
		WORD(data + 4) |= 5;
	}
}

/*
 * NAME : coopSessionWatching
 * DESCRIPTION : Report whether the host owns mission-selection input.
 * NOTES : Ordinary pause and vendor input remains local.
 * ARGS :
 * RETURN : Nonzero only in the current watched level.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopSessionWatching(void)
{
	return watching && watchingLevel == Level;
}

/*
 * NAME : coopSessionReset
 * DESCRIPTION : Release selection ownership after disconnect or deactivation.
 * NOTES : No native screen is closed during an overlay transition.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopSessionReset(void)
{
	watching = 0;
	watchingLevel = 0;
}
