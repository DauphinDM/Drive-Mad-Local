// Windows OpenGL 2.1 entry-point loader. The DLL only exports GL 1.1.
#include "gl_compat.h"

#ifdef _WIN32
#include <SDL.h>
#include <stdio.h>

#define DM_GL_DEFINE(name, type) type dm_gl_p_##name;
DM_GL_POINTERS(DM_GL_DEFINE)
#undef DM_GL_DEFINE

#define DM_GL_LOAD(name, type)                                                 \
  do {                                                                         \
    dm_gl_p_##name = (type)SDL_GL_GetProcAddress(#name);                       \
    if (!dm_gl_p_##name) {                                                     \
      fprintf(stderr, "Missing desktop OpenGL entry point: %s\n", #name);      \
      return 0;                                                                \
    }                                                                          \
  } while (0);

int dm_gl_load(void) {
  DM_GL_POINTERS(DM_GL_LOAD);
  return 1;
}
#else
int dm_gl_load(void) { return 1; }
#endif
