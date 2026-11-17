// Narrow Emscripten/WASI filesystem adapter: embedded /assets is read-only;
// /sandbox and /tmp live under the native save directory, never host /.
#include "host.h"
#ifdef _WIN32
#include "win32_compat.h"
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec
#endif
#endif

typedef struct {
  bool used;
  int fd;
  const NativeAsset *asset;
  const unsigned char *asset_bytes;
  size_t asset_size;
  int64_t position;
  DIR *dir;
  u32 flags;
} File;
static File files[256];
static int root_fd = -1, storage_fd = -1;
static int err(void) {
  switch (errno) {
  case EACCES:
  case EPERM:
    return 2;
  case EBADF:
    return 8;
  case EEXIST:
    return 20;
  case EINVAL:
    return 28;
  case EISDIR:
    return 31;
  case EMFILE:
    return 33;
  case ENOENT:
    return 44;
  case ENOMEM:
    return 48;
  case ENOSPC:
    return 51;
  case ENOTDIR:
    return 54;
  case ENOTEMPTY:
    return 55;
  case EROFS:
    return 69;
  default:
    return 29;
  }
}
static void mkdirs(char *path) {
  char *start = path;
#ifdef _WIN32
  if (((path[0] >= 'A' && path[0] <= 'Z') ||
       (path[0] >= 'a' && path[0] <= 'z')) &&
      path[1] == ':')
    start = path + 2;
#endif
  for (char *p = start + 1; *p; p++)
    if (*p == '/') {
      *p = 0;
      if (mkdir(path, 0700) && errno != EEXIST)
        fail("Cannot create save directory");
      *p = '/';
    }
  if (mkdir(path, 0700) && errno != EEXIST)
    fail("Cannot create save directory");
}
void filesystem_init(struct w2c_a *h) {
  const char *override = getenv("DRIVE_MAD_SAVE_DIR");
#ifdef _WIN32
  const char *local = getenv("LOCALAPPDATA"), *profile = getenv("USERPROFILE");
  int n = override ? snprintf(h->save_dir, sizeof(h->save_dir), "%s", override)
          : local
              ? snprintf(h->save_dir, sizeof(h->save_dir),
                         "%s/drive-mad-native", local)
              : snprintf(h->save_dir, sizeof(h->save_dir),
                         "%s/AppData/Local/drive-mad-native",
                         profile ? profile : ".");
#else
  const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
  int n = override ? snprintf(h->save_dir, sizeof(h->save_dir), "%s", override)
          : xdg
              ? snprintf(h->save_dir, sizeof(h->save_dir),
                         "%s/drive-mad-native", xdg)
              : snprintf(h->save_dir, sizeof(h->save_dir),
                         "%s/.local/share/drive-mad-native", home ? home : "");
#endif
  for (char *p = h->save_dir; *p; p++)
    if (*p == '\\')
      *p = '/';
#ifdef _WIN32
  bool absolute = (((h->save_dir[0] >= 'A' && h->save_dir[0] <= 'Z') ||
                    (h->save_dir[0] >= 'a' && h->save_dir[0] <= 'z')) &&
                   h->save_dir[1] == ':') ||
                  h->save_dir[0] == '/';
#else
  bool absolute = h->save_dir[0] == '/';
#endif
  if (n < 0 || (size_t)n >= sizeof(h->save_dir) || !absolute)
    fail("Save directory must be an absolute path");
  mkdirs(h->save_dir);
  root_fd = open(h->save_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (root_fd < 0)
    fail("Cannot open save directory");
  for (const char **p =
           (const char *[]){"sandbox", "tmp", "local-storage", NULL};
       *p; p++) {
    if (mkdirat(root_fd, *p, 0700) && errno != EEXIST)
      fail("Cannot create native save subdirectory");
  }
  storage_fd =
      openat(root_fd, "local-storage", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (storage_fd < 0)
    fail("Cannot open local storage");
  fprintf(stderr, "Native saves: %s\n", h->save_dir);
}
void filesystem_close(void) {
  for (int i = 3; i < 256; i++)
    if (files[i].used) {
      if (files[i].dir)
        closedir(files[i].dir);
      if (files[i].fd >= 0)
        close(files[i].fd);
      files[i].used = false;
    }
  if (storage_fd >= 0)
    close(storage_fd);
  if (root_fd >= 0)
    close(root_fd);
}
static const NativeAsset *asset(const char *path) {
  for (size_t i = 0; i < native_asset_count; i++)
    if (!strcmp(path, native_asset_index[i].path + (path[0] == '/' ? 0 : 1)))
      return &native_asset_index[i];
  return NULL;
}
static bool asset_dir(const char *path) {
  size_t n = strlen(path);
  if (!strcmp(path, "/"))
    return true;
  for (size_t i = 0; i < native_asset_count; i++) {
    const char *p = native_asset_index[i].path + (path[0] == '/' ? 0 : 1);
    if (!strncmp(path, p, n) && p[n] == '/')
      return true;
  }
  return false;
}
// Resolve parent directories with openat + O_NOFOLLOW so symlinks cannot
// escape.
static int parent(const char *path, char leaf[256]) {
  while (*path == '/')
    path++;
  if (strncmp(path, "sandbox", 7) && strncmp(path, "tmp", 3)) {
    errno = EACCES;
    return -1;
  }
  if (!((!strncmp(path, "sandbox", 7) && (path[7] == '/' || !path[7])) ||
        (!strncmp(path, "tmp", 3) && (path[3] == '/' || !path[3])))) {
    errno = EACCES;
    return -1;
  }
  char copy[4096];
  if (strlen(path) >= sizeof(copy)) {
    errno = EINVAL;
    return -1;
  }
  strcpy(copy, path);
  int fd = dup(root_fd);
  char *save = NULL, *part = strtok_r(copy, "/", &save);
  while (part) {
    if (!strcmp(part, "..") || !strcmp(part, ".") || strlen(part) > 255) {
      close(fd);
      errno = EACCES;
      return -1;
    }
    char *next = strtok_r(NULL, "/", &save);
    if (!next) {
      strcpy(leaf, part);
      return fd;
    }
    int child = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    close(fd);
    if (child < 0)
      return -1;
    fd = child;
    part = next;
  }
  close(fd);
  errno = EINVAL;
  return -1;
}
static File *file(u32 fd) {
  return fd < 256 && files[fd].used ? &files[fd] : NULL;
}
static int newfile(void) {
  for (int i = 3; i < 256; i++)
    if (!files[i].used) {
      files[i] = (File){.used = true, .fd = -1};
      return i;
    }
  errno = EMFILE;
  return -1;
}
u32 dm_syscall_openat(struct w2c_a *h, u32 dirfd, u32 p, u32 flags, u32 args) {
  const char *path = str(h, p);
  const NativeAsset *a = asset(path);
  if (h->tracing)
    fprintf(stderr, "open %s flags=%u\n", path, flags);
  if (a && (flags & (3 | 64 | 128 | 512 | 1024)))
    return (u32)-69;
  int fd = newfile();
  if (fd < 0)
    return (u32)-err();
  File *f = &files[fd];
  f->flags = flags;
  if (a) {
    f->asset = a;
    mods_asset(h, path, &f->asset_bytes, &f->asset_size);
    h->asset_reads++;
    return fd;
  }
  char leaf[256];
  int base = parent(path, leaf);
  if (base < 0) {
    f->used = false;
    return (u32)-err();
  }
  int native = (flags & 3) | O_NOFOLLOW | O_CLOEXEC;
  if (flags & 64)
    native |= O_CREAT;
  if (flags & 128)
    native |= O_EXCL;
  if (flags & 512)
    native |= O_TRUNC;
  if (flags & 1024)
    native |= O_APPEND;
  if (flags & 65536)
    native |= O_DIRECTORY;
  f->fd = openat(base, leaf, native, 0600);
  int error = err();
  close(base);
  if (f->fd < 0) {
    f->used = false;
    return (u32)-error;
  }
  return fd;
}
u32 dm_fd_close(struct w2c_a *h, u32 fd) {
  File *f = file(fd);
  if (!f)
    return 8;
  if (f->dir)
    closedir(f->dir);
  if (f->fd >= 0)
    close(f->fd);
  f->used = false;
  return 0;
}
u32 dm_fd_read(struct w2c_a *h, u32 fd, u32 vectors, u32 n, u32 count) {
  File *f = file(fd);
  if (!f)
    return 8;
  u32 total = 0;
  for (u32 i = 0; i < n; i++) {
    u32 p = read32(h, vectors + i * 8), size = read32(h, vectors + i * 8 + 4);
    void *buffer = mem(h, p, size);
    ssize_t got;
    if (f->asset) {
      size_t left = f->position >= (int64_t)f->asset_size
                        ? 0
                        : f->asset_size - f->position;
      got = size < left ? size : left;
      if (got)
        memcpy(buffer, f->asset_bytes + f->position, got);
      f->position += got;
    } else
      got = read(f->fd, buffer, size);
    if (got < 0)
      return err();
    total += got;
    if ((u32)got < size)
      break;
  }
  write32(h, count, total);
  return 0;
}
u32 dm_fd_write(struct w2c_a *h, u32 fd, u32 vectors, u32 n, u32 count) {
  File *f = file(fd);
  if (fd > 2 && !f)
    return 8;
  if (f && f->asset)
    return 69;
  u32 total = 0;
  for (u32 i = 0; i < n; i++) {
    u32 p = read32(h, vectors + i * 8), size = read32(h, vectors + i * 8 + 4);
    const char *buffer = mem(h, p, size);
    size_t done = 0;
    while (done < size) {
      ssize_t result =
          write(fd <= 2 ? (int)fd : f->fd, buffer + done, size - done);
      if (result < 0) {
        if (errno == EINTR)
          continue;
        return err();
      }
      if (!result)
        return 29;
      done += result;
    }
    total += done;
  }
  write32(h, count, total);
  return 0;
}
u32 dm_fd_seek(struct w2c_a *h, u32 fd, u32 lo, u32 hi, u32 whence,
               u32 result) {
  File *f = file(fd);
  if (!f)
    return 8;
  if (whence > 2)
    return 28;
  int64_t offset = (int64_t)(((u64)hi << 32) | lo), position;
  if (f->asset) {
    position = offset + (whence == 1   ? f->position
                         : whence == 2 ? (int64_t)f->asset_size
                                       : 0);
    if (position < 0)
      return 28;
    f->position = position;
  } else
    position = lseek(f->fd, offset, whence);
  if (position < 0)
    return err();
  write64(h, result, position);
  return 0;
}
static void put_stat(struct w2c_a *h, u32 p, const struct stat *s) {
  memset(mem(h, p, 96), 0, 96);
  write32(h, p, s->st_dev);
  write32(h, p + 4, s->st_mode);
  write32(h, p + 8, s->st_nlink);
#ifdef _WIN32
  write32(h, p + 12, 0);
  write32(h, p + 16, 0);
  write32(h, p + 20, 0);
  write64(h, p + 24, s->st_size);
  write32(h, p + 32, 4096);
  write32(h, p + 36, 0);
  write64(h, p + 40, s->st_atime);
  write32(h, p + 48, 0);
  write64(h, p + 56, s->st_mtime);
  write32(h, p + 64, 0);
  write64(h, p + 72, s->st_ctime);
  write32(h, p + 80, 0);
  write64(h, p + 88, 0);
#else
  write32(h, p + 12, s->st_uid);
  write32(h, p + 16, s->st_gid);
  write32(h, p + 20, s->st_rdev);
  write64(h, p + 24, s->st_size);
  write32(h, p + 32, 4096);
  write32(h, p + 36, s->st_blocks);
  write64(h, p + 40, s->st_atim.tv_sec);
  write32(h, p + 48, s->st_atim.tv_nsec);
  write64(h, p + 56, s->st_mtim.tv_sec);
  write32(h, p + 64, s->st_mtim.tv_nsec);
  write64(h, p + 72, s->st_ctim.tv_sec);
  write32(h, p + 80, s->st_ctim.tv_nsec);
  write64(h, p + 88, s->st_ino);
#endif
}
u32 dm_syscall_stat64(struct w2c_a *h, u32 p, u32 buf) {
  const char *path = str(h, p);
  const NativeAsset *a = asset(path);
  struct stat s = {0};
  if (a || asset_dir(path)) {
    s.st_mode = a ? (S_IFREG | 0444) : (S_IFDIR | 0555);
    s.st_size = a ? a->size : 4096;
    if (a) {
      const unsigned char *data;
      size_t size;
      mods_asset(h, path, &data, &size);
      s.st_size = size;
    }
    s.st_nlink = 1;
    s.st_ino = 1;
  } else {
    char leaf[256];
    int base = parent(path, leaf);
    if (base < 0)
      return (u32)-err();
    int rc = fstatat(base, leaf, &s, AT_SYMLINK_NOFOLLOW), error = err();
    close(base);
    if (rc < 0)
      return (u32)-error;
  }
  put_stat(h, buf, &s);
  return 0;
}
u32 dm_syscall_faccessat(struct w2c_a *h, u32 dir, u32 p, u32 mode, u32 flags) {
  const char *path = str(h, p);
  if (asset(path) || asset_dir(path))
    return mode & 2 ? (u32)-2 : 0;
  char leaf[256];
  int base = parent(path, leaf);
  if (base < 0)
    return (u32)-err();
  int rc = faccessat(base, leaf, mode, AT_SYMLINK_NOFOLLOW), error = err();
  close(base);
  return rc < 0 ? (u32)-error : 0;
}
u32 dm_syscall_mkdirat(struct w2c_a *h, u32 dir, u32 p, u32 mode) {
  char leaf[256];
  int base = parent(str(h, p), leaf);
  if (base < 0)
    return (u32)-err();
  int rc = mkdirat(base, leaf, 0700), error = err();
  close(base);
  return rc < 0 ? (u32)-error : 0;
}
u32 dm_syscall_unlinkat(struct w2c_a *h, u32 dir, u32 p, u32 flags) {
  char leaf[256];
  int base = parent(str(h, p), leaf);
  if (base < 0)
    return (u32)-err();
  int rc = unlinkat(base, leaf, flags == 512 ? AT_REMOVEDIR : 0), error = err();
  close(base);
  return rc < 0 ? (u32)-error : 0;
}
u32 dm_syscall_rmdir(struct w2c_a *h, u32 p) {
  return dm_syscall_unlinkat(h, -100, p, 512);
}
u32 dm_syscall_renameat(struct w2c_a *h, u32 olddir, u32 p, u32 newdir, u32 q) {
  char a[256], b[256];
  int x = parent(str(h, p), a);
  if (x < 0)
    return (u32)-err();
  int y = parent(str(h, q), b);
  if (y < 0) {
    int error = err();
    close(x);
    return (u32)-error;
  }
  int rc = renameat(x, a, y, b), error = err();
  close(x);
  close(y);
  return rc < 0 ? (u32)-error : 0;
}
u32 dm_syscall_utimensat(struct w2c_a *h, u32 dir, u32 p, u32 times,
                         u32 flags) {
  char leaf[256];
  int base = parent(str(h, p), leaf);
  if (base < 0)
    return (u32)-err();
  struct timespec ts[2];
  for (int i = 0; times && i < 2; i++) {
    ts[i].tv_sec = (int64_t)(((u64)read32(h, times + i * 16 + 4) << 32) |
                             read32(h, times + i * 16));
    ts[i].tv_nsec = read32(h, times + i * 16 + 8);
  }
  int rc = utimensat(base, leaf, times ? ts : NULL, AT_SYMLINK_NOFOLLOW),
      error = err();
  close(base);
  return rc < 0 ? (u32)-error : 0;
}
u32 dm_syscall_getdents64(struct w2c_a *h, u32 fd, u32 p, u32 size) {
  File *f = file(fd);
  if (!f || f->fd < 0)
    return (u32)-8;
  if (!f->dir)
    f->dir = fdopendir(dup(f->fd));
  if (!f->dir)
    return (u32)-err();
  u32 count = 0;
  struct dirent *entry;
  while (size - count >= 280 && (entry = readdir(f->dir))) {
    memset(mem(h, p + count, 280), 0, 280);
    write64(h, p + count, entry->d_ino);
    write64(h, p + count + 8, (++f->position) * 280);
    uint16_t length = 280;
    memcpy(mem(h, p + count + 16, 2), &length, 2);
#ifdef _WIN32
    // MinGW's dirent has no d_type; derive the DirEntry type from stat.
    struct stat st;
    u8 type = 0;
    if (fstatat(f->fd, entry->d_name, &st, 0) == 0)
      type = (st.st_mode & _S_IFDIR) ? 4 : (st.st_mode & _S_IFREG) ? 8 : 0;
#else
    u8 type = entry->d_type;
#endif
    *(u8 *)mem(h, p + count + 18, 1) = type;
    snprintf(mem(h, p + count + 19, 256), 256, "%s", entry->d_name);
    count += 280;
  }
  return count;
}
u32 dm_syscall_fcntl64(struct w2c_a *h, u32 fd, u32 cmd, u32 args) {
  File *f = file(fd);
  if (!f && fd > 2)
    return (u32)-8;
  if (cmd == 1 || cmd == 2 || cmd == 13 || cmd == 14)
    return 0;
  if (cmd == 3)
    return f ? f->flags : 0;
  if (cmd == 4 && f) {
    f->flags |= read32(h, args);
    return 0;
  }
  return (u32)-28;
}
u32 dm_syscall_ioctl(struct w2c_a *h, u32 fd, u32 op, u32 args) {
  return (u32)-59;
}

// Storage.write uses an independent key/value store in addition to /sandbox.
static bool keyname(const char *key, char name[256]) {
  size_t n = strlen(key);
  if (!n || n > 120)
    return false;
  for (size_t i = 0; i < n; i++)
    snprintf(name + i * 2, 3, "%02x", (unsigned char)key[i]);
  return true;
}
void storage_write(struct w2c_a *h, u32 path, u32 data, u32 size) {
  char name[256], temp[256];
  if (!keyname(str(h, path), name))
    fail("Native storage key too long");
  snprintf(temp, sizeof(temp), ".%s.tmp", name);
  int fd =
      openat(storage_fd, temp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
  if (fd < 0)
    fail("Cannot write native storage");
  const char *bytes = mem(h, data, size);
  size_t done = 0;
  while (done < size) {
    ssize_t n = write(fd, bytes + done, size - done);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      fail("Native storage write failed");
    done += n;
  }
  if (fsync(fd))
    fail("Native storage sync failed");
  close(fd);
  if (renameat(storage_fd, temp, storage_fd, name))
    fail("Native storage rename failed");
  fsync(storage_fd);
}
void storage_remove(struct w2c_a *h, u32 path) {
  char name[256];
  if (keyname(str(h, path), name) && unlinkat(storage_fd, name, 0) &&
      errno != ENOENT)
    fail("Native storage remove failed");
}
void storage_init(struct w2c_a *h) {
  DIR *dir = fdopendir(dup(storage_fd));
  if (!dir)
    fail("Cannot read native storage");
  rewinddir(dir);
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    size_t n = strlen(entry->d_name);
    if (!n || n > 240 || n % 2 ||
        strspn(entry->d_name, "0123456789abcdef") != n)
      continue;
    char key[121] = {0};
    for (size_t i = 0; i < n; i += 2) {
      char pair[3] = {entry->d_name[i], entry->d_name[i + 1], 0};
      key[i / 2] = strtoul(pair, NULL, 16);
    }
    int fd = openat(storage_fd, entry->d_name, O_RDONLY | O_NOFOLLOW);
    struct stat s;
    if (fd < 0)
      continue;
    if (fstat(fd, &s) || !S_ISREG(s.st_mode) || s.st_size < 0 ||
        s.st_size > 16 * 1024 * 1024) {
      close(fd);
      continue;
    }
    u32 path = copy_string(h, key),
        data = game_malloc(&h->game, s.st_size ? s.st_size : 1);
    size_t done = 0;
    while (done < (size_t)s.st_size) {
      ssize_t got =
          read(fd, mem(h, data + done, s.st_size - done), s.st_size - done);
      if (got < 0 && errno == EINTR)
        continue;
      if (got <= 0)
        break;
      done += got;
    }
    close(fd);
    if (done == (size_t)s.st_size)
      game_storage_write(&h->game, path, data, done);
    game_free(&h->game, path);
    game_free(&h->game, data);
  }
  closedir(dir);
}

// Exercise the real translated memory ABI without requiring a graphics session.
void filesystem_self_test(struct w2c_a *h) {
#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr))                                                               \
      fail("Filesystem self-test failed: " #expr);                             \
  } while (0)
  u32 scratch = game_malloc(&h->game, 4096), output = scratch + 64,
      count = scratch + 32;
  u32 name = copy_string(h, "assets/atlas/default.png");
  u32 fd = dm_syscall_openat(h, -100, name, 0, 0);
  CHECK((s32)fd >= 3);
  write32(h, scratch, output);
  write32(h, scratch + 4, 8);
  CHECK(dm_fd_read(h, fd, scratch, 1, count) == 0 && read32(h, count) == 8);
  CHECK(!memcmp(mem(h, output, 8), "\x89PNG\r\n\x1a\n", 8));
  CHECK(dm_fd_write(h, fd, scratch, 1, count) == 69);
  const unsigned char *effective_bytes;
  size_t effective_size;
  mods_asset(h, "assets/atlas/default.png", &effective_bytes, &effective_size);
  CHECK(dm_fd_seek(h, fd, 0, 0, 2, count) == 0 &&
        read32(h, count) == effective_size);
  CHECK(dm_syscall_stat64(h, name, scratch + 128) == 0 &&
        read32(h, scratch + 128 + 24) == effective_size);
  CHECK(dm_fd_seek(h, fd, 0, 0, 0, count) == 0 && read32(h, count) == 0);
  CHECK(dm_fd_close(h, fd) == 0 && dm_fd_close(h, fd) == 8);
  CHECK(dm_syscall_openat(h, -100, name, 1, 0) == (u32)-69);
  CHECK(dm_syscall_openat(h, -100, name, 64, 0) == (u32)-69);
  game_free(&h->game, name);
  name = copy_string(h, "/sandbox/../escape");
  CHECK(dm_syscall_openat(h, -100, name, 64 | 1, 0) == (u32)-2);
  game_free(&h->game, name);
  name = copy_string(h, "/etc/passwd");
  CHECK(dm_syscall_openat(h, -100, name, 0, 0) == (u32)-2);
  game_free(&h->game, name);
  char test_path[128];
  snprintf(test_path, sizeof(test_path), "/sandbox/.native-host-test-%ld",
           (long)getpid());
  name = copy_string(h, test_path);
  fd = dm_syscall_openat(h, -100, name, 64 | 128 | 2, 0);
  CHECK((s32)fd >= 3);
  memcpy(mem(h, output, 12), "native-save!", 12);
  write32(h, scratch + 4, 12);
  CHECK(dm_fd_write(h, fd, scratch, 1, count) == 0 && read32(h, count) == 12);
  CHECK(dm_fd_seek(h, fd, 0, 0, 0, count) == 0);
  memset(mem(h, output, 12), 0, 12);
  CHECK(dm_fd_read(h, fd, scratch, 1, count) == 0 &&
        !memcmp(mem(h, output, 12), "native-save!", 12));
  CHECK(dm_fd_close(h, fd) == 0);
  CHECK(dm_syscall_stat64(h, name, scratch + 128) == 0 &&
        read32(h, scratch + 128 + 24) == 12);
  CHECK(dm_syscall_unlinkat(h, -100, name, 0) == 0);
  game_free(&h->game, name);
  u32 key = copy_string(h, test_path);
  storage_write(h, key, output, 12);
  char encoded[256];
  CHECK(keyname(test_path, encoded));
  int actual = openat(storage_fd, encoded, O_RDONLY | O_NOFOLLOW);
  char bytes[12];
  CHECK(actual >= 0 && read(actual, bytes, 12) == 12 &&
        !memcmp(bytes, "native-save!", 12));
  close(actual);
  storage_remove(h, key);
  CHECK(faccessat(storage_fd, encoded, F_OK, 0) < 0 && errno == ENOENT);
  game_free(&h->game, key);
  game_free(&h->game, scratch);
#undef CHECK
}
