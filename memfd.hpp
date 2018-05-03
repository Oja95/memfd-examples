#ifndef _MEMFD_H
#define _MEMFD_H

#include <syscall.h>

/*
 * SPDX-License-Identifier: Unlicense
 *
 * No glibc wrappers exist for memfd_create(2), so provide our own.
 *
 * Also define memfd fcntl sealing macros. While they are already
 * defined in the kernel header file <linux/fcntl.h>, that file as
 * a whole conflicts with the original glibc header <fnctl.h>.
 */

static inline int memfd_create(const char *name, unsigned int flags) {
    return static_cast<int>(syscall(__NR_memfd_create, name, flags));
}

#define errorp(msg) {        \
    perror("[Error] " msg);      \
    exit(EXIT_FAILURE);        \
}

#define error(...) {        \
    fprintf(stderr, "[Error] ");    \
    fprintf(stderr, __VA_ARGS__);    \
    fprintf(stderr, "\n");      \
}

#ifndef F_LINUX_SPECIFIC_BASE
#define F_LINUX_SPECIFIC_BASE 1024
#endif

#ifndef F_ADD_SEALS
#define F_ADD_SEALS (F_LINUX_SPECIFIC_BASE + 9)

#define F_SEAL_SEAL     0x0001  /* prevent further seals from being set */
#define F_SEAL_SHRINK   0x0002  /* prevent file from shrinking */
#endif

#endif /* _MEMFD_H */
