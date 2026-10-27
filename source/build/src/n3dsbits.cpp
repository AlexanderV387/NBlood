// Nintendo 3DS support: startup, the two screens, the touch screen and what
// newlib lacks. Much of it as in the n3ds-ports project's Crispy Doom.

#include "compat.h"
#include "log.h"
#include "n3dsbits.h"
#include "sdl_inc.h"

#include <3ds.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <unordered_map>
#include <unordered_set>

#ifndef APPBASENAME
# define APPBASENAME "eduke32"
#endif

// The main thread's stack: 32 KB by default, too little for the engine.
extern "C" u32 __stacksize__ = 1 << 20;

#define TOP_WIDTH  400
#define TOP_HEIGHT 240

int32_t n3ds_bottom_map   = 1;
int32_t n3ds_status_below = 0;
int32_t n3ds_classic_hud  = 0;
int32_t n3ds_weapon_bar   = 0;
int32_t n3ds_show_fps     = 0;
int32_t n3ds_map_zoom     = 100;
int32_t n3ds_touch_turn   = 0;
int32_t n3ds_touch_speed  = 4;
int32_t n3ds_run_mode     = N3DS_RUN_HOLD;
int32_t n3ds_gpu_wide     = 1;

int (*n3ds_draw_bottom)(uint8_t *screen);
int (*n3ds_in_game)(void);
void (*n3ds_save_on_suspend)(void);

uint8_t n3ds_ui_black, n3ds_ui_dark, n3ds_ui_gray, n3ds_ui_white;

// newlib for the 3DS has memalign but not posix_memalign.
extern "C" int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    void *p = memalign(alignment, size);

    if (p == NULL)
        return ENOMEM;

    *memptr = p;
    return 0;
}

// Each folder's names, in lower case.
static std::unordered_map<std::string, std::unordered_set<std::string>> listings;

static void ForgetListings(void)
{
    listings.clear();
}

static std::string LowerCase(const char *text)
{
    std::string lower(text);

    for (auto & c : lower)
        c = tolower((unsigned char) c);

    return lower;
}

int n3ds_exists(const char *path)
{
    char const *slash = strrchr(path, '/');
    char const *name = slash ? slash + 1 : path;

    // A folder named with its slash, or "." and "..": asked directly.
    if (name[0] == '\0' || !strcmp(name, ".") || !strcmp(name, ".."))
    {
        struct stat st;
        return !stat(path, &st);
    }

    std::string folder = slash == NULL ? "." : slash == path ? "/" : std::string(path, slash - path);

    if (folder.back() == ':')
        folder += '/';  // the root of a device, "sdmc:/"
    auto found = listings.find(folder);

    if (found == listings.end())
    {
        std::unordered_set<std::string> names;

        if (DIR *dir = opendir(folder.c_str()))
        {
            while (struct dirent *entry = readdir(dir))
                names.insert(LowerCase(entry->d_name));

            closedir(dir);
        }

        found = listings.emplace(folder, std::move(names)).first;
    }

    return found->second.count(LowerCase(name)) != 0;
}

int n3ds_unlink(const char *path)
{
    ForgetListings();
    return unlink(path);
}

int n3ds_mkdir(const char *path, int mode)
{
    ForgetListings();
    return mkdir(path, mode);
}

static FILE *rewrite_files[8];

FILE *n3ds_fopen_rewrite(const char *name)
{
    FILE *file = fopen(name, "r+b");

    if (file == NULL)
    {
        ForgetListings();
        return fopen(name, "wb");
    }

    for (auto &f : rewrite_files)
    {
        if (f == NULL)
        {
            f = file;
            break;
        }
    }

    return file;
}

int n3ds_fclose(FILE *file)
{
    for (auto &f : rewrite_files)
    {
        if (f == file && file != NULL)
        {
            fflush(file);
            ftruncate(fileno(file), ftell(file));
            f = NULL;
            break;
        }
    }

    return fclose(file);
}

int n3ds_is_new3ds(void)
{
    static int is_new = -1;

    if (is_new < 0)
    {
        bool result = false;
        APT_CheckNew3DS(&result);
        is_new = result;
    }

    return is_new;
}

int n3ds_is_closing(void)
{
    return aptShouldClose();
}

// ==========================================================================
// HOME Menu, sleep mode and the bottom screen's backlight.
//
// The gsp::Lcd session is opened only around each change: keeping it open
// froze the console at the HOME Menu (in Wolfenstein), which needs it.

static aptHookCookie apt_hook;
static bool config_saved;
static bool bottom_on = true;

int n3ds_config_already_saved(void)
{
    return config_saved && n3ds_is_closing();
}

static void SetBottomBacklight(bool on)
{
    if (R_FAILED(gspLcdInit()))
        return;

    if (on)
        GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM);
    else
        GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);

    gspLcdExit();
}

// Never leave the HOME Menu or sleep mode with the bottom screen off. The
// configuration is saved when the HOME Menu opens.
static void AptHook(APT_HookType hook, void *)
{
    switch (hook)
    {
        case APTHOOK_ONSUSPEND:
            if (n3ds_save_on_suspend != NULL)
            {
                n3ds_save_on_suspend();
                config_saved = true;
            }
            fallthrough__;
        case APTHOOK_ONSLEEP:
        case APTHOOK_ONEXIT:
            if (!bottom_on)
                SetBottomBacklight(true);
            break;

        case APTHOOK_ONRESTORE:
            config_saved = false;
            fallthrough__;
        case APTHOOK_ONWAKEUP:
            if (!bottom_on)
                SetBottomBacklight(false);
            break;

        default:
            break;
    }
}

static void RestoreBottomBacklight(void)
{
    if (!bottom_on)
        SetBottomBacklight(true);
}

// The memory, split before main between the heap (the game's) and the
// linear heap (the GPU's: textures, vertices, the screens). libctru gives
// the linear heap 32 MB; the GPU renderer used 7 in a level (its textures
// are let go when short), and the heap ran out first: with 96 MB or more
// (the New 3DS), 20 MB for the linear heap. With less, half and half, at
// most 32 MB linear.
#define N3DS_LINEAR_SMALL (20u << 20)
#define N3DS_LINEAR_DEFAULT (32u << 20)
#define N3DS_SMALL_LINEAR_FROM (96u << 20)

extern "C" char *fake_heap_start, *fake_heap_end;
extern "C" u32 __ctru_heap, __ctru_heap_size, __ctru_linear_heap, __ctru_linear_heap_size;

extern "C" void __system_allocateHeaps(void)
{
    Handle reslimit = 0;
    ResourceLimitType type = RESLIMIT_COMMIT;
    s64 max_commit = 0, current_commit = 0;

    if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE)))
        svcBreak(USERBREAK_PANIC);

    svcGetResourceLimitLimitValues(&max_commit, reslimit, &type, 1);
    svcGetResourceLimitCurrentValues(&current_commit, reslimit, &type, 1);
    svcCloseHandle(reslimit);

    u32 const remaining = (u32) (max_commit - current_commit) & ~0xFFF;
    u32 const heap_max = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;

    if (remaining >= N3DS_SMALL_LINEAR_FROM)
        __ctru_linear_heap_size = N3DS_LINEAR_SMALL;
    else
        __ctru_linear_heap_size = min(remaining / 2, N3DS_LINEAR_DEFAULT) & ~0xFFF;

    __ctru_heap_size = remaining - __ctru_linear_heap_size;

    // The heap's addresses end at OS_HEAP_AREA_END: the rest goes linear.
    if (__ctru_heap_size > heap_max)
    {
        __ctru_heap_size = heap_max;
        __ctru_linear_heap_size = remaining - heap_max;
    }

    if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size,
                                  MEMOP_ALLOC, (MemPerm) (MEMPERM_READ | MEMPERM_WRITE))))
        svcBreak(USERBREAK_PANIC);

    if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size,
                                  MEMOP_ALLOC_LINEAR, (MemPerm) (MEMPERM_READ | MEMPERM_WRITE))))
        svcBreak(USERBREAK_PANIC);

    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);

    fake_heap_start = (char *) __ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}

void n3ds_open(void)
{
    // New 3DS: 804 MHz and the L2 cache.
    osSetSpeedupEnable(true);

    mkdir("sdmc:/3ds", 0777);
    mkdir(N3DS_GAME_DIR, 0777);
    chdir(N3DS_GAME_DIR);

    aptHook(&apt_hook, AptHook, NULL);
    atexit(RestoreBottomBacklight);

    // The touch screen is read here (buttons, keyboard, touch turning): as
    // a mouse, a tap would fire.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
}

// A tap turns the bottom screen off or on, except while it shows anything
// (or turns the view); when something appears it turns on by itself.
static void UpdateBottomBacklight(bool showing)
{
    static bool was_touching = false;
    bool const touching = (hidKeysHeld() & KEY_TOUCH) != 0;
    bool const turning = n3ds_touch_turn && n3ds_in_game != NULL && n3ds_in_game();

    if (touching && !was_touching && (!bottom_on || !(showing || turning)))
    {
        bottom_on = !bottom_on;
        SetBottomBacklight(bottom_on);
    }
    else if (showing && !bottom_on)
    {
        bottom_on = true;
        SetBottomBacklight(true);
    }

    was_touching = touching;
}

// ==========================================================================
// The touch screen.

int n3ds_get_touch(int *x, int *y)
{
    touchPosition touch;

    if ((hidKeysHeld() & KEY_TOUCH) == 0)
        return 0;

    hidTouchRead(&touch);
    *x = touch.px;
    *y = touch.py;
    return 1;
}

static struct { int x, y, w, h; } reserved[8];
static int num_reserved;
static bool keyboard_open;

void n3ds_clear_reserved_touch(void)
{
    num_reserved = 0;
}

void n3ds_reserve_touch(int x, int y, int w, int h)
{
    if (num_reserved < (int) ARRAY_SIZE(reserved))
    {
        reserved[num_reserved].x = x;
        reserved[num_reserved].y = y;
        reserved[num_reserved].w = w;
        reserved[num_reserved].h = h;
        ++num_reserved;
    }
}

int n3ds_touch_reserved(int x, int y)
{
    if (keyboard_open)
        return 1;

    for (int i = 0; i < num_reserved; ++i)
    {
        if (x >= reserved[i].x && x < reserved[i].x + reserved[i].w
         && y >= reserved[i].y && y < reserved[i].y + reserved[i].h)
            return 1;
    }

    return 0;
}

void n3ds_touch_drag(int *dx, int *dy)
{
    static bool dragging;
    static int last_x, last_y;
    int x, y;

    *dx = *dy = 0;

    if (!n3ds_get_touch(&x, &y))
    {
        dragging = false;
        return;
    }

    if (!dragging)
    {
        // Only a drag that starts away from the buttons.
        if (n3ds_touch_reserved(x, y))
            return;

        dragging = true;
    }
    else
    {
        *dx = x - last_x;
        *dy = y - last_y;
    }

    last_x = x;
    last_y = y;
}

char const *const n3ds_button_names[N3DS_NUM_BUTTONS] = { "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "SELECT" };

unsigned n3ds_buttons_held(void)
{
    static u32 const keys[N3DS_NUM_BUTTONS] = { KEY_A, KEY_B, KEY_X, KEY_Y, KEY_L, KEY_R, KEY_ZL, KEY_ZR, KEY_SELECT };
    u32 const held = hidKeysHeld();
    unsigned buttons = (held & KEY_START) ? N3DS_START_HELD : 0;

    for (int i = 0; i < N3DS_NUM_BUTTONS; ++i)
        if (held & keys[i])
            buttons |= 1u << i;

    return buttons;
}

void n3ds_post_key(int scancode, int down)
{
    SDL_Event ev;

    Bmemset(&ev, 0, sizeof(ev));
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    ev.key.keysym.scancode = (SDL_Scancode) scancode;
    ev.key.keysym.sym = SDL_GetKeyFromScancode((SDL_Scancode) scancode);
    SDL_PushEvent(&ev);
}

// ==========================================================================
// Drawing on the bottom screen (8-bit, 320x240, row by row).

void n3ds_fill(uint8_t *screen, int x, int y, int w, int h, int color)
{
    int const x0 = max(x, 0), x1 = min(x + w, N3DS_BOTTOM_WIDTH);
    int const y0 = max(y, 0), y1 = min(y + h, N3DS_BOTTOM_HEIGHT);

    for (int i = y0; i < y1; ++i)
        if (x1 > x0)
            Bmemset(screen + i * N3DS_BOTTOM_WIDTH + x0, color, x1 - x0);
}

// 5x7 letters: the rows' 5 low bits, the leftmost pixel the highest.
static const struct { char ch; uint8_t rows[7]; } font[] =
{
    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, {'3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
    {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'A', {0x0E,0x11,0x11,0x11,0x1F,0x11,0x11}}, {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D', {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N', {0x11,0x11,0x19,0x15,0x13,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}}, {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}}, {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}}, {'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
};

static void DrawChar(uint8_t *screen, int x, int y, char ch, int scale, int color)
{
    for (auto const &f : font)
    {
        if (f.ch != ch)
            continue;

        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (f.rows[row] & (0x10 >> col))
                    n3ds_fill(screen, x + col * scale, y + row * scale, scale, scale, color);
        return;
    }
}

void n3ds_draw_text(uint8_t *screen, int x, int y, const char *text, int scale, int color)
{
    for (; *text != '\0'; ++text, x += 6 * scale)
        DrawChar(screen, x, y, *text, scale, color);
}

// ==========================================================================
// The map's buttons: the keyboard at the top left, "+" above "-" at the
// bottom left. A tap zooms by a quarter; holding a button zooms on.

#define BUTTON_SIZE 24
#define BUTTON_X 4
#define MIN_ZOOM 25
#define MAX_ZOOM 800

static void MapButtonRows(int map_y, int map_height, int *plus_y, int *minus_y)
{
    *minus_y = map_y + map_height - BUTTON_SIZE - 4;
    *plus_y = *minus_y - BUTTON_SIZE - 4;
}

int n3ds_update_map_buttons(int map_y, int map_height)
{
    static int held = 0;
    static bool was_touching = false;
    int plus_y, minus_y, x, y, direction = 0;
    bool const touching = n3ds_get_touch(&x, &y);

    MapButtonRows(map_y, map_height, &plus_y, &minus_y);
    n3ds_reserve_touch(0, map_y, BUTTON_X + BUTTON_SIZE + 8, map_height);

    if (touching && !was_touching && x < BUTTON_X + BUTTON_SIZE + 8
        && y >= map_y && y < map_y + BUTTON_X + BUTTON_SIZE + 4)
        n3ds_show_keyboard(1);

    was_touching = touching;

    if (touching && x < BUTTON_X + BUTTON_SIZE + 8)
    {
        if (y >= plus_y - 4 && y < plus_y + BUTTON_SIZE + 2)
            direction = 1;
        else if (y >= minus_y - 2 && y < minus_y + BUTTON_SIZE + 4)
            direction = -1;
    }

    if (direction == 0)
        held = 0;
    else
    {
        if (held == 0 || held >= 10)
        {
            int const step = held == 0 ? 125 : 104;

            if (direction > 0)
                n3ds_map_zoom = n3ds_map_zoom * step / 100;
            else
                n3ds_map_zoom = n3ds_map_zoom * 100 / step;
        }

        ++held;
    }

    n3ds_map_zoom = clamp(n3ds_map_zoom, MIN_ZOOM, MAX_ZOOM);
    return n3ds_map_zoom;
}

static void DrawMapButton(uint8_t *screen, int y, bool plus)
{
    int const center = BUTTON_SIZE / 2;

    n3ds_fill(screen, BUTTON_X, y, BUTTON_SIZE, BUTTON_SIZE, n3ds_ui_gray);
    n3ds_fill(screen, BUTTON_X + 2, y + 2, BUTTON_SIZE - 4, BUTTON_SIZE - 4, n3ds_ui_black);
    n3ds_fill(screen, BUTTON_X + 5, y + center - 1, BUTTON_SIZE - 10, 2, n3ds_ui_white);

    if (plus)
        n3ds_fill(screen, BUTTON_X + center - 1, y + 5, 2, BUTTON_SIZE - 10, n3ds_ui_white);
}

// A small keyboard: three rows of keys and the space bar.
static void DrawKeyboardButton(uint8_t *screen, int y)
{
    n3ds_fill(screen, BUTTON_X, y, BUTTON_SIZE, BUTTON_SIZE, n3ds_ui_gray);
    n3ds_fill(screen, BUTTON_X + 2, y + 2, BUTTON_SIZE - 4, BUTTON_SIZE - 4, n3ds_ui_black);

    for (int row = 0; row < 3; ++row)
        for (int key = 0; key < 4 - (row == 2); ++key)
            n3ds_fill(screen, BUTTON_X + 5 + row + key * 4, y + 5 + row * 4, 2, 2, n3ds_ui_white);

    n3ds_fill(screen, BUTTON_X + 8, y + 17, 8, 2, n3ds_ui_white);
}

void n3ds_draw_map_buttons(uint8_t *screen, int map_y, int map_height)
{
    int plus_y, minus_y;

    MapButtonRows(map_y, map_height, &plus_y, &minus_y);
    DrawKeyboardButton(screen, map_y + 4);
    DrawMapButton(screen, plus_y, true);
    DrawMapButton(screen, minus_y, false);
}

// ==========================================================================
// The weapon buttons, as the PC's number keys.

void n3ds_draw_weapon_buttons(uint8_t *screen, int y, int slots, unsigned owned, int in_hand)
{
    int const pitch = N3DS_BOTTOM_WIDTH / slots;

    for (int slot = 0; slot < slots; ++slot)
    {
        int const x = slot * pitch + 2;
        int const w = pitch - 4;
        char const digit[2] = { (char) (slot == 9 ? '0' : '1' + slot), '\0' };

        n3ds_fill(screen, x, y + 2, w, N3DS_WEAPON_BAR_HEIGHT - 4, n3ds_ui_gray);
        n3ds_fill(screen, x + 2, y + 4, w - 4, N3DS_WEAPON_BAR_HEIGHT - 8,
                  slot == in_hand ? n3ds_ui_gray : n3ds_ui_black);
        n3ds_draw_text(screen, x + (w - 10) / 2, y + (N3DS_WEAPON_BAR_HEIGHT - 14) / 2, digit, 2,
                       slot == in_hand ? n3ds_ui_black : (owned & (1u << slot)) ? n3ds_ui_white : n3ds_ui_dark);
    }
}

void n3ds_update_weapon_buttons(int y, int slots)
{
    static int key_down = 0;
    int x, ty;
    bool const touching = n3ds_get_touch(&x, &ty);

    n3ds_reserve_touch(0, y, N3DS_BOTTOM_WIDTH, N3DS_WEAPON_BAR_HEIGHT);

    if (key_down != 0 && !touching)
    {
        n3ds_post_key(key_down, 0);
        key_down = 0;
    }

    if (key_down == 0 && touching && ty >= y && ty < y + N3DS_WEAPON_BAR_HEIGHT && x >= 0 && x < N3DS_BOTTOM_WIDTH)
    {
        int const slot = x * slots / N3DS_BOTTOM_WIDTH;

        // SDL's scancodes: 1 to 9, then 0.
        key_down = SDL_SCANCODE_1 + slot;
        n3ds_post_key(key_down, 1);
    }
}

// ==========================================================================
// The keyboard on the bottom screen (the system's keyboard is an applet
// that stops the game). Each key reaches the game as a key press.

#define KEY_W 28
#define KEY_H 30
#define KEY_PITCH_X 31
#define KEY_PITCH_Y 34
#define KEYS_X 6
#define KEYS_Y 50

struct vkey_t
{
    int x, y, w;        // in keys
    int scancode;       // the key it presses, or 0 to close
    const char *label;
};

static vkey_t vkeys[64];
static int num_vkeys;

static void AddKey(int x, int y, int w, int scancode, const char *label)
{
    vkeys[num_vkeys++] = { x, y, w, scancode, label };
}

static int CharScancode(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return SDL_SCANCODE_A + ch - 'A';
    if (ch >= '1' && ch <= '9')
        return SDL_SCANCODE_1 + ch - '1';

    switch (ch)
    {
        case '0': return SDL_SCANCODE_0;
        case '-': return SDL_SCANCODE_MINUS;
        case '.': return SDL_SCANCODE_PERIOD;
        default:  return 0;
    }
}

static void SetUpKeys(void)
{
    static const char *const rows[] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM." };
    static char labels[40][2];
    int n = 0;

    for (int row = 0; row < (int) ARRAY_SIZE(rows); ++row)
    {
        for (int i = 0; rows[row][i] != '\0'; ++i, ++n)
        {
            labels[n][0] = rows[row][i];
            labels[n][1] = '\0';
            AddKey(i, row, 1, CharScancode(rows[row][i]), labels[n]);
        }
    }

    AddKey(8, 3, 2, SDL_SCANCODE_BACKSPACE, "DEL");
    AddKey(0, 4, 3, 0, "CLOSE");
    AddKey(3, 4, 4, SDL_SCANCODE_SPACE, "SPACE");
    AddKey(7, 4, 3, SDL_SCANCODE_RETURN, "ENTER");
}

static void KeyRect(const vkey_t *k, int *x, int *y, int *w)
{
    *x = KEYS_X + k->x * KEY_PITCH_X;
    *y = KEYS_Y + k->y * KEY_PITCH_Y;
    *w = k->w * KEY_PITCH_X - (KEY_PITCH_X - KEY_W);
}

static void DrawKeyboard(uint8_t *screen, int pressed)
{
    int x, y, w;

    Bmemset(screen, n3ds_ui_black, N3DS_BOTTOM_WIDTH * N3DS_BOTTOM_HEIGHT);
    n3ds_draw_text(screen, 10, 14, "KEYBOARD", 2, n3ds_ui_gray);

    for (int i = 0; i < num_vkeys; ++i)
    {
        const vkey_t *k = &vkeys[i];
        int const len = (int) strlen(k->label);
        int const scale = len == 1 ? 2 : 1;

        KeyRect(k, &x, &y, &w);
        n3ds_fill(screen, x, y, w, KEY_H, i == pressed ? n3ds_ui_white : n3ds_ui_gray);
        n3ds_fill(screen, x + 1, y + 1, w - 2, KEY_H - 2, i == pressed ? n3ds_ui_gray : n3ds_ui_dark);
        n3ds_draw_text(screen, x + (w - len * 6 * scale + scale) / 2, y + (KEY_H - 7 * scale) / 2, k->label, scale,
                       n3ds_ui_white);
    }
}

// Taps on the keyboard, every frame (so none is missed). Returns the key
// being pressed, or -1.
// A key is held while touched, as on a PC keyboard: Duke's cheat codes
// start when it sees D and N held, and a press and release sent together
// were never seen held.
static int held_key;  // its scancode, or 0

static void ReleaseKey(void)
{
    if (held_key != 0)
        n3ds_post_key(held_key, 0);

    held_key = 0;
}

static int UpdateKeyboard(void)
{
    static bool was_touching = false;
    static int pressed = -1;
    int x, y, kx, ky, kw;
    bool const touching = n3ds_get_touch(&x, &y);

    if (touching && !was_touching)
    {
        pressed = -1;

        for (int i = 0; i < num_vkeys; ++i)
        {
            KeyRect(&vkeys[i], &kx, &ky, &kw);

            if (x >= kx && x < kx + kw && y >= ky && y < ky + KEY_H)
            {
                pressed = i;

                if (vkeys[i].scancode == 0)
                    keyboard_open = false;
                else
                {
                    held_key = vkeys[i].scancode;
                    n3ds_post_key(held_key, 1);
                }
                break;
            }
        }
    }
    else if (!touching && was_touching)
    {
        ReleaseKey();
        pressed = -1;
    }

    was_touching = touching;
    return pressed;
}

void n3ds_show_keyboard(int show)
{
    if (!show)
        ReleaseKey();

    keyboard_open = show;
}

int n3ds_keyboard_open(void)
{
    return keyboard_open;
}

// ==========================================================================
// Frame counter: frames per second (white), milliseconds of the game per
// frame (green) and of composing the screens (yellow), each second.

static int counter_fps, counter_work_ms, counter_compose_ms;

static void CountFrame(int work_us, int compose_us)
{
    static int frames;
    static u64 start, work_total, compose_total;
    u64 const now = osGetTime();

    ++frames;
    work_total += work_us;
    compose_total += compose_us;

    if (now - start >= 1000)
    {
        counter_fps = (int) (frames * 1000 / (now - start));
        counter_work_ms = (int) ((work_total / frames + 500) / 1000);
        counter_compose_ms = (int) ((compose_total / frames + 500) / 1000);
        frames = 0;
        work_total = compose_total = 0;
        start = now;
    }
}

static void FillBottom(u32 *framebuffer, int x, int y, int w, int h, u32 color)
{
    for (int i = x; i < x + w; ++i)
        for (int j = y; j < y + h; ++j)
            framebuffer[i * TOP_HEIGHT + (TOP_HEIGHT - 1 - j)] = color;
}

// 3x5 digits.
static void DrawNumber(u32 *framebuffer, int number, int x, int y, int scale, u32 color)
{
    static const uint8_t digits[10][5] =
    {
        {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
        {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
    };
    char text[12];

    snprintf(text, sizeof(text), "%d", number);

    for (int i = 0; text[i] != '\0'; ++i)
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 3; ++col)
                if (digits[text[i] - '0'][row] & (4 >> col))
                    FillBottom(framebuffer, x + (i * 4 + col) * scale, y + row * scale, scale, scale, color);
}

// At the top right of the bottom screen.
static void DrawFrameCounter(u32 *framebuffer)
{
    int const y = 2;

    FillBottom(framebuffer, N3DS_BOTTOM_WIDTH - 94, y, 92, 14, 0x000000FF);
    DrawNumber(framebuffer, counter_fps, N3DS_BOTTOM_WIDTH - 91, y + 2, 2, 0xFFFFFFFF);
    DrawNumber(framebuffer, counter_work_ms, N3DS_BOTTOM_WIDTH - 61, y + 2, 2, 0x80FF80FF);
    DrawNumber(framebuffer, counter_compose_ms, N3DS_BOTTOM_WIDTH - 31, y + 2, 2, 0xFFE060FF);
}

// ==========================================================================
// Presenting. The game's thread hands each frame (a copy) to a thread on
// another core, which composes both screens, swaps them and waits for the
// vertical blank, while the game makes the next frame. The game waits only
// if that thread has not finished the previous one, so the vertical blank
// still sets the pace.

struct frame_t
{
    bool has_top;           // otherwise the GPU shows the top screen
    uint8_t top[TOP_WIDTH * TOP_HEIGHT];
    int width, height;
    u32 colors[256];        // palette in the framebuffer format (RGBA8)
    bool bottom;            // the bottom screen is updated (every other frame)
    bool bottom_showing;    // and bottom_screen has something
    uint8_t bottom_screen[N3DS_BOTTOM_WIDTH * N3DS_BOTTOM_HEIGHT];
};

static frame_t frame;
static LightEvent frame_ready, frame_done;
static Thread present_thread;
static u64 compose_ticks;
static volatile uint32_t frames_presented;

const char *volatile n3ds_where = "start";
volatile uint32_t n3ds_audio_mixes;

// The top screen (400x240): the frame centered, its rows stretched or
// squeezed to 240 if it has another height. The framebuffer is rotated:
// screen pixel (x, y) is at [x * 240 + (239 - y)]. Copied 8 columns at a
// time, so the writes of each source row stay in a few cache lines.
static void ComposeTop(const frame_t *f)
{
    u32 *framebuffer = (u32 *) gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    int const width = min(f->width, TOP_WIDTH);
    int const left = (TOP_WIDTH - width) / 2;

    if (left > 0)
    {
        Bmemset(framebuffer, 0, left * TOP_HEIGHT * sizeof(u32));
        Bmemset(framebuffer + (left + width) * TOP_HEIGHT, 0, (TOP_WIDTH - left - width) * TOP_HEIGHT * sizeof(u32));
    }

    for (int x = 0; x < width; x += 8)
    {
        int const columns = min(width - x, 8);
        u32 *column = framebuffer + (left + x) * TOP_HEIGHT + (TOP_HEIGHT - 1);

        for (int y = 0; y < TOP_HEIGHT; ++y)
        {
            int const row = f->height == TOP_HEIGHT ? y : y * f->height / TOP_HEIGHT;
            const uint8_t *source = f->top + row * f->width + x;

            for (int k = 0; k < columns; ++k)
                column[k * TOP_HEIGHT - y] = f->colors[source[k]];
        }
    }

    GSPGPU_FlushDataCache(framebuffer, TOP_WIDTH * TOP_HEIGHT * sizeof(u32));
    gfxScreenSwapBuffers(GFX_TOP, false);
}

static void ComposeBottom(const frame_t *f)
{
    // Whether each of the two buffers has anything but black.
    static bool drawn[2] = { true, true };
    static int buffer;
    u32 *framebuffer = (u32 *) gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);

    if (f->bottom_showing)
    {
        for (int x = 0; x < N3DS_BOTTOM_WIDTH; x += 8)
        {
            u32 *column = framebuffer + x * TOP_HEIGHT + (TOP_HEIGHT - 1);

            for (int y = 0; y < N3DS_BOTTOM_HEIGHT; ++y)
            {
                const uint8_t *source = f->bottom_screen + y * N3DS_BOTTOM_WIDTH + x;

                for (int k = 0; k < 8; ++k)
                    column[k * TOP_HEIGHT - y] = f->colors[source[k]];
            }
        }

        drawn[buffer] = true;
    }
    else if (drawn[buffer])
    {
        Bmemset(framebuffer, 0, N3DS_BOTTOM_WIDTH * TOP_HEIGHT * sizeof(u32));
        drawn[buffer] = false;
    }
    else if (!n3ds_show_fps)
        return;  // already black

    if (n3ds_show_fps)
    {
        DrawFrameCounter(framebuffer);
        drawn[buffer] = true;
    }

    GSPGPU_FlushDataCache(framebuffer, N3DS_BOTTOM_WIDTH * TOP_HEIGHT * sizeof(u32));
    gfxScreenSwapBuffers(GFX_BOTTOM, false);
    buffer ^= 1;
}

static void ShowFrame(void)
{
    u64 const start = svcGetSystemTick();

    if (frame.has_top)
        ComposeTop(&frame);

    if (frame.bottom)
        ComposeBottom(&frame);

    compose_ticks = svcGetSystemTick() - start;

    // In GPU mode citro3d waits for it.
    if (frame.has_top)
        gspWaitForVBlank();
}

// No frame for a few seconds: where the game's thread is, in the log.
static void LogStall(int seconds)
{
    static uint32_t last_mixes;
    uint32_t const mixes = n3ds_audio_mixes;

    LOG_F(WARNING, "3DS: no frame for %d s (frame %u): the game is at \"%s\"; audio mixed %u times since; ndsp resyncs %u",
          seconds, (unsigned) frames_presented, n3ds_where, (unsigned) (mixes - last_mixes), (unsigned) ndspChnListResyncs);
    last_mixes = mixes;
}

static void PresentThread(void *arg)
{
    LOG_F(INFO, "3DS: presenting on core %d; heap %08lx-%08lx, linear heap %08lx-%08lx, stack near %p", (int) svcGetProcessorID(),
          (unsigned long) __ctru_heap, (unsigned long) (__ctru_heap + __ctru_heap_size),
          (unsigned long) __ctru_linear_heap, (unsigned long) (__ctru_linear_heap + __ctru_linear_heap_size), (void *) &arg);

    for (;;)
    {
        int waited = 0;

        while (LightEvent_WaitTimeout(&frame_ready, 1000 * 1000 * 1000))
        {
            ++waited;
            if (waited == 3 || (waited > 3 && waited % 10 == 0))
                LogStall(waited);
        }

        ++frames_presented;

        // Closed from the HOME Menu: the screens are no longer ours.
        if (!n3ds_is_closing())
            ShowFrame();

        LightEvent_Signal(&frame_done);
    }
}

static void StopPresenting(void);

// newlib's abort (a failed assert, an uncaught exception, new without
// memory) ends the program without a word: back in the Homebrew Launcher.
// These say why at the end of the log (written directly: the failure may
// be inside the logging, and logging from here failed again and again
// until the stack ran out), and stop with Luma's crash screen.
static volatile int aborting;

static __attribute__((noreturn)) void Stop(const char *text)
{
    if (aborting++ == 0)
    {
        if (FILE *log = fopen(N3DS_GAME_DIR "/" APPBASENAME ".log", "a"))
        {
            fputs(text, log);
            fclose(log);
        }
    }

    svcBreak(USERBREAK_PANIC);
    for (;;) { }
}

extern "C" __attribute__((noreturn)) void abort(void)
{
    char text[256];

    if (aborting)
        Stop("");

    snprintf(text, sizeof(text), "3DS: abort, called from %p; the game was at \"%s\"\n", __builtin_return_address(0), n3ds_where);
    Stop(text);
}

extern "C" __attribute__((noreturn)) void __assert_func(const char *file, int line, const char *function, const char *expression)
{
    char text[512];

    if (aborting)
        Stop("");

    snprintf(text, sizeof(text), "3DS: assertion \"%s\" failed at %s:%d (%s), called from %p; the game was at \"%s\"\n",
             expression, file, line, function ? function : "?", __builtin_return_address(0), n3ds_where);
    Stop(text);
}

// Leaving any other way: where the game was (with the log still open).
// Straight to the file: at exit the engine's console, which the log also
// goes to, is gone (logging there crashed).
static void LogExit(void)
{
    if (FILE *log = fopen(N3DS_GAME_DIR "/" APPBASENAME ".log", "a"))
    {
        fprintf(log, "3DS: exit; the game was at \"%s\"\n", n3ds_where);
        fclose(log);
    }
}

// On the system core (1), which the game may use up to 80% of the time.
// Not on the Old 3DS: its system core already has the audio thread.
static void StartPresenting(void)
{
    s32 priority = 0x30;
    Result result = 0;
    int limit;

    LightEvent_Init(&frame_ready, RESET_ONESHOT);
    LightEvent_Init(&frame_done, RESET_STICKY);
    LightEvent_Signal(&frame_done);

    if (!n3ds_is_new3ds())
    {
        LOG_F(INFO, "3DS: composing the screens on the game's core (Old 3DS)");
        return;
    }

    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);

    // The .3dsx gets 80%; a .cia only up to what its exheader allows.
    for (limit = 80; limit > 0; limit -= 10)
    {
        result = APT_SetAppCpuTimeLimit(limit);
        if (R_SUCCEEDED(result))
            break;
    }

    present_thread = threadCreate(PresentThread, NULL, 32 * 1024, priority, 1, true);

    if (present_thread == NULL)
        present_thread = threadCreate(PresentThread, NULL, 32 * 1024, priority, 2, true);

    if (present_thread != NULL)
        atexit(StopPresenting);

    atexit(LogExit);

    LOG_F(INFO, "3DS: composing the screens %s (core 1 time limit: %d%%, %08lx)",
          present_thread != NULL ? "on another core" : "on the game's core", limit, (unsigned long) result);
}

// Before the screens close at exit: the composing thread may be busy.
static void StopPresenting(void)
{
    if (present_thread != NULL)
        LightEvent_WaitTimeout(&frame_done, 100 * 1000 * 1000);
}

static int Nearest(const uint8_t *palette, int r, int g, int b)
{
    int best = 0, best_distance = INT32_MAX;

    for (int i = 0; i < 256; ++i)
    {
        int const dr = palette[i * 4] - r, dg = palette[i * 4 + 1] - g, db = palette[i * 4 + 2] - b;
        int const distance = dr * dr + dg * dg + db * db;

        if (distance < best_distance)
        {
            best = i;
            best_distance = distance;
        }
    }

    return best;
}

// Hands a frame to the other core: the top screen's (buffer, unless NULL:
// the GPU shows it) and every other one the bottom screen's. work_us and
// gpu_us, when the GPU drew: for the frame counter.
static void Present(const uint8_t *buffer, int width, int height, const uint8_t *palette, int work_us, int gpu_us)
{
    static bool started;
    static int bottom_frames;
    static bool showing;
    static u64 work_start;
    static uint8_t last_palette[256 * 4];

    // Closed from the HOME Menu: the screens are no longer ours.
    if (n3ds_is_closing())
        return;

    if (!started)
    {
        StartPresenting();
        started = true;

        // Whether a pthread key keeps what is set (loguru relied on it).
        pthread_key_t key;
        int const created = pthread_key_create(&key, NULL);
        int const set = created == 0 ? pthread_setspecific(key, &started) : -1;
        LOG_F(INFO, "3DS: pthread keys: create %d, set %d, kept %d", created, set,
              created == 0 && pthread_getspecific(key) == &started);
    }

    // The memory in use, once a minute (it ran out after ten minutes once).
    static unsigned frames;

    if (++frames % 3600 == 0)
    {
        struct mallinfo const info = mallinfo();
        LOG_F(INFO, "3DS: memory in use %d KB, free in the heap %d KB", info.uordblks >> 10, info.fordblks >> 10);
    }

    u64 const work_end = svcGetSystemTick();
    bool const gpu = buffer == NULL;

    // Wait until the previous frame is on its way to the screen.
    if (present_thread != NULL)
    {
        N3DS_WHERE("present: waiting for the previous frame");
        LightEvent_Wait(&frame_done);
        LightEvent_Clear(&frame_done);

        if (gpu)
            CountFrame(work_us, gpu_us);
        else
            CountFrame((int) ((work_end - work_start) / CPU_TICKS_PER_USEC), (int) (compose_ticks / CPU_TICKS_PER_USEC));

        work_start = svcGetSystemTick();
    }

    // The other thread is idle: the frame is free to fill.
    frame.has_top = !gpu;

    if (!gpu)
    {
        width = min(width, TOP_WIDTH);
        height = min(height, TOP_HEIGHT);
        Bmemcpy(frame.top, buffer, width * height);
        frame.width = width;
        frame.height = height;
    }

    if (Bmemcmp(last_palette, palette, sizeof(last_palette)))
    {
        Bmemcpy(last_palette, palette, sizeof(last_palette));

        for (int k = 0; k < 256; ++k)
            frame.colors[k] = ((u32) palette[k * 4] << 24) | ((u32) palette[k * 4 + 1] << 16) | ((u32) palette[k * 4 + 2] << 8) | 0xFF;

        n3ds_ui_black = Nearest(palette, 0, 0, 0);
        n3ds_ui_dark  = Nearest(palette, 48, 48, 48);
        n3ds_ui_gray  = Nearest(palette, 128, 128, 128);
        n3ds_ui_white = Nearest(palette, 255, 255, 255);
    }

    // The bottom screen, 30 times per second: drawn here, with the game.
    // The keyboard instead, when open; its taps are read every frame.
    frame.bottom = (++bottom_frames & 1) == 0;

    if (frame.bottom)
        n3ds_clear_reserved_touch();

    if (keyboard_open)
    {
        if (num_vkeys == 0)
            SetUpKeys();

        int const pressed = UpdateKeyboard();

        if (frame.bottom && keyboard_open)
        {
            DrawKeyboard(frame.bottom_screen, pressed);
            showing = frame.bottom_showing = true;
        }
    }

    if (frame.bottom && !keyboard_open)
    {
        N3DS_WHERE("present: bottom screen");
        showing = n3ds_draw_bottom != NULL && n3ds_draw_bottom(frame.bottom_screen);
        frame.bottom_showing = showing;
    }

    N3DS_WHERE("present: backlight");
    UpdateBottomBacklight(showing);
    N3DS_WHERE("present: done");

    if (present_thread != NULL)
        LightEvent_Signal(&frame_ready);
    else
    {
        if (!gpu || frame.bottom)
            ShowFrame();

        if (gpu)
            CountFrame(work_us, gpu_us);
        else
            CountFrame((int) ((work_end - work_start + compose_ticks) / CPU_TICKS_PER_USEC),
                       (int) (compose_ticks / CPU_TICKS_PER_USEC));

        work_start = svcGetSystemTick();
    }
}

void n3ds_present(const uint8_t *buffer, int width, int height, const uint8_t *palette)
{
    Present(buffer, width, height, palette, 0, 0);
}

void n3ds_present_bottom(const uint8_t *palette, int work_us, int gpu_us)
{
    Present(NULL, 0, 0, palette, work_us, gpu_us);
}

int n3ds_wide_supported(void)
{
    static int supported = -1;

    if (supported < 0)
    {
        u8 model = CFG_MODEL_3DS;

        if (R_SUCCEEDED(cfguInit()))
        {
            CFGU_GetSystemModel(&model);
            cfguExit();
        }

        supported = model != CFG_MODEL_2DS;
    }

    return supported;
}

void n3ds_release_top(void)
{
    if (present_thread != NULL)
    {
        LightEvent_Wait(&frame_done);
        frame.has_top = false;
    }
}

// pgl's messages (printf in pgl.c), in the log.
extern "C" int pgl_log(const char *format, ...)
{
    char text[256];
    va_list args;

    va_start(args, format);
    int const length = vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    size_t end = strlen(text);
    while (end > 0 && text[end - 1] == '\n')
        text[--end] = '\0';

    LOG_F(INFO, "%s", text);
    return length;
}
