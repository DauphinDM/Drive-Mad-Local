// OpenGL ES 2 entry points. All pointer arguments refer to translated linear
// memory.
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL 0x84f9
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821a
#endif

#ifdef DM_DESKTOP_GL
// The game ships GLSL ES 1.00 shaders. Desktop GL 2.1 accepts the same syntax
// as GLSL 1.20 once ES-only precision/extension declarations are removed.
static bool line_contains(const char *line, size_t length, const char *needle) {
  size_t n = strlen(needle);
  if (n > length)
    return false;
  for (size_t i = 0; i + n <= length; i++)
    if (memcmp(line + i, needle, n) == 0)
      return true;
  return false;
}
static bool qualifier_at(const char *p) {
  static const char *const words[] = {"highp", "mediump", "lowp"};
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
    size_t n = strlen(words[i]);
    if (strncmp(p, words[i], n) == 0 &&
        (p[n] == '\0' || !(p[n] == '_' || (p[n] >= '0' && p[n] <= '9') ||
                           (p[n] >= 'A' && p[n] <= 'Z') ||
                           (p[n] >= 'a' && p[n] <= 'z'))))
      return true;
  }
  return false;
}
static char *desktop_shader(const char *source) {
  size_t length = strlen(source), capacity = length * 2 + 256, used = 0;
  char *out = malloc(capacity);
  if (!out)
    return NULL;
  bool has_version = strstr(source, "#version") != NULL;
  if (!has_version) {
    memcpy(out + used, "#version 120\n", 13);
    used += 13;
  }
  const char *p = source;
  while (*p) {
    const char *end = strchr(p, '\n');
    size_t n = end ? (size_t)(end - p) + 1 : strlen(p);
    const char *t = p;
    while (t < p + n && (*t == ' ' || *t == '\t'))
      t++;
    bool handled = false;
    if (strncmp(t, "#version", 8) == 0) {
      if (line_contains(t, n - (size_t)(t - p), "300") ||
          line_contains(t, n - (size_t)(t - p), "310") ||
          line_contains(t, n - (size_t)(t - p), "320")) {
        free(out);
        return NULL;
      }
      memcpy(out + used, "#version 120\n", 13);
      used += 13;
      handled = true;
    } else if (strncmp(t, "precision", 9) == 0) {
      handled = true; // Drop the complete precision statement.
    } else if (strncmp(t, "#define", 7) == 0 &&
               line_contains(t, n - (size_t)(t - p), "precision")) {
      // The game defines FLOAT_PRECISION_* as "precision highp float". Keep
      // the macro name but make it empty for desktop GLSL.
      const char *name = t + 7;
      while (*name == ' ' || *name == '\t')
        name++;
      const char *name_end = name;
      while (name_end < p + n && *name_end != ' ' && *name_end != '\t' &&
             *name_end != '\n' && *name_end != '\r')
        name_end++;
      memcpy(out + used, "#define ", 8);
      used += 8;
      memcpy(out + used, name, (size_t)(name_end - name));
      used += (size_t)(name_end - name);
      out[used++] = '\n';
      handled = true;
    } else if (strncmp(t, "FLOAT_PRECISION_MEDIUM", 22) == 0 ||
               strncmp(t, "FLOAT_PRECISION_HIGH", 20) == 0) {
      size_t length = strncmp(t, "FLOAT_PRECISION_MEDIUM", 22) == 0 ? 22 : 20;
      const char *rest = t + length;
      size_t remaining = n - (size_t)(rest - p);
      while (remaining && (*rest == ' ' || *rest == '\t')) {
        rest++;
        remaining--;
      }
      if (remaining && *rest == ';') {
        rest++;
        remaining--;
        while (remaining && (*rest == ' ' || *rest == '\t' ||
                             *rest == '\r' || *rest == '\n')) {
          rest++;
          remaining--;
        }
      }
      handled = remaining == 0;
    } else if (strncmp(t, "#extension", 10) == 0 &&
               (line_contains(t, n - (size_t)(t - p), "GL_OES") ||
                line_contains(t, n - (size_t)(t - p), "GL_EXT"))) {
      // Desktop GLSL 1.20 has the derivatives the game uses; ES extensions
      // are rejected by desktop compilers.
      handled = true;
    }
    if (!handled) {
      for (size_t i = 0; i < n;) {
        if (qualifier_at(p + i)) {
          i += strncmp(p + i, "mediump", 7) == 0   ? 7
               : strncmp(p + i, "highp", 5) == 0   ? 5
                                                   : 4;
        } else {
          out[used++] = p[i++];
        }
      }
    }
    p += n;
  }
  out[used] = '\0';
  return out;
}
#endif

void dm_glEnable(struct w2c_a *h, u32 a) { glEnable(a); }
void dm_glDisable(struct w2c_a *h, u32 a) { glDisable(a); }
void dm_glTexParameterf(struct w2c_a *h, u32 a, u32 b, f32 c) {
  glTexParameterf(a, b, c);
}
void dm_glBindTexture(struct w2c_a *h, u32 a, u32 b) { glBindTexture(a, b); }
void dm_glGetIntegerv(struct w2c_a *h, u32 a, u32 p) {
  GLint v[16] = {0};
  glGetIntegerv(a, v);
  size_t n = (a == GL_VIEWPORT || a == GL_SCISSOR_BOX)
                 ? 4
                 : ((a == GL_MAX_VIEWPORT_DIMS) ? 2 : 1);
  memcpy(mem(h, p, n * 4), v, n * 4);
}
void dm_glBindRenderbuffer(struct w2c_a *h, u32 a, u32 b) {
  glBindRenderbuffer(a, b);
}
void dm_glBindFramebuffer(struct w2c_a *h, u32 a, u32 b) {
  glBindFramebuffer(a, b);
}
u32 dm_glGetUniformLocation(struct w2c_a *h, u32 a, u32 p) {
  return glGetUniformLocation(a, str(h, p));
}
void dm_glTexParameteri(struct w2c_a *h, u32 a, u32 b, u32 c) {
  glTexParameteri(a, b, (int)c);
}
void dm_glDisableVertexAttribArray(struct w2c_a *h, u32 a) {
  glDisableVertexAttribArray(a);
}
void dm_glViewport(struct w2c_a *h, u32 a, u32 b, u32 c, u32 d) {
  glViewport(a, b, c, d);
}
void dm_glUniformMatrix4fv(struct w2c_a *h, u32 a, u32 b, u32 c, u32 p) {
  glUniformMatrix4fv(a, b, c, mem(h, p, (size_t)b * 64));
}
static size_t pixel_size(u32 width, u32 height, u32 format, u32 type) {
  size_t n = format == GL_RGBA              ? 4
             : format == GL_RGB             ? 3
             : format == GL_LUMINANCE_ALPHA ? 2
                                            : 1;
  if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 ||
      type == GL_UNSIGNED_SHORT_5_5_5_1)
    n = 2;
  if (type == GL_FLOAT)
    n *= 4;
  GLint alignment = 4;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
  size_t row = (size_t)width * n,
         stride = (row + alignment - 1) & ~((size_t)alignment - 1);
  return height ? stride * (height - 1) + row : 0;
}
void dm_glTexImage2D(struct w2c_a *h, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f,
                     u32 g, u32 i, u32 p) {
  glTexImage2D(a, b, c, d, e, f, g, i,
               p ? mem(h, p, pixel_size(d, e, g, i)) : NULL);
}
void dm_glGenTextures(struct w2c_a *h, u32 n, u32 p) {
  glGenTextures(n, mem(h, p, (size_t)n * 4));
}
u32 dm_glGetAttribLocation(struct w2c_a *h, u32 a, u32 p) {
  return glGetAttribLocation(a, str(h, p));
}
void dm_glVertexAttribPointer(struct w2c_a *h, u32 a, u32 b, u32 c, u32 d,
                              u32 e, u32 p) {
  GLint buffer = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
  glVertexAttribPointer(a, b, c, d, e,
                        buffer ? (void *)(uintptr_t)p : mem(h, p, 1));
}
void dm_glDepthMask(struct w2c_a *h, u32 a) { glDepthMask(a); }
void dm_glEnableVertexAttribArray(struct w2c_a *h, u32 a) {
  glEnableVertexAttribArray(a);
}
void dm_glDeleteRenderbuffers(struct w2c_a *h, u32 n, u32 p) {
  glDeleteRenderbuffers(n, mem(h, p, (size_t)n * 4));
}
void dm_glBindBuffer(struct w2c_a *h, u32 a, u32 b) { glBindBuffer(a, b); }
void dm_glDeleteTextures(struct w2c_a *h, u32 n, u32 p) {
  glDeleteTextures(n, mem(h, p, (size_t)n * 4));
}
void dm_glFramebufferRenderbuffer(struct w2c_a *h, u32 a, u32 b, u32 c, u32 d) {
#ifndef DM_DESKTOP_GL
  // GLES2 core has no packed depth/stencil attachment; EXT/OES drivers expose
  // it as a depth attachment with a packed depth-stencil renderbuffer.
  if (b == GL_DEPTH_STENCIL_ATTACHMENT)
    b = GL_DEPTH_ATTACHMENT;
#endif
  glFramebufferRenderbuffer(a, b, c, d);
}
void dm_glRenderbufferStorage(struct w2c_a *h, u32 a, u32 b, u32 c, u32 d) {
#ifndef DM_DESKTOP_GL
  if (b == GL_DEPTH_STENCIL) {
    // The game asks for a combined attachment, which is not a core GLES2
    // renderbuffer format. Use the packed extension, then fall back to depth.
    static const GLenum packed = 0x88f0; // GL_DEPTH24_STENCIL8_OES
    glRenderbufferStorage(a, packed, c, d);
    if (glGetError() != GL_INVALID_ENUM)
      return;
    b = GL_DEPTH_COMPONENT16;
  }
#endif
  glRenderbufferStorage(a, b, c, d);
}
void dm_glGenRenderbuffers(struct w2c_a *h, u32 n, u32 p) {
  glGenRenderbuffers(n, mem(h, p, (size_t)n * 4));
}
void dm_glClear(struct w2c_a *h, u32 a) { glClear(a); }
void dm_glBufferData(struct w2c_a *h, u32 a, u32 b, u32 p, u32 c) {
  glBufferData(a, b, p ? mem(h, p, b) : NULL, c);
}
void dm_glDrawArrays(struct w2c_a *h, u32 a, u32 b, u32 c) {
  glDrawArrays(a, b, c);
  h->draw_calls++;
}
void dm_glDeleteFramebuffers(struct w2c_a *h, u32 n, u32 p) {
  glDeleteFramebuffers(n, mem(h, p, (size_t)n * 4));
}
void dm_glUniform3fv(struct w2c_a *h, u32 a, u32 n, u32 p) {
  glUniform3fv(a, n, mem(h, p, (size_t)n * 12));
}
void dm_glDepthRangef(struct w2c_a *h, f32 a, f32 b) { glDepthRangef(a, b); }
void dm_glGenFramebuffers(struct w2c_a *h, u32 n, u32 p) {
  glGenFramebuffers(n, mem(h, p, (size_t)n * 4));
}
void dm_glGenBuffers(struct w2c_a *h, u32 n, u32 p) {
  glGenBuffers(n, mem(h, p, (size_t)n * 4));
}
void dm_glUseProgram(struct w2c_a *h, u32 a) { glUseProgram(a); }
void dm_glClearColor(struct w2c_a *h, f32 a, f32 b, f32 c, f32 d) {
  glClearColor(a, b, c, d);
}
void dm_glUniform1i(struct w2c_a *h, u32 a, u32 b) { glUniform1i(a, b); }
void dm_glColorMask(struct w2c_a *h, u32 a, u32 b, u32 c, u32 d) {
  glColorMask(a, b, c, d);
}
void dm_glGetBooleanv(struct w2c_a *h, u32 a, u32 p) {
  GLboolean v[16] = {0};
  glGetBooleanv(a, v);
  size_t n = a == GL_COLOR_WRITEMASK ? 4 : 1;
  memcpy(mem(h, p, n), v, n);
}
void dm_glFramebufferTexture2D(struct w2c_a *h, u32 a, u32 b, u32 c, u32 d,
                               u32 e) {
  glFramebufferTexture2D(a, b, c, d, e);
}
void dm_glUniform2f(struct w2c_a *h, u32 a, f32 b, f32 c) {
  glUniform2f(a, b, c);
}
void dm_glStencilOp(struct w2c_a *h, u32 a, u32 b, u32 c) {
  glStencilOp(a, b, c);
}
void dm_glStencilFunc(struct w2c_a *h, u32 a, u32 b, u32 c) {
  glStencilFunc(a, b, c);
}
void dm_glScissor(struct w2c_a *h, u32 a, u32 b, u32 c, u32 d) {
  glScissor(a, b, c, d);
}
void dm_glDeleteBuffers(struct w2c_a *h, u32 n, u32 p) {
  glDeleteBuffers(n, mem(h, p, (size_t)n * 4));
}
void dm_glReadPixels(struct w2c_a *h, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f,
                     u32 p) {
  glReadPixels(a, b, c, d, e, f, mem(h, p, (size_t)c * d * 4));
}
void dm_glUniform1f(struct w2c_a *h, u32 a, f32 b) { glUniform1f(a, b); }
void dm_glGetProgramiv(struct w2c_a *h, u32 a, u32 b, u32 p) {
  glGetProgramiv(a, b, mem(h, p, 4));
}
void dm_glAttachShader(struct w2c_a *h, u32 a, u32 b) { glAttachShader(a, b); }
void dm_glGetShaderiv(struct w2c_a *h, u32 a, u32 b, u32 p) {
  glGetShaderiv(a, b, mem(h, p, 4));
}
void dm_glActiveTexture(struct w2c_a *h, u32 a) { glActiveTexture(a); }
u32 dm_glCheckFramebufferStatus(struct w2c_a *h, u32 a) {
  return glCheckFramebufferStatus(a);
}
void dm_glClearStencil(struct w2c_a *h, u32 a) { glClearStencil(a); }
void dm_glUniform3f(struct w2c_a *h, u32 a, f32 b, f32 c, f32 d) {
  glUniform3f(a, b, c, d);
}
void dm_glGetProgramInfoLog(struct w2c_a *h, u32 a, u32 b, u32 p, u32 q) {
  glGetProgramInfoLog(a, b, p ? mem(h, p, 4) : NULL, mem(h, q, b));
}
void dm_glLinkProgram(struct w2c_a *h, u32 a) { glLinkProgram(a); }
u32 dm_glCreateProgram(struct w2c_a *h) { return glCreateProgram(); }
void dm_glGetShaderInfoLog(struct w2c_a *h, u32 a, u32 b, u32 p, u32 q) {
  glGetShaderInfoLog(a, b, p ? mem(h, p, 4) : NULL, mem(h, q, b));
}
void dm_glCompileShader(struct w2c_a *h, u32 a) {
  glCompileShader(a);
#ifdef DM_DESKTOP_GL
  GLint ok = 0;
  glGetShaderiv(a, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096] = {0};
    glGetShaderInfoLog(a, sizeof(log) - 1, NULL, log);
    fprintf(stderr, "Desktop shader compile failed:\n%s\n", log);
  }
#endif
}
void dm_glShaderSource(struct w2c_a *h, u32 shader, u32 n, u32 strings,
                       u32 lengths) {
  const char **values = calloc(n, sizeof(char *));
  GLint *sizes = calloc(n, sizeof(GLint));
  if (!values || !sizes)
    fail("Out of memory for shader source");
  for (u32 i = 0; i < n; i++) {
    u32 p = read32(h, strings + i * 4);
    sizes[i] = lengths ? (s32)read32(h, lengths + i * 4) : -1;
    values[i] = sizes[i] < 0 ? str(h, p) : mem(h, p, sizes[i]);
  }
#ifdef DM_DESKTOP_GL
  size_t total = 1;
  for (u32 i = 0; i < n; i++)
    total += sizes[i] < 0 ? strlen(values[i]) : (size_t)sizes[i];
  char *joined = malloc(total);
  if (!joined)
    fail("Out of memory for shader source");
  size_t cursor = 0;
  for (u32 i = 0; i < n; i++) {
    size_t size = sizes[i] < 0 ? strlen(values[i]) : (size_t)sizes[i];
    memcpy(joined + cursor, values[i], size);
    cursor += size;
  }
  joined[cursor] = '\0';
  char *converted = desktop_shader(joined);
  free(joined);
  if (!converted) {
    fprintf(stderr, "Unsupported desktop shader version\n");
    glShaderSource(shader, n, (const GLchar *const *)values, sizes);
  } else {
    static unsigned dumped;
    const char *directory = getenv("DRIVE_MAD_DUMP_SHADERS");
    if (directory && dumped < 200) {
      char path[4096];
      snprintf(path, sizeof(path), "%s/shader-%03u.glsl", directory, dumped++);
      FILE *file = fopen(path, "wb");
      if (file) {
        fwrite(converted, 1, strlen(converted), file);
        fclose(file);
      }
    }
    const GLchar *source = converted;
    GLint size = (GLint)strlen(converted);
    glShaderSource(shader, 1, (const GLchar *const *)&source, &size);
    free(converted);
  }
#else
  glShaderSource(shader, n, (const GLchar *const *)values, sizes);
#endif
  free(values);
  free(sizes);
}
u32 dm_glCreateShader(struct w2c_a *h, u32 a) { return glCreateShader(a); }
void dm_glTexSubImage2D(struct w2c_a *h, u32 a, u32 b, u32 c, u32 d, u32 e,
                        u32 f, u32 g, u32 i, u32 p) {
  glTexSubImage2D(a, b, c, d, e, f, g, i, mem(h, p, pixel_size(e, f, g, i)));
}
void dm_glStencilMask(struct w2c_a *h, u32 a) { glStencilMask(a); }
void dm_glBlendFunc(struct w2c_a *h, u32 a, u32 b) { glBlendFunc(a, b); }
void dm_glDepthFunc(struct w2c_a *h, u32 a) { glDepthFunc(a); }
