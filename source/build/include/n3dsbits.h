// Nintendo 3DS support: startup and what newlib lacks.

#pragma once

#ifndef n3dsbits_h_
#define n3dsbits_h_

#ifdef __cplusplus
extern "C" {
#endif

// The game's folder on the SD card, where it looks for its data and
// writes its configuration, saves and log.
#define N3DS_GAME_DIR "sdmc:/3ds/" APPBASENAME

// First thing in main: faster clock, and the game's folder as the
// working directory.
void n3ds_open(void);

// Counted by platform/3DS/ndsp/ndsp-channel.c.
extern volatile uint32_t ndspChnListResyncs;

#ifdef __cplusplus
}
#endif

#endif
