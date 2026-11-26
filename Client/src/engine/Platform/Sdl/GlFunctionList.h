#pragma once

// An X-macro: one line per function, with its PFNGL...PROC typedef, the GlFunctions member name and the GL name.
// GlLoader.h expands it into the struct, the LoadGlFunctions requests and the name list test_gl_loader.cpp checks. Not
// glad or GLEW: vcpkg's glad port brings the compatibility profile and needs Python, and a generated glad.c is
// thousands of lines with its own update path; with 51 functions the own table is a few lines. The first two go apart:
// they ask which context the driver gave. They are GL 1.0, so any driver has them, and LoadGlFunctions requests them
// first: an old driver reports its version, not the first 3.3 function it lacks.
#define MU_GL_QUERY_FUNCTIONS(X)                                                                                       \
    X(PFNGLGETSTRINGPROC, getString, "glGetString")                                                                    \
    X(PFNGLGETINTEGERVPROC, getIntegerv, "glGetIntegerv")

// The rest, in the order of the code that uses them.
#define MU_GL_RENDER_FUNCTIONS(X)                                                                                      \
    X(PFNGLVIEWPORTPROC, viewport, "glViewport")                                                                       \
    X(PFNGLCLEARCOLORPROC, clearColor, "glClearColor")                                                                 \
    X(PFNGLCLEARPROC, clear, "glClear")                                                                                \
    X(PFNGLGETERRORPROC, getError, "glGetError")                                                                       \
    X(PFNGLENABLEPROC, enable, "glEnable")                                                                             \
    X(PFNGLDISABLEPROC, disable, "glDisable")                                                                          \
    X(PFNGLBLENDFUNCPROC, blendFunc, "glBlendFunc")                                                                    \
    X(PFNGLDEPTHFUNCPROC, depthFunc, "glDepthFunc")                                                                    \
    X(PFNGLDEPTHMASKPROC, depthMask, "glDepthMask")                                                                    \
    X(PFNGLPIXELSTOREIPROC, pixelStorei, "glPixelStorei")                                                              \
    X(PFNGLREADPIXELSPROC, readPixels, "glReadPixels")                                                                 \
    X(PFNGLGENTEXTURESPROC, genTextures, "glGenTextures")                                                              \
    X(PFNGLDELETETEXTURESPROC, deleteTextures, "glDeleteTextures")                                                     \
    X(PFNGLBINDTEXTUREPROC, bindTexture, "glBindTexture")                                                              \
    X(PFNGLTEXIMAGE2DPROC, texImage2D, "glTexImage2D")                                                                 \
    X(PFNGLTEXPARAMETERIPROC, texParameteri, "glTexParameteri")                                                        \
    X(PFNGLGENFRAMEBUFFERSPROC, genFramebuffers, "glGenFramebuffers")                                                  \
    X(PFNGLDELETEFRAMEBUFFERSPROC, deleteFramebuffers, "glDeleteFramebuffers")                                         \
    X(PFNGLBINDFRAMEBUFFERPROC, bindFramebuffer, "glBindFramebuffer")                                                  \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, framebufferTexture2D, "glFramebufferTexture2D")                                   \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, checkFramebufferStatus, "glCheckFramebufferStatus")                             \
    X(PFNGLCREATESHADERPROC, createShader, "glCreateShader")                                                           \
    X(PFNGLSHADERSOURCEPROC, shaderSource, "glShaderSource")                                                           \
    X(PFNGLCOMPILESHADERPROC, compileShader, "glCompileShader")                                                        \
    X(PFNGLGETSHADERIVPROC, getShaderiv, "glGetShaderiv")                                                              \
    X(PFNGLGETSHADERINFOLOGPROC, getShaderInfoLog, "glGetShaderInfoLog")                                               \
    X(PFNGLDELETESHADERPROC, deleteShader, "glDeleteShader")                                                           \
    X(PFNGLCREATEPROGRAMPROC, createProgram, "glCreateProgram")                                                        \
    X(PFNGLATTACHSHADERPROC, attachShader, "glAttachShader")                                                           \
    X(PFNGLLINKPROGRAMPROC, linkProgram, "glLinkProgram")                                                              \
    X(PFNGLGETPROGRAMIVPROC, getProgramiv, "glGetProgramiv")                                                           \
    X(PFNGLGETPROGRAMINFOLOGPROC, getProgramInfoLog, "glGetProgramInfoLog")                                            \
    X(PFNGLDELETEPROGRAMPROC, deleteProgram, "glDeleteProgram")                                                        \
    X(PFNGLUSEPROGRAMPROC, useProgram, "glUseProgram")                                                                 \
    X(PFNGLGETUNIFORMLOCATIONPROC, getUniformLocation, "glGetUniformLocation")                                         \
    X(PFNGLUNIFORMMATRIX4FVPROC, uniformMatrix4fv, "glUniformMatrix4fv")                                               \
    X(PFNGLUNIFORM1IPROC, uniform1i, "glUniform1i")                                                                    \
    X(PFNGLUNIFORM1FPROC, uniform1f, "glUniform1f")                                                                    \
    X(PFNGLUNIFORM3FPROC, uniform3f, "glUniform3f")                                                                    \
    X(PFNGLGENVERTEXARRAYSPROC, genVertexArrays, "glGenVertexArrays")                                                  \
    X(PFNGLDELETEVERTEXARRAYSPROC, deleteVertexArrays, "glDeleteVertexArrays")                                         \
    X(PFNGLBINDVERTEXARRAYPROC, bindVertexArray, "glBindVertexArray")                                                  \
    X(PFNGLGENBUFFERSPROC, genBuffers, "glGenBuffers")                                                                 \
    X(PFNGLDELETEBUFFERSPROC, deleteBuffers, "glDeleteBuffers")                                                        \
    X(PFNGLBINDBUFFERPROC, bindBuffer, "glBindBuffer")                                                                 \
    X(PFNGLBUFFERDATAPROC, bufferData, "glBufferData")                                                                 \
    X(PFNGLVERTEXATTRIBPOINTERPROC, vertexAttribPointer, "glVertexAttribPointer")                                      \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, enableVertexAttribArray, "glEnableVertexAttribArray")                          \
    X(PFNGLDRAWARRAYSPROC, drawArrays, "glDrawArrays")

// All of them, in request order.
#define MU_GL_FUNCTIONS(X) MU_GL_QUERY_FUNCTIONS(X) MU_GL_RENDER_FUNCTIONS(X)
