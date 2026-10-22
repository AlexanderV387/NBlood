// Nintendo 3DS support: startup, the two screens, the touch screen and what
// newlib lacks. Game-independent; each game sets the hooks below.

#pragma once

#ifndef n3dsbits_h_
#define n3dsbits_h_

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// The game's folder on the SD card, where it looks for its data and
// writes its configuration, saves and log.
#define N3DS_GAME_DIR "sdmc:/3ds/" APPBASENAME

// First thing in main: faster clock, the game's folder as the working
// directory, the HOME Menu hooks.
void n3ds_open(void);

// New 3DS (804 MHz, the third core) or Old 3DS.
int n3ds_is_new3ds(void);

// The HOME Menu is closing the game: only quit, as soon as possible.
int n3ds_is_closing(void);

// Opens a file to write it whole, over its old contents if it exists:
// n3ds_fclose cuts it at the end. On the SD card, writing a file just
// created or emptied can take seconds.
FILE *n3ds_fopen_rewrite(const char *name);
int n3ds_fclose(FILE *file);

// Counted by platform/3DS/ndsp/ndsp-channel.c.
extern volatile uint32_t ndspChnListResyncs;

// Where the game's thread last was, and how many times the audio was
// mixed: logged by the thread that presents the frames when they stop
// coming.
extern const char *volatile n3ds_where;
extern volatile uint32_t n3ds_audio_mixes;
#define N3DS_WHERE(place) (n3ds_where = (place))

// ==========================================================================
// The screens. The 8-bit frame goes to the top screen (400x240), composed
// on another core at the vertical blank; the bottom screen (320x240,
// 8-bit too, with the same palette) is drawn by the game 30 times a second.

#define N3DS_BOTTOM_WIDTH  320
#define N3DS_BOTTOM_HEIGHT 240

// palette: 256 entries of r, g, b, (unused), as the engine's palette_t.
void n3ds_present(const uint8_t *frame, int width, int height, const uint8_t *palette);

// Set by the game: draws the bottom screen (from the game's thread);
// returns whether it shows anything (otherwise black, and a tap turns
// the backlight off).
extern int (*n3ds_draw_bottom)(uint8_t *screen);

// Set by the game: playing (in a level, with no menu).
extern int (*n3ds_in_game)(void);

// Set by the game once its configuration is loaded: saves it. Called when
// the HOME Menu opens, as the game may be closed from there (and then the
// SD card is much slower).
extern void (*n3ds_save_on_suspend)(void);

// The configuration was saved when the HOME Menu opened, and the game is
// being closed from there: no need to write it again.
int n3ds_config_already_saved(void);

// Colors of the game's palette nearest to these, for the bottom screen's
// buttons and keyboard (updated with the palette).
extern uint8_t n3ds_ui_black, n3ds_ui_dark, n3ds_ui_gray, n3ds_ui_white;

void n3ds_fill(uint8_t *screen, int x, int y, int w, int h, int color);
void n3ds_draw_text(uint8_t *screen, int x, int y, const char *text, int scale, int color);

// ==========================================================================
// The touch screen.

// Where the bottom screen is being touched, if it is.
int n3ds_get_touch(int *x, int *y);

// Touches starting in these rectangles (the bottom screen's own buttons,
// set each time they are drawn) do not turn the view.
void n3ds_clear_reserved_touch(void);
void n3ds_reserve_touch(int x, int y, int w, int h);
int n3ds_touch_reserved(int x, int y);

// Touch turning: how far the finger moved since the last call (bottom
// screen pixels), for a drag that did not start on a button.
void n3ds_touch_drag(int *dx, int *dy);

// A key press as from a PC keyboard (an SDL scancode).
void n3ds_post_key(int scancode, int down);

// The keyboard on the bottom screen: its keys reach the game as key
// presses. Opened by its button and when the game asks for text.
void n3ds_show_keyboard(int show);
int n3ds_keyboard_open(void);

// The map's buttons on the bottom screen: the keyboard at the top left,
// zoom "+" and "-" at the bottom left. Returns the zoom (percent).
int n3ds_update_map_buttons(int map_y, int map_height);
void n3ds_draw_map_buttons(uint8_t *screen, int map_y, int map_height);

// The weapon buttons: a row of numbered buttons at row y (1, 2, ... and 0
// for the tenth), the weapons of the bits of owned white, in_hand's on
// gray; a touch on one holds that number's key while touched.
#define N3DS_WEAPON_BAR_HEIGHT 24
void n3ds_draw_weapon_buttons(uint8_t *screen, int y, int slots, unsigned owned, int in_hand);
void n3ds_update_weapon_buttons(int y, int slots);

// ==========================================================================
// Options of the 3DS, saved in the game's configuration.

extern int32_t n3ds_bottom_map;     // the map on the bottom screen
extern int32_t n3ds_status_below;   // bottom screen: status bar below the map
extern int32_t n3ds_classic_hud;    // status bar on the top screen, as on PC
extern int32_t n3ds_weapon_bar;     // weapon buttons at the bottom
extern int32_t n3ds_show_fps;       // frame counter on the bottom screen
extern int32_t n3ds_map_zoom;       // bottom screen map zoom (percent)
extern int32_t n3ds_touch_turn;     // sliding on the bottom screen turns
extern int32_t n3ds_touch_speed;    // 1-10
extern int32_t n3ds_run_mode;       // see N3DS_RUN_*

enum
{
    N3DS_RUN_HOLD,  // while the run button is held
    N3DS_RUN_ONCE,  // from a press until the player stops moving
    N3DS_RUN_STICK, // the Circle Pad fully pushed runs
    N3DS_NUM_RUN_MODES
};

#ifdef __cplusplus
}
#endif

#endif
