/* PINE only writes data; commands execute here on the normal module tick. */
#include "modules/forge/include/forge.h"

/* Part of Forge's BSS and module memory size, not a loader reservation.
 * PINE follows the module discovery header and exports table; see PINE.md. */
volatile forgeMailbox_t forgeMailbox __attribute__((aligned(16)));
const forgeExports_t moduleExports DL_MODULE_EXPORTS = {FORGE_EXPORTS_MAGIC, 1, sizeof(forgeExports_t), &forgeMailbox};
static u32 lastRequestId, activeRequestId;
static const u8 stagingCommands = (1 << FORGE_REFRESH) | (1 << FORGE_SELECT) |
                                 (1 << FORGE_REFRESH_SELECT) | (1 << FORGE_START_GAME) |
                                 (1 << FORGE_SELECT_RETAIL);
static const u8 gameCommands = (1 << FORGE_RELOAD) | (1 << FORGE_REFRESH) | (1 << FORGE_SELECT) |
                              (1 << FORGE_REFRESH_SELECT) | (1 << FORGE_SELECT_RETAIL);

void forgeBridgeDisable(void)
{
	volatile forgeMailbox_t* mailbox = FORGE_MAILBOX;
	mailbox->available = mailbox->capabilities = mailbox->context = 0;
	if (activeRequestId) {
		mailbox->state = FORGE_FAILED;
		mailbox->error = FORGE_ERROR_INACTIVE;
		mailbox->completedId = activeRequestId;
		activeRequestId = 0;
	}
	lastRequestId = mailbox->requestId;
	mailbox->gameMode = FORGE_MODE_CQ;
}

void forgeBridgeInit(void)
{
	volatile forgeMailbox_t* mailbox = FORGE_MAILBOX;
	u32 i;
	for (i = 0; i < sizeof(*mailbox) / 4; ++i)
		((volatile u32*)mailbox)[i] = 0;
	lastRequestId = activeRequestId = 0;
	mailbox->magic = FORGE_MAILBOX_MAGIC;
}

void forgeBridgeTick(const mapHopApi_t* hopApi)
{
	volatile forgeMailbox_t* mailbox = FORGE_MAILBOX;
	u32 id, command, i;
	u8 gameMode;
	char name[FORGE_FILENAME_SIZE];
	int error;
	++mailbox->heartbeat;
	mailbox->available = hopApi != 0;
	mailbox->context = hopApi ? hopApi->context() : FORGE_CONTEXT_UNAVAILABLE;
	mailbox->capabilities = mailbox->context == FORGE_CONTEXT_STAGING ? stagingCommands :
	                  mailbox->context == FORGE_CONTEXT_GAME ? gameCommands : 0;
	if (activeRequestId) {
		mailbox->state = hopApi ? hopApi->state() : FORGE_FAILED;
		mailbox->error = hopApi ? hopApi->error() : FORGE_ERROR_INACTIVE;
		if (mailbox->state == FORGE_COMPLETE || mailbox->state == FORGE_FAILED) {
			mailbox->completedId = activeRequestId;
			activeRequestId = 0;
		}
	}
	id = mailbox->requestId;
	if (!id || id == lastRequestId)
		return;
	command = mailbox->command;
	gameMode = mailbox->gameMode;
	for (i = 0; i < sizeof(name); ++i)
		name[i] = mailbox->filename[i];
	if (id != mailbox->requestId)
		return; /* Host is still submitting a newer request. */
	lastRequestId = id;
	/* A subsequent host request may omit the optional mode byte. */
	mailbox->gameMode = FORGE_MODE_CQ;
	for (i = 0; i < sizeof(name) && name[i]; ++i)
		;
	if ((command == FORGE_SELECT || command == FORGE_REFRESH_SELECT || command == FORGE_SELECT_RETAIL) &&
	    i == sizeof(name)) {
		mailbox->rejectError = FORGE_ERROR_NAME;
		mailbox->rejectedId = id;
		return;
	}
	error = activeRequestId ? FORGE_ERROR_BUSY : !hopApi ? FORGE_ERROR_INACTIVE : hopApi->request(command, name, gameMode);
	if (error) {
		mailbox->rejectError = error;
		mailbox->rejectedId = id;
		return;
	}
	mailbox->acceptedId = id;
	activeRequestId = id;
	mailbox->state = hopApi->state();
	mailbox->error = 0;
	if (mailbox->state == FORGE_COMPLETE || mailbox->state == FORGE_FAILED) {
		mailbox->completedId = id;
		activeRequestId = 0;
	}
}
