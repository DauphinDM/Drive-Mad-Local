// Windows equivalents for the POSIX calls used by the save/plugin layer.
// Directory handles are represented by pseudo descriptors and path joins; all
// guest-facing path confinement happens before these calls in filesystem.c.
#include "win32_compat.h"

#include <limits.h>
#include <process.h>

#define DM_PSEUDO_BASE 10000
#define DM_PSEUDO_COUNT 128
typedef struct {
  bool used, directory;
  char path[4096];
} DmFd;
static DmFd dm_fds[DM_PSEUDO_BASE + DM_PSEUDO_COUNT];

static bool dm_absolute(const char *path) {
  return (((path[0] >= 'A' && path[0] <= 'Z') ||
           (path[0] >= 'a' && path[0] <= 'z')) &&
          path[1] == ':') ||
         path[0] == '/' || path[0] == '\\';
}
static bool dm_full_path(const char *path, char out[4096]) {
  DWORD length = GetFullPathNameA(path, 4096, out, NULL);
  if (!length || length >= 4096) {
    errno = ENOENT;
    return false;
  }
  for (char *p = out; *p; p++)
    if (*p == '\\')
      *p = '/';
  return true;
}
static bool dm_safe_path(const char *path) {
  char probe[4096];
  if (strlen(path) >= sizeof(probe)) {
    errno = ENAMETOOLONG;
    return false;
  }
  strcpy(probe, path);
  char *start = probe;
  if (((start[0] >= 'A' && start[0] <= 'Z') ||
       (start[0] >= 'a' && start[0] <= 'z')) &&
      start[1] == ':')
    start += 2;
  while (*start == '/')
    start++;
  for (char *p = start; ; p++) {
    if (*p == '/' || !*p) {
      char saved = *p;
      *p = 0;
      if (*probe) {
        DWORD attributes = GetFileAttributesA(probe);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
          errno = EACCES;
          return false;
        }
      }
      *p = saved;
      if (!saved)
        break;
    }
  }
  return true;
}
static bool dm_is_dir(const char *path) {
  DWORD attributes = GetFileAttributesA(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY);
}
static DmFd *dm_slot(int fd) {
  return fd >= 0 && fd < DM_PSEUDO_BASE + DM_PSEUDO_COUNT ? &dm_fds[fd] : NULL;
}
static void dm_record(int fd, const char *path, bool directory) {
  DmFd *slot = dm_slot(fd);
  if (!slot)
    return;
  slot->used = true;
  slot->directory = directory;
  snprintf(slot->path, sizeof(slot->path), "%s", path);
}
static const char *dm_path(int fd) {
  DmFd *slot = dm_slot(fd);
  return slot && slot->used ? slot->path : NULL;
}
static int dm_alloc_dir(const char *path) {
  for (int fd = DM_PSEUDO_BASE; fd < DM_PSEUDO_BASE + DM_PSEUDO_COUNT; fd++)
    if (!dm_fds[fd].used) {
      dm_record(fd, path, true);
      return fd;
    }
  errno = EMFILE;
  return -1;
}
static bool dm_join(int directory, const char *path, char out[4096]) {
  const char *base = dm_path(directory);
  if (path[0] == '/' || path[0] == '\\' || dm_absolute(path))
    return dm_full_path(path, out);
  if (!base) {
    errno = EBADF;
    return false;
  }
  if (snprintf(out, 4096, "%s/%s", base, path) >= 4096) {
    errno = ENAMETOOLONG;
    return false;
  }
  return true;
}
int dm_win_open(const char *path, int flags, ...) {
  va_list args;
  va_start(args, flags);
  int mode = (flags & O_CREAT) ? va_arg(args, int) : 0;
  va_end(args);
  (void)mode;
  char full[4096];
  if (!dm_full_path(path, full) || !dm_safe_path(full))
    return -1;
  bool directory = dm_is_dir(full);
  if ((flags & O_DIRECTORY) || directory) {
    if (!directory) {
      errno = ENOTDIR;
      return -1;
    }
    if (flags & (O_CREAT | O_WRONLY | O_RDWR | O_TRUNC)) {
      errno = EISDIR;
      return -1;
    }
    return dm_alloc_dir(full);
  }
  int native = _O_BINARY | (flags & 3);
  if (flags & O_CREAT)
    native |= _O_CREAT;
  if (flags & O_EXCL)
    native |= _O_EXCL;
  if (flags & O_TRUNC)
    native |= _O_TRUNC;
  if (flags & O_APPEND)
    native |= _O_APPEND;
  int fd = _open(full, native, _S_IREAD | _S_IWRITE);
  if (fd < 0)
    return -1;
  dm_record(fd, full, false);
  return fd;
}
int dm_win_openat(int directory, const char *path, int flags, ...) {
  va_list args;
  va_start(args, flags);
  int mode = (flags & O_CREAT) ? va_arg(args, int) : 0;
  va_end(args);
  if (directory == AT_FDCWD)
    return dm_win_open(path, flags, mode);
  char full[4096];
  if (!dm_join(directory, path, full))
    return -1;
  return dm_win_open(full, flags, mode);
}
int dm_win_close(int fd) {
  DmFd *slot = dm_slot(fd);
  if (slot && slot->used)
    slot->used = false;
  if (fd >= DM_PSEUDO_BASE)
    return 0;
  return _close(fd);
}
int dm_win_dup(int fd) {
  const char *path = dm_path(fd);
  if (fd >= DM_PSEUDO_BASE) {
    if (!path) {
      errno = EBADF;
      return -1;
    }
    return dm_alloc_dir(path);
  }
  int copy = _dup(fd);
  if (copy >= 0 && path)
    dm_record(copy, path, false);
  return copy;
}
ssize_t dm_win_read(int fd, void *buffer, size_t size) {
  if (size > INT_MAX)
    size = INT_MAX;
  return _read(fd, buffer, (unsigned)size);
}
ssize_t dm_win_write(int fd, const void *buffer, size_t size) {
  if (size > INT_MAX)
    size = INT_MAX;
  return _write(fd, buffer, (unsigned)size);
}
int64_t dm_win_lseek(int fd, int64_t offset, int whence) {
  return _lseeki64(fd, offset, whence);
}
ssize_t dm_win_pread(int fd, void *buffer, size_t size, int64_t offset) {
  int64_t previous = _lseeki64(fd, 0, SEEK_CUR);
  if (previous < 0 || _lseeki64(fd, offset, SEEK_SET) < 0)
    return -1;
  ssize_t result = dm_win_read(fd, buffer, size);
  _lseeki64(fd, previous, SEEK_SET);
  return result;
}
int dm_win_fsync(int fd) {
  if (fd >= DM_PSEUDO_BASE)
    return 0;
  return _commit(fd);
}
int dm_win_fstat(int fd, struct stat *status) {
  if (fd >= DM_PSEUDO_BASE) {
    memset(status, 0, sizeof(*status));
    status->st_mode = _S_IFDIR | 0700;
    status->st_nlink = 1;
    return 0;
  }
  return _fstat(fd, status);
}
int dm_win_stat(const char *path, struct stat *status) {
  char full[4096];
  if (!dm_full_path(path, full) || !dm_safe_path(full))
    return -1;
  return _stat(full, status);
}
int dm_win_fstatat(int directory, const char *path, struct stat *status,
                   int flags) {
  (void)flags;
  char full[4096];
  if (!dm_join(directory, path, full) || !dm_safe_path(full))
    return -1;
  return _stat(full, status);
}
int dm_win_faccessat(int directory, const char *path, int mode, int flags) {
  (void)flags;
  char full[4096];
  if (!dm_join(directory, path, full) || !dm_safe_path(full))
    return -1;
  return _access(full, mode);
}
int dm_win_mkdir(const char *path, int mode) {
  (void)mode;
  char full[4096];
  if (!dm_full_path(path, full) || !dm_safe_path(full))
    return -1;
  return _mkdir(full);
}
int dm_win_mkdirat(int directory, const char *path, int mode) {
  (void)mode;
  char full[4096];
  if (!dm_join(directory, path, full) || !dm_safe_path(full))
    return -1;
  return _mkdir(full);
}
int dm_win_unlink(const char *path) {
  char full[4096];
  if (!dm_full_path(path, full) || !dm_safe_path(full))
    return -1;
  return _unlink(full);
}
int dm_win_unlinkat(int directory, const char *path, int flags) {
  char full[4096];
  if (!dm_join(directory, path, full) || !dm_safe_path(full))
    return -1;
  return flags & AT_REMOVEDIR ? _rmdir(full) : _unlink(full);
}
int dm_win_rmdir(const char *path) {
  char full[4096];
  if (!dm_full_path(path, full) || !dm_safe_path(full))
    return -1;
  return _rmdir(full);
}
static int dm_errno_from_win(void) {
  switch (GetLastError()) {
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
    return ENOENT;
  case ERROR_ACCESS_DENIED:
  case ERROR_SHARING_VIOLATION:
    return EACCES;
  case ERROR_ALREADY_EXISTS:
  case ERROR_FILE_EXISTS:
    return EEXIST;
  case ERROR_DIR_NOT_EMPTY:
    return ENOTEMPTY;
  default:
    return EIO;
  }
}
int dm_win_rename(const char *from, const char *to) {
  char a[4096], b[4096];
  if (!dm_full_path(from, a) || !dm_full_path(to, b) || !dm_safe_path(a) ||
      !dm_safe_path(b))
    return -1;
  if (!MoveFileExA(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    errno = dm_errno_from_win();
    return -1;
  }
  return 0;
}
int dm_win_renameat(int from_directory, const char *from, int to_directory,
                    const char *to) {
  char a[4096], b[4096];
  if (!dm_join(from_directory, from, a) || !dm_join(to_directory, to, b))
    return -1;
  return dm_win_rename(a, b);
}
int dm_win_utimensat(int directory, const char *path,
                     const struct timespec times[2], int flags) {
  (void)flags;
  char full[4096];
  if (!dm_join(directory, path, full) || !dm_safe_path(full))
    return -1;
  if (!times)
    return _utime(full, NULL);
  struct utimbuf values;
  values.actime = times[0].tv_sec;
  values.modtime = times[1].tv_sec;
  return _utime(full, &values);
}
DIR *dm_win_fdopendir(int fd) {
  const char *path = dm_path(fd);
  if (!path) {
    errno = EBADF;
    return NULL;
  }
  char copy[4096];
  snprintf(copy, sizeof(copy), "%s", path);
  DIR *directory = opendir(copy);
  if (directory)
    dm_win_close(fd);
  return directory;
}
int dm_win_clock_gettime(int clock_id, struct timespec *value) {
  if (clock_id == CLOCK_MONOTONIC) {
    LARGE_INTEGER frequency, counter;
    if (!QueryPerformanceFrequency(&frequency) ||
        !QueryPerformanceCounter(&counter)) {
      errno = EIO;
      return -1;
    }
    value->tv_sec = (time_t)(counter.QuadPart / frequency.QuadPart);
    value->tv_nsec = (long)((counter.QuadPart % frequency.QuadPart) * 1000000000LL /
                            frequency.QuadPart);
    return 0;
  }
  FILETIME filetime;
  GetSystemTimePreciseAsFileTime(&filetime);
  ULARGE_INTEGER ticks;
  ticks.LowPart = filetime.dwLowDateTime;
  ticks.HighPart = filetime.dwHighDateTime;
  uint64_t unix_100ns = ticks.QuadPart - 116444736000000000ULL;
  value->tv_sec = (time_t)(unix_100ns / 10000000ULL);
  value->tv_nsec = (long)((unix_100ns % 10000000ULL) * 100ULL);
  return 0;
}
static char dm_dl_error[512];
void *dm_win_dlopen(const char *path, int flags) {
  (void)flags;
  char full[4096];
  const char *target = path;
  if (strncmp(path, "/proc/self/fd/", 14) == 0) {
    int fd = atoi(path + 14);
    target = dm_path(fd);
    if (!target) {
      snprintf(dm_dl_error, sizeof(dm_dl_error), "invalid pinned library fd");
      return NULL;
    }
  }
  if (!dm_full_path(target, full) || !dm_safe_path(full)) {
    snprintf(dm_dl_error, sizeof(dm_dl_error), "unsafe plugin path");
    return NULL;
  }
  HMODULE library = LoadLibraryA(full);
  if (!library)
    snprintf(dm_dl_error, sizeof(dm_dl_error),
             "LoadLibrary failed for %s (error %lu)", full, GetLastError());
  return library;
}
void *dm_win_dlsym(void *handle, const char *name) {
  void *symbol = (void *)GetProcAddress((HMODULE)handle, name);
  if (!symbol)
    snprintf(dm_dl_error, sizeof(dm_dl_error), "missing symbol: %s", name);
  return symbol;
}
int dm_win_dlclose(void *handle) { return FreeLibrary((HMODULE)handle) ? 0 : -1; }
const char *dm_win_dlerror(void) {
  return dm_dl_error[0] ? dm_dl_error : "no dynamic-loader error";
}
