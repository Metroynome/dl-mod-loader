/* Use the loader's freestanding string routines, without another libc. */
#include "../../src/strings.c"

/* Freestanding structure/buffer copies used by the networking module. */
void* memcpy(void* destination, const void* source, u32 size)
{
	u8* out = destination;
	const u8* in = source;
	u32 i;
	for (i = 0; i < size; ++i)
		out[i] = in[i];
	return destination;
}
