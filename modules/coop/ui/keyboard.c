#include "keyboard.h"
#include "sound.h"
#include <librac4/graphics.h>
#include <librac4/pad.h>

#define TEXT_COLOR (0x80ffffff)
#define ACCENT_COLOR (0x8024a9de)
#define INPUT_SCALE (1.0f)
#define CURSOR_BLINK_TICKS (73728000)
#define NAVIGATION_DELAY (110592000)
#define NAVIGATION_INTERVAL (14745600)
#define NAVIGATION_BUTTONS (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)
#define KEY_SIZE (32.0f)
#define KEY_GAP (4.0f)
#define PANEL_PADDING (12.0f)
#define INPUT_HEIGHT (32.0f)
#define ACTION_HEIGHT (30.0f)
#define GRID_WIDTH (KEY_SIZE * 11 + KEY_GAP * 10)
#define GRID_HEIGHT (KEY_SIZE * 4 + KEY_GAP * 3)
#define GRID_TOP (PANEL_PADDING + INPUT_HEIGHT + 18)
#define PANEL_WIDTH (GRID_WIDTH + PANEL_PADDING * 2)
#define PANEL_HEIGHT (GRID_TOP + GRID_HEIGHT + 8 + ACTION_HEIGHT + PANEL_PADDING)
#define PANEL_X (panelX)
#define PANEL_Y (panelY)
#define GRID_X (PANEL_X + PANEL_PADDING)
#define GRID_Y (PANEL_Y + GRID_TOP)

static char input[COOP_KEYBOARD_TEXT_BYTES];
static int length, cursor, selected, column, textStart, limit, uppercase, symbols;
static u32 cursorTime, moveTime, repeatDirection, repeatTime, repeatDelay, stickDirection;
static int cursorVisible;
static float panelX, panelY;
static const char letters[] = "1234567890@qwertyuiop#asdfghjkl'/zxcvbnm-_,!";
static const char punctuation[] = "1234567890@!#$%&*()+=[]{}<>:;'/\\|~^_-,.?\"`=+";

/*
 * NAME : cursorTicks
 * DESCRIPTION : Read a clock that continues while the native menu is paused.
 * NOTES : EE Count runs at 147456000 ticks per second.
 * ARGS :
 * RETURN : Wrapping counter value.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 cursorTicks(void)
{
	u32 value;
	__asm__ volatile("mfc0 %0, $9" : "=r"(value));
	return value;
}

/*
 * NAME : resetCursorBlink
 * DESCRIPTION : Show the cursor immediately after editing or opening.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void resetCursorBlink(void)
{
	cursorTime = cursorTicks();
	cursorVisible = 1;
}

/*
 * NAME : coopKeyboardMove
 * DESCRIPTION : Move the whole keypad within the screen borders.
 * NOTES : Includes the outer frame; dead zone prevents stick drift.
 * ARGS : Raw right-stick horizontal and vertical bytes, centered at 127.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopKeyboardMove(int horizontal, int vertical)
{
	u32 now = cursorTicks(), elapsed = now - moveTime;
	float step;
	moveTime = now;
	/* Cap long gaps so reopening or a stalled frame cannot jump the panel. */
	if (elapsed > 7372800)
		elapsed = 7372800;
	step = elapsed * (180.0f / 147456000.0f / 103.0f);
	horizontal -= 127;
	vertical -= 127;
	if (horizontal > 24)
		panelX += (horizontal - 24) * step;
	else if (horizontal < -24)
		panelX += (horizontal + 24) * step;
	if (vertical > 24)
		panelY += (vertical - 24) * step;
	else if (vertical < -24)
		panelY += (vertical + 24) * step;
	if (panelX < 2)
		panelX = 2;
	else if (panelX > SCREEN_WIDTH - PANEL_WIDTH - 2)
		panelX = SCREEN_WIDTH - PANEL_WIDTH - 2;
	if (panelY < 2)
		panelY = 2;
	else if (panelY > SCREEN_HEIGHT - PANEL_HEIGHT - 2)
		panelY = SCREEN_HEIGHT - PANEL_HEIGHT - 2;
}

/*
 * NAME : coopKeyboardNavigate
 * DESCRIPTION : Supply left-stick navigation for the next input update.
 * NOTES : Uses D-pad repeat timing; the dominant axis wins diagonals.
 * ARGS : Raw horizontal and vertical bytes, centered at 127.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopKeyboardNavigate(int horizontal, int vertical)
{
	int x, y;
	horizontal -= 127;
	vertical -= 127;
	x = horizontal < 0 ? -horizontal : horizontal;
	y = vertical < 0 ? -vertical : vertical;
	stickDirection = 0;
	if (x > 48 && x > y)
		stickDirection = horizontal < 0 ? PAD_LEFT : PAD_RIGHT;
	else if (y > 48)
		stickDirection = vertical < 0 ? PAD_UP : PAD_DOWN;
}

/*
 * NAME : deleteDigit
 * DESCRIPTION : Delete one character before the cursor.
 * NOTES : Square and the wide Delete key use the same action.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void deleteDigit(void)
{
	int i;
	if (cursor) {
		--cursor;
		for (i = cursor; i < length; ++i)
			input[i] = input[i + 1];
		--length;
		coopUiPlaySound(COOP_UI_DELETE);
	}
	if (!cursor)
		textStart = 0;
	resetCursorBlink();
}

/*
 * NAME : repeatNavigation
 * DESCRIPTION : Repeat a held direction after a short initial delay.
 * NOTES : Actions and submit buttons remain single-press; one direction wins diagonals.
 * ARGS : Newly pressed and held PAD bits.
 * RETURN : New presses with one optional repeated navigation step.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 repeatNavigation(u32 pressed, u32 held)
{
	u32 now = cursorTicks();
	u32 direction = held & PAD_UP ? PAD_UP : held & PAD_DOWN ? PAD_DOWN :
	    held & PAD_LEFT ? PAD_LEFT : held & PAD_RIGHT ? PAD_RIGHT : 0;
	u32 fresh = pressed & direction;
	pressed &= ~NAVIGATION_BUTTONS;
	if (!direction) {
		repeatDirection = 0;
		return pressed;
	}
	if (direction != repeatDirection || fresh) {
		repeatDirection = direction;
		repeatTime = now;
		repeatDelay = NAVIGATION_DELAY;
		return pressed | direction;
	}
	if (now - repeatTime >= repeatDelay) {
		repeatTime = now;
		repeatDelay = NAVIGATION_INTERVAL;
		return pressed | direction;
	}
	return pressed;
}

/*
 * NAME : centeredText
 * DESCRIPTION : Center and fit text inside a keypad row in pixel coordinates.
 * NOTES :
 * ARGS : Pixel geometry, text and color.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void centeredText(float x, float y, float width, const char* text, u32 color)
{
	float textWidth = gfxGetFontWidth(text, -1, 1);
	float available = width - 8;
	float scale = textWidth * INPUT_SCALE > available ? available / textWidth : INPUT_SCALE;
	float offset = (width - textWidth * scale) * .5f;
	gfxScreenSpaceText(x + offset, y, scale, scale, color, text, -1, 0);
}

/*
 * NAME : pixelBox
 * DESCRIPTION : Convert the keypad's pixel geometry to native screen coordinates.
 * NOTES : Equal width and height keep keys square in the game's render surface.
 * ARGS : Pixel rectangle and color.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void pixelBox(float x, float y, float width, float height, u32 color)
{
	gfxScreenSpaceBox(x / SCREEN_WIDTH, y / SCREEN_HEIGHT, width / SCREEN_WIDTH, height / SCREEN_HEIGHT, color);
}

/*
 * NAME : drawInput
 * DESCRIPTION : Keep the editing cursor visible without overflowing the field.
 * NOTES : Long decimal strings scroll inside the text field.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void drawInput(void)
{
	char text[COOP_KEYBOARD_TEXT_BYTES];
	int count, end;
	u32 now = cursorTicks(), periods = (now - cursorTime) / CURSOR_BLINK_TICKS;
	float x = GRID_X + 6, y = PANEL_Y + PANEL_PADDING + 8;
	float width = GRID_WIDTH - 12;
	if (periods) {
		cursorVisible ^= periods & 1;
		cursorTime += periods * CURSOR_BLINK_TICKS;
	}
	if (cursor < textStart)
		textStart = cursor;
	for (;;) {
		for (count = textStart; count < cursor; ++count)
			text[count - textStart] = input[count];
		text[cursor - textStart] = 0;
		if (gfxGetFontWidth(text, -1, INPUT_SCALE) <= width - 3 || textStart == cursor)
			break;
		++textStart;
	}
	if (cursorVisible) {
		float caret = x + gfxGetFontWidth(text, -1, INPUT_SCALE);
		pixelBox(caret, y - 1, 1, 16, TEXT_COLOR);
	}
	for (end = textStart; end < length; ++end) {
		text[end - textStart] = input[end];
		text[end - textStart + 1] = 0;
		if (gfxGetFontWidth(text, -1, INPUT_SCALE) > width) {
			text[end - textStart] = 0;
			break;
		}
	}
	if (end == textStart)
		text[0] = 0;
	gfxScreenSpaceText(x, y, INPUT_SCALE, INPUT_SCALE, TEXT_COLOR, text, -1, 0);
}

/*
 * NAME : coopKeyboardOpen
 * DESCRIPTION : Open a private text draft using the standalone keyboard.
 * NOTES :
 * ARGS : Initial text and maximum editable characters.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopKeyboardOpen(const char* text, int maximum)
{
	limit = maximum < COOP_KEYBOARD_TEXT_BYTES - 1 ? maximum : COOP_KEYBOARD_TEXT_BYTES - 1;
	if (limit < 0)
		limit = 0;
	length = 0;
	while (text && text[length] && length < limit) {
		input[length] = text[length];
		++length;
	}
	input[length] = 0;
	cursor = length;
	selected = 27;
	column = 5;
	uppercase = symbols = textStart = 0;
	repeatDirection = stickDirection = 0;
	panelX = (SCREEN_WIDTH - PANEL_WIDTH) * .5f;
	panelY = (SCREEN_HEIGHT - PANEL_HEIGHT) * .5f;
	moveTime = cursorTicks();
	resetCursorBlink();
	coopUiPlaySound(COOP_UI_OPEN);
}

/*
 * NAME : insertCharacter
 * DESCRIPTION : Insert a key or shortcut space at the text cursor.
 * NOTES : Respects the caller's character limit.
 * ARGS : Character to insert.
 * RETURN : Zero while editing.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int insertCharacter(char key)
{
	int i;
	if (!key || length >= limit)
		return 0;
	for (i = length; i > cursor; --i)
		input[i] = input[i - 1];
	input[cursor++] = key;
	input[++length] = 0;
	resetCursorBlink();
	coopUiPlaySound(COOP_UI_SELECT);
	return 0;
}

/*
 * NAME : coopKeyboardInput
 * DESCRIPTION : Navigate and edit the QWERTY keyboard.
 * NOTES :
 * ARGS : New and held PAD bits. L2 shifts case; L1/R1 move the text cursor.
 * RETURN : Zero editing, one accepted, negative cancelled.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopKeyboardInput(u32 pressed, u32 held)
{
	int row = selected < 44 ? selected / 11 : 4;
	int previous = selected, oldCursor = cursor;
	char key;
	if (pressed & PAD_TRIANGLE) {
		coopUiPlaySound(COOP_UI_CLOSE);
		return -1;
	}
	if (pressed & (PAD_START | PAD_R2)) {
		coopUiPlaySound(COOP_UI_CLOSE);
		return 1;
	}
	if (!(held & NAVIGATION_BUTTONS))
		held |= stickDirection;
	pressed = repeatNavigation(pressed, held);
	if (pressed & PAD_UP)
		row = (row + 4) % 5;
	else if (pressed & PAD_DOWN)
		row = (row + 1) % 5;
	else if (pressed & (PAD_LEFT | PAD_RIGHT)) {
		int count = row == 4 ? 5 : 11;
		column = (column + (pressed & PAD_LEFT ? count - 1 : 1)) % count;
	}
	if (row == 4 && column > 4)
		column = 4;
	selected = row == 4 ? 44 + column : row * 11 + column;
	if ((pressed & PAD_L1) && cursor)
		--cursor;
	else if ((pressed & PAD_R1) && cursor < length)
		++cursor;
	if (cursor != oldCursor)
		resetCursorBlink();
	if (selected != previous || cursor != oldCursor)
		coopUiPlaySound(COOP_UI_MOVE);
	if (pressed & PAD_SQUARE)
		deleteDigit();
	if (pressed & PAD_L2) {
		uppercase = !uppercase;
		coopUiPlaySound(COOP_UI_SELECT);
	}
	if (pressed & PAD_CIRCLE)
		return insertCharacter(' ');
	if (!(pressed & PAD_CROSS))
		return 0;
	if (selected == 48) {
		coopUiPlaySound(COOP_UI_CLOSE);
		return 1;
	}
	if (selected == 47) {
		deleteDigit();
		return 0;
	}
	if (selected == 44 || selected == 45) {
		if (selected == 44)
			uppercase = !uppercase;
		else
			symbols = !symbols;
		coopUiPlaySound(COOP_UI_SELECT);
		return 0;
	}
	key = selected == 46 ? ' ' : (symbols ? punctuation : letters)[selected];
	if (uppercase && key >= 'a' && key <= 'z')
		key -= 'a' - 'A';
	return insertCharacter(key);
}

/*
 * NAME : coopKeyboardDraw
 * DESCRIPTION : Draw the text field and keyboard in the numpad style.
 * NOTES :
 * ARGS : Call before native graphics-chain submission.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopKeyboardDraw(void)
{
	int i;
	const char* actions[] = {uppercase ? "\x16 abc" : "\x16 ABC", symbols ? "ABC" : "@#:", "\x11 Space", "\x13 Delete", "\x1f Done"};
	gfxScreenSpaceBox(0, 0, 1, 1, 0x60000000);
	pixelBox(PANEL_X - 2, PANEL_Y - 2, PANEL_WIDTH + 4, PANEL_HEIGHT + 4, 0x80303030);
	pixelBox(PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT, 0x80505050);
	pixelBox(GRID_X, PANEL_Y + PANEL_PADDING, GRID_WIDTH, INPUT_HEIGHT, 0x80303030);
	drawInput();
	for (i = 0; i < 49; ++i) {
		float width = i < 44 ? KEY_SIZE : (GRID_WIDTH - KEY_GAP * 4) / 5;
		float height = i < 44 ? KEY_SIZE : ACTION_HEIGHT;
		float x = GRID_X + (i < 44 ? (i % 11) * (KEY_SIZE + KEY_GAP) : (i - 44) * (width + KEY_GAP));
		float y = GRID_Y + (i < 44 ? (i / 11) * (KEY_SIZE + KEY_GAP) : GRID_HEIGHT + 8);
		char text[2] = {0, 0};
		if (i < 44) {
			text[0] = (symbols ? punctuation : letters)[i];
			if (uppercase && text[0] >= 'a' && text[0] <= 'z')
				text[0] -= 'a' - 'A';
		}
		if (i == selected)
			pixelBox(x - 2, y - 2, width + 4, height + 4, ACCENT_COLOR);
		pixelBox(x, y, width, height, i == selected ? 0x80707070 : 0x80404040);
		centeredText(x, y + (height - 16) * .5f, width, i < 44 ? text : actions[i - 44], TEXT_COLOR);
	}
}

/*
 * NAME : coopKeyboardText
 * DESCRIPTION : Read the current keyboard draft.
 * NOTES :
 * ARGS :
 * RETURN : Text remains valid until the next edit or opening.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
const char* coopKeyboardText(void)
{
	return input;
}
