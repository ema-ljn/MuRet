#pragma once

// La lista de las funciones de OpenGL que pide el cliente (D-C2 del roadmap, pregunta 5 del plan de
// la CL5), como una X-macro: una línea por función, con su typedef PFNGL...PROC, el nombre del
// miembro de GlFunctions y el de GL. GlLoader.h la expande para armar el struct, el pedido de
// LoadGlFunctions y la lista de nombres que prueba test_gl_loader.cpp. Una función nueva es una
// línea acá, en el grupo de quien la usa.
//
// No es glad ni GLEW: el puerto glad de vcpkg trae el perfil de compatibilidad y pide Python, y un
// glad.c generado son miles de líneas con su propia forma de actualizarse. Con 51 funciones, la
// tabla propia son pocas líneas, y el pedido (SDL_GL_GetProcAddress, el reintento en opengl32.dll)
// ya estaba escrito desde la CL0.3 (CODING_RULES §10). Salió de la del lab de la CL5.0
// (labs/cl5.0-pipeline/gl/LabGl.h), que tenía 41: el lab se queda solo con glDrawElements, que el
// cliente no usa (dibuja con glDrawArrays, de a dos triángulos por quad).
//
// Las dos primeras van aparte: con ellas se pregunta qué contexto dio el driver. Son de la 1.0,
// así que las tiene cualquiera, y LoadGlFunctions las pide antes que el resto: un driver viejo dice
// su versión, y no la primera función de la 3.3 que le falta.
#define MURET_GL_QUERY_FUNCTIONS(X)                                                                                    \
    X(PFNGLGETSTRINGPROC, getString, "glGetString")                                                                    \
    X(PFNGLGETINTEGERVPROC, getIntegerv, "glGetIntegerv")

// El resto, en el orden de quién lo usa:
//   glViewport, glClearColor, glClear     el frame de Main (CL0.3) y el viewport de GlRenderer
//   glGetError                            los tests, después de cada paso
//   glEnable, glDisable, glBlendFunc      la mezcla (el lab de la CL5.0 y GlSpriteRenderer)
//   glDepthFunc, glDepthMask              la profundidad del pase 3D (GlModelRenderer, CL6.3)
//   glPixelStorei                         GL_UNPACK_ALIGNMENT al subir una textura (GlTexture)
//   glReadPixels                          leer lo dibujado (GlFramebuffer)
//   glGenTextures a glTexParameteri       las texturas (GlTexture y el color de GlFramebuffer)
//   glGenFramebuffers a glCheck...        el framebuffer de los tests (GlFramebuffer)
//   glCreateShader a glUniform1i          los shaders, el programa, la matriz y las banderas del
//                                         fragment shader (GlShader y GlRenderer)
//   glUniform1f, glUniform3f              la niebla del 3D: el inicio, el fin y el color (GlModelRenderer)
//   glGenVertexArrays a glDrawArrays      el VAO, el VBO y el dibujo (GlRenderer)
#define MURET_GL_RENDER_FUNCTIONS(X)                                                                                   \
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

// Todas, en el orden en que se piden.
#define MURET_GL_FUNCTIONS(X) MURET_GL_QUERY_FUNCTIONS(X) MURET_GL_RENDER_FUNCTIONS(X)
