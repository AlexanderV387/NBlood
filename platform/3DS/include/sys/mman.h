// The 3DS has no memory-mapped files. mio (the texture cache's file
// mapping) compiles against these and gets MAP_FAILED: the texture cache
// is then simply not used.

#ifndef N3DS_SYS_MMAN_H
#define N3DS_SYS_MMAN_H

#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED  1
#define MAP_PRIVATE 2
#define MAP_FAILED  ((void *) -1)

#define MS_ASYNC      1
#define MS_INVALIDATE 2
#define MS_SYNC       4

static inline void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void) addr; (void) length; (void) prot; (void) flags; (void) fd; (void) offset;
    errno = ENOSYS;
    return MAP_FAILED;
}

static inline int munmap(void *addr, size_t length)
{
    (void) addr; (void) length;
    errno = ENOSYS;
    return -1;
}

static inline int msync(void *addr, size_t length, int flags)
{
    (void) addr; (void) length; (void) flags;
    errno = ENOSYS;
    return -1;
}

#endif
