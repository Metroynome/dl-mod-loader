#ifndef DL_CAMPAIGN_MENU_H
#define DL_CAMPAIGN_MENU_H

/* Native frame/pause functions verified against retail boot/campaign/co-op overlays. */
#include <librac4/interop.h>

#if DL_MODULE_REGION == 2

static VariableAddress_t vaFrameSubmit = {
	.MainMenu = 0x4e8630,
	.Battledome = 0x638418,
	.Catacrom = 0x5e7e88,
	.Sarathos = 0x5e7a30,
	.Kronos = 0x60e418,
	.Shaar = 0x5e0008,
	.Valix = 0x5d2fb8,
	.Orxon = 0x5ead30,
	.Torval = 0x5f5668,
	.Stygia = 0x5e2490,
	.Maraxus = 0x5fbcb0,
	.GhostStation = 0x5f59a8,
	.DreadZoneInterior = 0x606128
};

static VariableAddress_t vaPauseButton = {
	.MainMenu = 0x0,
	.Battledome = 0x5fc4a8,
	.Catacrom = 0x5ac658,
	.Sarathos = 0x5ac200,
	.Kronos = 0x5d2420,
	.Shaar = 0x5a43e0,
	.Valix = 0x5973b8,
	.Orxon = 0x5aedc0,
	.Torval = 0x5b96f8,
	.Stygia = 0x5a6498,
	.Maraxus = 0x5bfd40,
	.GhostStation = 0x5b9da8,
	.DreadZoneInterior = 0x5ca1b8
};

static VariableAddress_t vaPauseClose = {
	.MainMenu = 0x0,
	.Battledome = 0x5fd1e0,
	.Catacrom = 0x5ad390,
	.Sarathos = 0x5acf38,
	.Kronos = 0x5d3158,
	.Shaar = 0x5a5118,
	.Valix = 0x5980f0,
	.Orxon = 0x5afaf8,
	.Torval = 0x5ba430,
	.Stygia = 0x5a71d0,
	.Maraxus = 0x5c0a78,
	.GhostStation = 0x5baae0,
	.DreadZoneInterior = 0x5caef0
};

static VariableAddress_t vaPauseCall = {
	.MainMenu = 0x0,
	.Battledome = 0x5fc984,
	.Catacrom = 0x5acb34,
	.Sarathos = 0x5ac6dc,
	.Kronos = 0x5d28fc,
	.Shaar = 0x5a48bc,
	.Valix = 0x597894,
	.Orxon = 0x5af29c,
	.Torval = 0x5b9bd4,
	.Stygia = 0x5a6974,
	.Maraxus = 0x5c021c,
	.GhostStation = 0x5ba284,
	.DreadZoneInterior = 0x5ca694
};

#elif DL_MODULE_REGION == 3 || DL_MODULE_REGION == 4

static VariableAddress_t vaFrameSubmit = {
	.MainMenu = 0x50ba78,
	.Battledome = 0x64ec80,
	.Catacrom = 0x5feb70,
	.Sarathos = 0x5fe958,
	.Kronos = 0x625140,
	.Shaar = 0x5f71b0,
	.Valix = 0x5e9fe0,
	.Orxon = 0x601a58,
	.Torval = 0x60c3d0,
	.Stygia = 0x5f94b8,
	.Maraxus = 0x612958,
	.GhostStation = 0x60c690,
	.DreadZoneInterior = 0x61ce90
};

static VariableAddress_t vaPauseButton = {
	.MainMenu = 0x0,
	.Battledome = 0x612c80,
	.Catacrom = 0x5c32b0,
	.Sarathos = 0x5c3098,
	.Kronos = 0x5e90b8,
	.Shaar = 0x5bb4f8,
	.Valix = 0x5ae350,
	.Orxon = 0x5c5a58,
	.Torval = 0x5d03d0,
	.Stygia = 0x5bd430,
	.Maraxus = 0x5d6958,
	.GhostStation = 0x5d0a00,
	.DreadZoneInterior = 0x5e0e90
};

static VariableAddress_t vaPauseClose = {
	.MainMenu = 0x0,
	.Battledome = 0x6139b8,
	.Catacrom = 0x5c3fe8,
	.Sarathos = 0x5c3dd0,
	.Kronos = 0x5e9df0,
	.Shaar = 0x5bc230,
	.Valix = 0x5af088,
	.Orxon = 0x5c6790,
	.Torval = 0x5d1108,
	.Stygia = 0x5be168,
	.Maraxus = 0x5d7690,
	.GhostStation = 0x5d1738,
	.DreadZoneInterior = 0x5e1bc8
};

static VariableAddress_t vaPauseCall = {
	.MainMenu = 0x0,
	.Battledome = 0x61315c,
	.Catacrom = 0x5c378c,
	.Sarathos = 0x5c3574,
	.Kronos = 0x5e9594,
	.Shaar = 0x5bb9d4,
	.Valix = 0x5ae82c,
	.Orxon = 0x5c5f34,
	.Torval = 0x5d08ac,
	.Stygia = 0x5bd90c,
	.Maraxus = 0x5d6e34,
	.GhostStation = 0x5d0edc,
	.DreadZoneInterior = 0x5e136c
};

#else

static VariableAddress_t vaFrameSubmit = {
	.MainMenu = 0x4e7708,
	.Battledome = 0x633430,
	.Catacrom = 0x5e34e0,
	.Sarathos = 0x5e3208,
	.Kronos = 0x6099f0,
	.Shaar = 0x5dbae0,
	.Valix = 0x5ced50,
	.Orxon = 0x5e63c8,
	.Torval = 0x5f0d40,
	.Stygia = 0x5ddde8,
	.Maraxus = 0x5f7288,
	.GhostStation = 0x5f1000,
	.DreadZoneInterior = 0x601640
};

static VariableAddress_t vaPauseButton = {
	.MainMenu = 0x0,
	.Battledome = 0x5f7778,
	.Catacrom = 0x5a7f68,
	.Sarathos = 0x5a7c90,
	.Kronos = 0x5cdcb0,
	.Shaar = 0x5a0170,
	.Valix = 0x593408,
	.Orxon = 0x5aa710,
	.Torval = 0x5b5088,
	.Stygia = 0x5a20a8,
	.Maraxus = 0x5bb5d0,
	.GhostStation = 0x5b56b8,
	.DreadZoneInterior = 0x5c5988
};

static VariableAddress_t vaPauseClose = {
	.MainMenu = 0x0,
	.Battledome = 0x5f84b0,
	.Catacrom = 0x5a8ca0,
	.Sarathos = 0x5a89c8,
	.Kronos = 0x5ce9e8,
	.Shaar = 0x5a0ea8,
	.Valix = 0x594140,
	.Orxon = 0x5ab448,
	.Torval = 0x5b5dc0,
	.Stygia = 0x5a2de0,
	.Maraxus = 0x5bc308,
	.GhostStation = 0x5b63f0,
	.DreadZoneInterior = 0x5c66c0
};

static VariableAddress_t vaPauseCall = {
	.MainMenu = 0x0,
	.Battledome = 0x5f7c54,
	.Catacrom = 0x5a8444,
	.Sarathos = 0x5a816c,
	.Kronos = 0x5ce18c,
	.Shaar = 0x5a064c,
	.Valix = 0x5938e4,
	.Orxon = 0x5aabec,
	.Torval = 0x5b5564,
	.Stygia = 0x5a2584,
	.Maraxus = 0x5bbaac,
	.GhostStation = 0x5b5b94,
	.DreadZoneInterior = 0x5c5e64
};

#endif
#endif
