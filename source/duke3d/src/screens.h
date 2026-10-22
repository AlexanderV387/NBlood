//-------------------------------------------------------------------------
/*
Copyright (C) 2016 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

extern void G_DisplayExtraScreens(void);
#ifdef __3DS__
// The 3DS's bottom screen (screens.cpp).
extern uint32_t g_n3dsInputBits;   // input bits from taps, used once
extern int32_t g_n3dsConfigVersion; // for changes to older settings
int G_N3DS_StatusBarOnBottom(void);
void G_N3DS_Init(void);
# include "n3dsbits.h"
# define G_N3DS_WHERE(place) N3DS_WHERE(place)
#else
# define G_N3DS_WHERE(place) ((void) 0)
#endif
extern void G_DisplayLogo(void);
extern void G_DoOrderScreen(void);

static inline int G_LastMapInfoIndex(void)
{
    Bassert(ud.last_level >= 1);  // NOTE: last_level is 1-based
    return ud.volume_number*MAXLEVELS + ud.last_level-1;
}

#if 1//def DEBUGGINGAIDS
typedef struct {
    uint32_t lastgtic;
    uint32_t lastnumins, numins;
    int32_t numonscreen;
} sprstat_t;

extern sprstat_t g_spriteStat;
#endif

extern int32_t dr_yxaspect, dr_viewingrange;
extern int32_t g_noLogoAnim, g_noLogo;
extern double g_moveActorsTime, g_moveWorldTime;

extern void G_FadePalette(int32_t r, int32_t g, int32_t b, int32_t e);
