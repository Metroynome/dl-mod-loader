#include "numpad.h"
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
#define KEY_SIZE (44.0f)
#define KEY_GAP (5.0f)
#define PANEL_PADDING (12.0f)
#define INPUT_HEIGHT (32.0f)
#define ACTION_HEIGHT (30.0f)
#define GRID_WIDTH (KEY_SIZE * 3 + KEY_GAP * 2)
#define GRID_HEIGHT (KEY_SIZE * 4 + KEY_GAP * 3)
#define GRID_TOP (PANEL_PADDING + INPUT_HEIGHT + 26)
#define PANEL_WIDTH (GRID_WIDTH + PANEL_PADDING * 2)
#define PANEL_HEIGHT (GRID_TOP + GRID_HEIGHT + 8 + ACTION_HEIGHT + PANEL_PADDING)
#define PANEL_X (panelX)
#define PANEL_Y (panelY)
#define GRID_X (PANEL_X + PANEL_PADDING)
#define GRID_Y (PANEL_Y + GRID_TOP)

static coopNumpadKind_e inputKind;
static char input[COOP_NUMPAD_TEXT_BYTES];
static int length, cursor, selected, column, invalid, textStart;
static u32 acceptedValue;
static u32 cursorTime;
static u32 moveTime;
static float panelX, panelY;
static u32 repeatDirection, repeatTime, repeatDelay;
static u32 stickDirection;
static int cursorVisible;
static double acceptedNumber, minimumValue, maximumValue;
static const char* keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "-", "\x13 Delete", "\x1f Done"};

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
 * NAME : appendNumber
 * DESCRIPTION : Append one unsigned decimal value.
 * NOTES :
 * ARGS : Destination and value.
 * RETURN : First byte after the number.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static char* appendNumber(char* text, u32 value)
{
	char digits[10];
	int count = 0;
	do {
		digits[count++] = '0' + value % 10;
		value /= 10;
	} while (value);
	while (count)
		*text++ = digits[--count];
	*text = 0;
	return text;
}

/*
 * NAME : coopNumpadFormat
 * DESCRIPTION : Format an IPv4 value or unsigned integer for the caller.
 * NOTES : Signed decimal Number input is supplied as text to Open.
 * ARGS : Input kind, value and a destination of at least 16 bytes.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopNumpadFormat(coopNumpadKind_e kind, u32 value, char* text)
{
	int i;
	if (kind != COOP_NUMPAD_ADDRESS) {
		appendNumber(text, value);
		return;
	}
	for (i = 0; i < 4; ++i) {
		text = appendNumber(text, (value >> (i * 8)) & 255);
		if (i < 3)
			*text++ = '.';
	}
	*text = 0;
}

/*
 * NAME : parseAddress
 * DESCRIPTION : Validate four complete IPv4 octets.
 * NOTES : An empty address means none.
 * ARGS : Output IPv4 bytes.
 * RETURN : Nonzero when valid.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int parseAddress(u32* result)
{
	u32 value = 0, address = 0;
	int i, octet = 0, digits = 0;
	if (!length) {
		*result = 0;
		return 1;
	}
	for (i = 0; i <= length; ++i) {
		char c = input[i];
		if (c >= '0' && c <= '9') {
			value = value * 10 + c - '0';
			if (++digits > 3 || value > 255)
				return 0;
		} else if (c == '.' || !c) {
			if (!digits || octet > 3)
				return 0;
			address |= value << (octet++ * 8);
			value = digits = 0;
			if (!c) {
				*result = address;
				return octet == 4;
			}
		} else {
			return 0;
		}
	}
	return 0;
}

/*
 * NAME : parseInput
 * DESCRIPTION : Validate IP, integer port or signed decimal Number input.
 * NOTES : No exponent notation; punctuation rules depend on the input type.
 * ARGS : Integer/IP output and decimal output.
 * RETURN : Nonzero when the complete input is valid and within bounds.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int parseInput(u32* integer, double* number)
{
	double value = 0, fraction = .1;
	int i = 0, digits = 0, point = 0, negative = 0;
	if (inputKind == COOP_NUMPAD_ADDRESS)
		return parseAddress(integer);
	if (inputKind == COOP_NUMPAD_NUMBER && input[0] == '-') {
		negative = 1;
		++i;
	}
	for (; i < length; ++i) {
		char c = input[i];
		if (c == '.' && inputKind == COOP_NUMPAD_NUMBER && !point) {
			point = 1;
		} else if (c >= '0' && c <= '9') {
			++digits;
			if (point) {
				value += (c - '0') * fraction;
				fraction *= .1;
			} else {
				value = value * 10 + c - '0';
			}
		} else {
			return 0;
		}
	}
	if (negative)
		value = -value;
	if (!digits || !(value >= minimumValue && value <= maximumValue))
		return 0;
	if (inputKind == COOP_NUMPAD_PORT) {
		if (value < 1 || value > 65535)
			return 0;
		*integer = (u32)value;
	}
	*number = value;
	return 1;
}

/*
 * NAME : coopNumpadOpen
 * DESCRIPTION : Start editing a private copy of the supplied text.
 * NOTES : The cursor starts at the end of the existing value.
 * ARGS : Input type, initial text and numeric minimum/maximum.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopNumpadOpen(coopNumpadKind_e kind, const char* text, double minimum, double maximum)
{
	inputKind = kind;
	minimumValue = minimum;
	maximumValue = maximum;
	length = 0;
	while (text && text[length] && length < COOP_NUMPAD_TEXT_BYTES - 1) {
		input[length] = text[length];
		++length;
	}
	input[length] = 0;
	cursor = length;
	selected = 4;
	column = 1;
	invalid = textStart = 0;
	acceptedValue = 0;
	acceptedNumber = 0;
	parseInput(&acceptedValue, &acceptedNumber);
	resetCursorBlink();
	repeatDirection = 0;
	stickDirection = 0;
	panelX = (SCREEN_WIDTH - PANEL_WIDTH) * .5f;
	panelY = (SCREEN_HEIGHT - PANEL_HEIGHT) * .5f;
	moveTime = cursorTicks();
	coopUiPlaySound(COOP_UI_OPEN);
}

/*
 * NAME : coopNumpadMove
 * DESCRIPTION : Move the whole keypad within the screen borders.
 * NOTES : Includes the outer frame; dead zone prevents stick drift.
 * ARGS : Raw right-stick horizontal and vertical bytes, centered at 127.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopNumpadMove(int horizontal, int vertical)
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
 * NAME : coopNumpadNavigate
 * DESCRIPTION : Supply left-stick navigation for the next input update.
 * NOTES : Uses D-pad repeat timing; the dominant axis wins diagonals.
 * ARGS : Raw horizontal and vertical bytes, centered at 127.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopNumpadNavigate(int horizontal, int vertical)
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
	invalid = 0;
	resetCursorBlink();
}

/*
 * NAME : submitInput
 * DESCRIPTION : Commit only a valid completed value.
 * NOTES : A failed validation never changes the accepted outputs.
 * ARGS :
 * RETURN : One accepted, zero still editing.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int submitInput(void)
{
	u32 value = 0;
	double number = 0;
	if (!parseInput(&value, &number)) {
		invalid = 1;
		return 0;
	}
	acceptedValue = value;
	acceptedNumber = number;
	coopUiPlaySound(COOP_UI_CLOSE);
	return 1;
}

/*
 * NAME : keyEnabled
 * DESCRIPTION : Keep punctuation visible but disable it for unsuitable types.
 * NOTES :
 * ARGS : Key index.
 * RETURN : Nonzero when the key accepts X.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int keyEnabled(int key)
{
	return !(key == 9 && inputKind == COOP_NUMPAD_PORT) &&
	    !(key == 11 && inputKind != COOP_NUMPAD_NUMBER);
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
 * NAME : coopNumpadInput
 * DESCRIPTION : Navigate the PS5-style grid and edit text at the cursor.
 * NOTES : L1/R1 moves the cursor; Start/R2 submits; Triangle cancels.
 * ARGS : New presses and held PAD bits. Square deletes; X inserts the key.
 * RETURN : Zero editing, one accepted, negative cancelled.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int coopNumpadInput(u32 pressed, u32 held)
{
	int row = selected < 12 ? selected / 3 : 4;
	int previousKey = selected, previousCursor = cursor;
	int i, limit = inputKind == COOP_NUMPAD_ADDRESS ? 15 :
	    inputKind == COOP_NUMPAD_PORT ? 5 : COOP_NUMPAD_TEXT_BYTES - 1;
	if (pressed & PAD_TRIANGLE) {
		coopUiPlaySound(COOP_UI_CLOSE);
		return -1;
	}
	if (pressed & (PAD_START | PAD_R2))
		return submitInput();
	if (!(held & NAVIGATION_BUTTONS))
		held |= stickDirection;
	pressed = repeatNavigation(pressed, held);
	if (pressed & PAD_UP)
		row = (row + 4) % 5;
	else if (pressed & PAD_DOWN)
		row = (row + 1) % 5;
	else if (row == 4 && (pressed & (PAD_LEFT | PAD_RIGHT))) {
		selected = selected == 12 ? 13 : 12;
		column = selected == 12 ? 0 : 2;
	} else if (row < 4 && (pressed & PAD_LEFT))
		column = (column + 2) % 3;
	else if (row < 4 && (pressed & PAD_RIGHT))
		column = (column + 1) % 3;
	if (row < 4)
		selected = row * 3 + column;
	else if (pressed & (PAD_UP | PAD_DOWN))
		selected = column == 2 ? 13 : 12;
	if (pressed & (PAD_L1 | PAD_R1)) {
		if ((pressed & PAD_L1) && cursor)
			--cursor;
		else if ((pressed & PAD_R1) && cursor < length)
			++cursor;
		resetCursorBlink();
	}
	if (selected != previousKey || cursor != previousCursor)
		coopUiPlaySound(COOP_UI_MOVE);
	if (pressed & PAD_SQUARE)
		deleteDigit();
	if (!(pressed & PAD_CROSS) || !keyEnabled(selected))
		return 0;
	if (selected == 13)
		return submitInput();
	if (selected == 12) {
		deleteDigit();
		return 0;
	}
	invalid = 0;
	if (length < limit) {
		for (i = length; i > cursor; --i)
			input[i] = input[i - 1];
		input[cursor++] = keys[selected][0];
		input[++length] = 0;
		resetCursorBlink();
		coopUiPlaySound(COOP_UI_SELECT);
	}
	return 0;
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
	char text[COOP_NUMPAD_TEXT_BYTES];
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
 * NAME : coopNumpadDraw
 * DESCRIPTION : Draw the input field, keypad and paired action buttons.
 * NOTES : Call from a safe native UI render pass, never a VSync callback.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void coopNumpadDraw(void)
{
	int i;
	gfxScreenSpaceBox(0, 0, 1, 1, 0x60000000);
	pixelBox(PANEL_X - 2, PANEL_Y - 2, PANEL_WIDTH + 4, PANEL_HEIGHT + 4, 0x80303030);
	pixelBox(PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT, 0x80505050);
	pixelBox(GRID_X, PANEL_Y + PANEL_PADDING, GRID_WIDTH, INPUT_HEIGHT, 0x80303030);
	drawInput();
	if (invalid)
		centeredText(PANEL_X, PANEL_Y + PANEL_PADDING + INPUT_HEIGHT + 6, PANEL_WIDTH,
		    inputKind == COOP_NUMPAD_ADDRESS ? "Four numbers: 0 to 255" :
		    inputKind == COOP_NUMPAD_PORT ? "Port: 1 to 65535" : "Invalid or out of range", ACCENT_COLOR);
	for (i = 0; i < 14; ++i) {
		float actionWidth = (GRID_WIDTH - KEY_GAP) * .5f;
		float width = i < 12 ? KEY_SIZE : actionWidth;
		float height = i < 12 ? KEY_SIZE : ACTION_HEIGHT;
		float x = GRID_X + (i < 12 ? (i % 3) * (KEY_SIZE + KEY_GAP) : (i - 12) * (actionWidth + KEY_GAP));
		float y = GRID_Y + (i < 12 ? (i / 3) * (KEY_SIZE + KEY_GAP) : GRID_HEIGHT + 8);
		if (i == selected)
			pixelBox(x - 2, y - 2, width + 4, height + 4, ACCENT_COLOR);
		pixelBox(x, y, width, height, i == selected ? 0x80707070 : 0x80404040);
		centeredText(x, y + (height - 16) * .5f, width, keys[i], keyEnabled(i) ? TEXT_COLOR : 0x80606060);
	}
}

/*
 * NAME : coopNumpadValue
 * DESCRIPTION : Read the last accepted IP address or port.
 * NOTES : Use coopNumpadNumber for signed decimal Number input.
 * ARGS :
 * RETURN : IPv4 bytes or a port; zero for the Number type.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
u32 coopNumpadValue(void)
{
	return acceptedValue;
}

/*
 * NAME : coopNumpadNumber
 * DESCRIPTION : Read the last accepted numeric value without losing its sign.
 * NOTES : The text getter preserves the entered decimal spelling.
 * ARGS :
 * RETURN : Signed decimal value.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
double coopNumpadNumber(void)
{
	return acceptedNumber;
}

/*
 * NAME : coopNumpadText
 * DESCRIPTION : Read the editor text for the owning UI.
 * NOTES : Valid until the next editor operation; consume after acceptance.
 * ARGS :
 * RETURN : Current input text.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
const char* coopNumpadText(void)
{
	return input;
}
