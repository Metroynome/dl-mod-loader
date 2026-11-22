#ifndef DL_COOP_KEYBOARD_H
#define DL_COOP_KEYBOARD_H

#include "module.h"

#define COOP_KEYBOARD_TEXT_BYTES (32)

/* Standalone UI: caller owns input capture, release barrier and safe drawing.
 * D-pad/left stick repeat; right stick moves the bounded panel. X inserts,
 * Square deletes, Circle inserts a space, L1/R1 move the cursor, L2 shifts, Start/R2 accepts,
 * Triangle cancels. Accept/cancel never write the caller's original string. */
void coopKeyboardOpen(const char* text, int maximum);
int coopKeyboardInput(u32 pressed, u32 held);
void coopKeyboardDraw(void);
void coopKeyboardMove(int horizontal, int vertical);
void coopKeyboardNavigate(int horizontal, int vertical);
const char* coopKeyboardText(void);

#endif
