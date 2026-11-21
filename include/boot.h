#ifndef DL_BOOT_H
#define DL_BOOT_H

#include <librac4/game.h>
#include <librac4/map.h>

#if DL_MODULE_REGION == 2
#define DL_BOOT_IDLE_CHECK (0x4389fc)
#define DL_BOOT_IDLE_TIMER (0x21e258)
#define DL_BOOT_IDLE_LOAD (0x8f828859)
#define DL_BOOT_IDLE_COMPARE (0x28431771)
#elif DL_MODULE_REGION == 3 || DL_MODULE_REGION == 4
#define DL_BOOT_IDLE_CHECK (0x45c420)
#define DL_BOOT_IDLE_TIMER (0x2389d8)
#define DL_BOOT_IDLE_LOAD (0x8f828819)
#define DL_BOOT_IDLE_COMPARE (0x28431c21)
#else
#define DL_BOOT_IDLE_CHECK (0x438420)
#define DL_BOOT_IDLE_TIMER (0x21e1d8)
#define DL_BOOT_IDLE_LOAD (0x8f828859)
#define DL_BOOT_IDLE_COMPARE (0x28431c21)
#endif

/*
 * NAME : dlResetBootIdle
 * DESCRIPTION : Prevent the boot-menu trailer from taking captured input away.
 * NOTES : Shared by Mod Menu and Co-op. No instructions are patched; the native
 *         countdown resumes if neither module is active. Retail 1.00 mappings.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static inline void dlResetBootIdle(void)
{
	const volatile u32* check = (const volatile u32*)DL_BOOT_IDLE_CHECK;
	if (Level > 1 || (Level == 1 && Mission->ActiveMission != -1))
		return;
	/* Match the native load, increment, two-minute limit, branch and store.
	 * This also avoids touching the counter while another overlay is loaded. */
	if (check[0] != DL_BOOT_IDLE_LOAD || check[1] != 0x24420001 ||
	    check[2] != DL_BOOT_IDLE_COMPARE || check[3] != 0x14600025 ||
	    check[4] != DL_BOOT_IDLE_LOAD + 0x20000000)
		return;
	*(volatile u32*)DL_BOOT_IDLE_TIMER = 0;
}

#endif
