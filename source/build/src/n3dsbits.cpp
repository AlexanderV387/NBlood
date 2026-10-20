// Nintendo 3DS support: startup and what newlib lacks.

#include "compat.h"
#include "n3dsbits.h"

#include <3ds.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef APPBASENAME
# define APPBASENAME "eduke32"
#endif

// The main thread's stack: 32 KB by default, too little for the engine.
extern "C" u32 __stacksize__ = 1 << 20;

void n3ds_open(void)
{
    // New 3DS: 804 MHz and the L2 cache.
    osSetSpeedupEnable(true);

    mkdir("sdmc:/3ds", 0777);
    mkdir(N3DS_GAME_DIR, 0777);
    chdir(N3DS_GAME_DIR);
}

// newlib for the 3DS has memalign but not posix_memalign.
extern "C" int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    void *p = memalign(alignment, size);

    if (p == NULL)
        return ENOMEM;

    *memptr = p;
    return 0;
}
