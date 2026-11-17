// Minimal POSIX compatibility for the native host on Windows.
// The game adapter and plugin loader were written against POSIX openat/dlopen;
// this file keeps their path-confined behaviour on a single-threaded host.
#ifndef DRIVE_MAD_WIN32_COMPAT_H
#define DRIVE_MAD_WIN32_COMPAT_H

#ifndef _WIN32
#error "win32_compat.h is only for Windows builds"
#endif

#include <direct.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <utime.h>
#include <windows.h>

#define AT_FDCWD -100
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define O_NOFOLLOW 0
#define O_CLOEXEC 0
#define O_NONBLOCK 0
#define O_DIRECTORY 0x40000000
#undef O_CREAT
#undef O_EXCL
#undef O_TRUNC
#undef O_APPEND
#undef O_RDONLY
#undef O_WRONLY
#undef O_RDWR
#define O_CREAT _O_CREAT
#define O_EXCL _O_EXCL
#define O_TRUNC _O_TRUNC
#define O_APPEND _O_APPEND
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_RDWR _O_RDWR
#define RTLD_NOW 0
#define RTLD_LOCAL 0
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

int dm_win_open(const char *path, int flags, ...);
int dm_win_openat(int directory, const char *path, int flags, ...);
int dm_win_close(int fd);
int dm_win_dup(int fd);
ssize_t dm_win_read(int fd, void *buffer, size_t size);
ssize_t dm_win_write(int fd, const void *buffer, size_t size);
int64_t dm_win_lseek(int fd, int64_t offset, int whence);
ssize_t dm_win_pread(int fd, void *buffer, size_t size, int64_t offset);
int dm_win_fsync(int fd);
int dm_win_fstat(int fd, struct stat *status);
int dm_win_stat(const char *path, struct stat *status);
int dm_win_fstatat(int directory, const char *path, struct stat *status,
                   int flags);
int dm_win_faccessat(int directory, const char *path, int mode, int flags);
int dm_win_mkdir(const char *path, int mode);
int dm_win_mkdirat(int directory, const char *path, int mode);
int dm_win_unlink(const char *path);
int dm_win_unlinkat(int directory, const char *path, int flags);
int dm_win_rmdir(const char *path);
int dm_win_rename(const char *from, const char *to);
int dm_win_renameat(int from_directory, const char *from, int to_directory,
                    const char *to);
int dm_win_utimensat(int directory, const char *path,
                     const struct timespec times[2], int flags);
DIR *dm_win_fdopendir(int fd);
int dm_win_clock_gettime(int clock_id, struct timespec *value);
void *dm_win_dlopen(const char *path, int flags);
void *dm_win_dlsym(void *handle, const char *name);
int dm_win_dlclose(void *handle);
const char *dm_win_dlerror(void);

#define open dm_win_open
#define openat dm_win_openat
#define close dm_win_close
#define dup dm_win_dup
#define read dm_win_read
#define write dm_win_write
#define lseek dm_win_lseek
#define pread dm_win_pread
#define fsync dm_win_fsync
#define fstat dm_win_fstat
#define fstatat dm_win_fstatat
#define faccessat dm_win_faccessat
#define mkdir dm_win_mkdir
#define mkdirat dm_win_mkdirat
#define unlink dm_win_unlink
#define unlinkat dm_win_unlinkat
#define rmdir dm_win_rmdir
#define rename dm_win_rename
#define renameat dm_win_renameat
#define utimensat dm_win_utimensat
#define fdopendir dm_win_fdopendir
#define clock_gettime dm_win_clock_gettime
#define dlopen dm_win_dlopen
#define dlsym dm_win_dlsym
#define dlclose dm_win_dlclose
#define dlerror dm_win_dlerror
#define strtok_r strtok_s
#define getpid _getpid
#define strdup _strdup

#endif
