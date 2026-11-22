#ifndef DL_COOP_NUMPAD_H
#define DL_COOP_NUMPAD_H

#include "module.h"

#define COOP_NUMPAD_TEXT_BYTES (32)

typedef enum coopNumpadKind {
	COOP_NUMPAD_ADDRESS,
	COOP_NUMPAD_PORT,
	COOP_NUMPAD_NUMBER
} coopNumpadKind_e;

/* No Mod Menu dependency. The caller owns the overlay's input capture and
 * release barrier, and draws once per frame before graphics-chain submission.
 * Input takes new presses and held buttons; held directions repeat.
 * Navigate supplies raw left-stick X/Y bytes before Input, independently of Mod Menu.
 * Return values are 0 editing, 1 accepted, -1 cancelled.
 * Number accepts signed decimal text within minimum/maximum; Port accepts
 * only integers from 1 through 65535 (also constrained by the supplied limits).
 * Limits are ignored for IP input. Initial text is copied, not retained.
 * Values are committed only on Done, Start or R2.
 * IPv4 uses the Online API's byte order. */
void coopNumpadOpen(coopNumpadKind_e kind, const char* text, double minimum, double maximum);
int coopNumpadInput(u32 pressed, u32 held);
void coopNumpadDraw(void);
void coopNumpadMove(int horizontal, int vertical);
void coopNumpadNavigate(int horizontal, int vertical);
u32 coopNumpadValue(void);
double coopNumpadNumber(void);
const char* coopNumpadText(void);
void coopNumpadFormat(coopNumpadKind_e kind, u32 value, char* text);

#endif
