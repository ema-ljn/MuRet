#pragma once

#include "Core/Expected.h"
#include "Platform/GlLoader.h"

#include <string>

// Las funciones de OpenGL que el lab de la CL5.0 pide además de las cinco de la CL0.3
// (Client::Platform::GlFunctions): la mezcla, glReadPixels, texturas, framebuffers, shaders,
// buffers y el VAO. Es la X-macro de la pregunta 5 del plan de la CL5, en chico: una línea por
// función, con su typedef PFNGL...PROC, el nombre del miembro y el de GL, arma el miembro del
// struct y su pedido en LoadLabGl. La CL5.1 lleva la misma forma a Platform/GlFunctionList.h; esta
// lista, con lo que usa cada una el lab, es su punto de partida.
#define MURET_LAB_GL_FUNCTIONS(X)                                                                                      \
    X(PFNGLGETERRORPROC, getError, "glGetError")                                                                       \
    X(PFNGLENABLEPROC, enable, "glEnable")                                                                             \
    X(PFNGLDISABLEPROC, disable, "glDisable")                                                                          \
    X(PFNGLBLENDFUNCPROC, blendFunc, "glBlendFunc")                                                                    \
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
    X(PFNGLGENVERTEXARRAYSPROC, genVertexArrays, "glGenVertexArrays")                                                  \
    X(PFNGLDELETEVERTEXARRAYSPROC, deleteVertexArrays, "glDeleteVertexArrays")                                         \
    X(PFNGLBINDVERTEXARRAYPROC, bindVertexArray, "glBindVertexArray")                                                  \
    X(PFNGLGENBUFFERSPROC, genBuffers, "glGenBuffers")                                                                 \
    X(PFNGLDELETEBUFFERSPROC, deleteBuffers, "glDeleteBuffers")                                                        \
    X(PFNGLBINDBUFFERPROC, bindBuffer, "glBindBuffer")                                                                 \
    X(PFNGLBUFFERDATAPROC, bufferData, "glBufferData")                                                                 \
    X(PFNGLVERTEXATTRIBPOINTERPROC, vertexAttribPointer, "glVertexAttribPointer")                                      \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, enableVertexAttribArray, "glEnableVertexAttribArray")                          \
    X(PFNGLDRAWELEMENTSPROC, drawElements, "glDrawElements")

namespace Labs
{
// La tabla de la CL0.3 (getString, getIntegerv, viewport, clearColor, clear) y las de arriba. Un
// LabGl{} tiene todos los punteros en nulo.
struct LabGl : Client::Platform::GlFunctions
{
#define MURET_LAB_GL_MEMBER(type, member, name) type member = nullptr;
    MURET_LAB_GL_FUNCTIONS(MURET_LAB_GL_MEMBER)
#undef MURET_LAB_GL_MEMBER
};

// Pide las de MURET_LAB_GL_FUNCTIONS al contexto actual, sobre las de la CL0.3 que ya trae `base`,
// con los mismos chequeos que LoadGlFunctions (también el reintento en opengl32.dll de Windows). El
// error nombra la primera que falta.
[[nodiscard]] Client::Core::Expected<LabGl, std::string> LoadLabGl(const Client::Platform::GlFunctions& base);
} // namespace Labs
