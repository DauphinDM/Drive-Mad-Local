// Desktop OpenGL 2.1 compatibility for the GLES2 calls used by the game.
// Linux keeps its native GLES2 path; macOS and Windows use this shim.
#ifndef DRIVE_MAD_GL_COMPAT_H
#define DRIVE_MAD_GL_COMPAT_H

#if defined(DM_DESKTOP_GL)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#endif
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#ifndef GLchar
typedef char GLchar;
#endif

// GLES2 entry points not present in desktop GL 2.1.
static inline void dm_compat_glDepthRangef(GLfloat near_value,
                                           GLfloat far_value) {
  glDepthRange(near_value, far_value);
}
#define glDepthRangef dm_compat_glDepthRangef

#ifndef _WIN32
// Framebuffer objects are exposed as EXT in a 2.1 context on macOS.
#define glBindFramebuffer glBindFramebufferEXT
#define glBindRenderbuffer glBindRenderbufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glDeleteFramebuffers glDeleteFramebuffersEXT
#define glDeleteRenderbuffers glDeleteRenderbuffersEXT
#define glFramebufferRenderbuffer glFramebufferRenderbufferEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glGenFramebuffers glGenFramebuffersEXT
#define glGenRenderbuffers glGenRenderbuffersEXT
#define glRenderbufferStorage glRenderbufferStorageEXT
#else
// opengl32.dll only exports GL 1.1. Resolve the rest through SDL after the
// context is created. Calls are macro-redirected to these pointers.
#define DM_GL_POINTERS(X)                                                       \
  X(glActiveTexture, PFNGLACTIVETEXTUREPROC)                                   \
  X(glAttachShader, PFNGLATTACHSHADERPROC)                                     \
  X(glBindBuffer, PFNGLBINDBUFFERPROC)                                         \
  X(glBindFramebufferEXT, PFNGLBINDFRAMEBUFFEREXTPROC)                         \
  X(glBindRenderbufferEXT, PFNGLBINDRENDERBUFFEREXTPROC)                       \
  X(glBufferData, PFNGLBUFFERDATAPROC)                                         \
  X(glCheckFramebufferStatusEXT, PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)           \
  X(glCompileShader, PFNGLCOMPILESHADERPROC)                                   \
  X(glCreateProgram, PFNGLCREATEPROGRAMPROC)                                   \
  X(glCreateShader, PFNGLCREATESHADERPROC)                                     \
  X(glDeleteBuffers, PFNGLDELETEBUFFERSPROC)                                   \
  X(glDeleteFramebuffersEXT, PFNGLDELETEFRAMEBUFFERSEXTPROC)                   \
  X(glDeleteRenderbuffersEXT, PFNGLDELETERENDERBUFFERSEXTPROC)                 \
  X(glDisableVertexAttribArray, PFNGLDISABLEVERTEXATTRIBARRAYPROC)             \
  X(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC)               \
  X(glFramebufferRenderbufferEXT, PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)         \
  X(glFramebufferTexture2DEXT, PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)               \
  X(glGenBuffers, PFNGLGENBUFFERSPROC)                                         \
  X(glGenFramebuffersEXT, PFNGLGENFRAMEBUFFERSEXTPROC)                         \
  X(glGenRenderbuffersEXT, PFNGLGENRENDERBUFFERSEXTPROC)                       \
  X(glGetAttribLocation, PFNGLGETATTRIBLOCATIONPROC)                           \
  X(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC)                           \
  X(glGetProgramiv, PFNGLGETPROGRAMIVPROC)                                     \
  X(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC)                             \
  X(glGetShaderiv, PFNGLGETSHADERIVPROC)                                       \
  X(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC)                         \
  X(glLinkProgram, PFNGLLINKPROGRAMPROC)                                       \
  X(glRenderbufferStorageEXT, PFNGLRENDERBUFFERSTORAGEEXTPROC)                 \
  X(glShaderSource, PFNGLSHADERSOURCEPROC)                                     \
  X(glUniform1f, PFNGLUNIFORM1FPROC)                                           \
  X(glUniform1i, PFNGLUNIFORM1IPROC)                                           \
  X(glUniform2f, PFNGLUNIFORM2FPROC)                                           \
  X(glUniform3f, PFNGLUNIFORM3FPROC)                                           \
  X(glUniform3fv, PFNGLUNIFORM3FVPROC)                                         \
  X(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC)                             \
  X(glUseProgram, PFNGLUSEPROGRAMPROC)                                         \
  X(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC)
#define DM_GL_DECLARE(name, type) extern type dm_gl_p_##name;
DM_GL_POINTERS(DM_GL_DECLARE)
#undef DM_GL_DECLARE
#define glActiveTexture ((PFNGLACTIVETEXTUREPROC)dm_gl_p_glActiveTexture)
#define glAttachShader ((PFNGLATTACHSHADERPROC)dm_gl_p_glAttachShader)
#define glBindBuffer ((PFNGLBINDBUFFERPROC)dm_gl_p_glBindBuffer)
#define glBufferData ((PFNGLBUFFERDATAPROC)dm_gl_p_glBufferData)
#define glCompileShader ((PFNGLCOMPILESHADERPROC)dm_gl_p_glCompileShader)
#define glCreateProgram ((PFNGLCREATEPROGRAMPROC)dm_gl_p_glCreateProgram)
#define glCreateShader ((PFNGLCREATESHADERPROC)dm_gl_p_glCreateShader)
#define glDeleteBuffers ((PFNGLDELETEBUFFERSPROC)dm_gl_p_glDeleteBuffers)
#define glDisableVertexAttribArray                                            \
  ((PFNGLDISABLEVERTEXATTRIBARRAYPROC)dm_gl_p_glDisableVertexAttribArray)
#define glEnableVertexAttribArray                                             \
  ((PFNGLENABLEVERTEXATTRIBARRAYPROC)dm_gl_p_glEnableVertexAttribArray)
#define glGenBuffers ((PFNGLGENBUFFERSPROC)dm_gl_p_glGenBuffers)
#define glGetAttribLocation ((PFNGLGETATTRIBLOCATIONPROC)dm_gl_p_glGetAttribLocation)
#define glGetProgramInfoLog ((PFNGLGETPROGRAMINFOLOGPROC)dm_gl_p_glGetProgramInfoLog)
#define glGetProgramiv ((PFNGLGETPROGRAMIVPROC)dm_gl_p_glGetProgramiv)
#define glGetShaderInfoLog ((PFNGLGETSHADERINFOLOGPROC)dm_gl_p_glGetShaderInfoLog)
#define glGetShaderiv ((PFNGLGETSHADERIVPROC)dm_gl_p_glGetShaderiv)
#define glGetUniformLocation ((PFNGLGETUNIFORMLOCATIONPROC)dm_gl_p_glGetUniformLocation)
#define glLinkProgram ((PFNGLLINKPROGRAMPROC)dm_gl_p_glLinkProgram)
#define glShaderSource ((PFNGLSHADERSOURCEPROC)dm_gl_p_glShaderSource)
#define glUniform1f ((PFNGLUNIFORM1FPROC)dm_gl_p_glUniform1f)
#define glUniform1i ((PFNGLUNIFORM1IPROC)dm_gl_p_glUniform1i)
#define glUniform2f ((PFNGLUNIFORM2FPROC)dm_gl_p_glUniform2f)
#define glUniform3f ((PFNGLUNIFORM3FPROC)dm_gl_p_glUniform3f)
#define glUniform3fv ((PFNGLUNIFORM3FVPROC)dm_gl_p_glUniform3fv)
#define glUniformMatrix4fv ((PFNGLUNIFORMMATRIX4FVPROC)dm_gl_p_glUniformMatrix4fv)
#define glUseProgram ((PFNGLUSEPROGRAMPROC)dm_gl_p_glUseProgram)
#define glVertexAttribPointer ((PFNGLVERTEXATTRIBPOINTERPROC)dm_gl_p_glVertexAttribPointer)
#define glBindFramebuffer ((PFNGLBINDFRAMEBUFFEREXTPROC)dm_gl_p_glBindFramebufferEXT)
#define glBindRenderbuffer ((PFNGLBINDRENDERBUFFEREXTPROC)dm_gl_p_glBindRenderbufferEXT)
#define glCheckFramebufferStatus ((PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)dm_gl_p_glCheckFramebufferStatusEXT)
#define glDeleteFramebuffers ((PFNGLDELETEFRAMEBUFFERSEXTPROC)dm_gl_p_glDeleteFramebuffersEXT)
#define glDeleteRenderbuffers ((PFNGLDELETERENDERBUFFERSEXTPROC)dm_gl_p_glDeleteRenderbuffersEXT)
#define glFramebufferRenderbuffer ((PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)dm_gl_p_glFramebufferRenderbufferEXT)
#define glFramebufferTexture2D ((PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)dm_gl_p_glFramebufferTexture2DEXT)
#define glGenFramebuffers ((PFNGLGENFRAMEBUFFERSEXTPROC)dm_gl_p_glGenFramebuffersEXT)
#define glGenRenderbuffers ((PFNGLGENRENDERBUFFERSEXTPROC)dm_gl_p_glGenRenderbuffersEXT)
#define glRenderbufferStorage ((PFNGLRENDERBUFFERSTORAGEEXTPROC)dm_gl_p_glRenderbufferStorageEXT)
#endif
#else
#include <GLES2/gl2.h>
#endif

#endif
