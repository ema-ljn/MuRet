#pragma once

#include "Core/Expected.h"

// SDL_opengl.h incluye <windows.h> (y sus macros near, far, ERROR) solo si APIENTRY no está
// definido. Se define con el valor de windows.h y se saca después: GL lo usa solo al declarar.
#if defined(_WIN32) && !defined(APIENTRY)
#define APIENTRY __stdcall
#define MURET_GL_LOADER_DEFINED_APIENTRY
#endif
#include <SDL3/SDL_opengl.h>
#ifdef MURET_GL_LOADER_DEFINED_APIENTRY
#undef APIENTRY
#undef APIENTRYP
#undef MURET_GL_LOADER_DEFINED_APIENTRY
#endif

#include <cstdint>
#include <string>

// Las funciones de OpenGL no se enlazan como las de una biblioteca: las da el driver, en tiempo
// de ejecución, y recién cuando hay un contexto. Esta es la tabla mínima de la CL0.3, pedida a
// SDL_GL_GetProcAddress (D-C2: sin GLAD ni GLEW). El tipo de cada puntero es su typedef
// PFNGL…PROC, con la convención de llamada de GL: los de la 1.x los declara SDL_opengl.h con
// SDL_OPENGL_1_FUNCTION_TYPEDEFS (PUBLIC en ClientPlatform, Client/CMakeLists.txt) y los de la 3.3
// (shaders, buffers) SDL_opengl_glext.h, siempre.
namespace Client::Platform
{
// OpenGL 3.3 core (D-C3 del roadmap): la versión que se pide antes de crear la ventana y la mínima
// que se acepta del driver.
inline constexpr int GlMajorVersion = 3;
inline constexpr int GlMinorVersion = 3;

struct GlFunctions
{
    PFNGLGETSTRINGPROC getString = nullptr;
    PFNGLGETINTEGERVPROC getIntegerv = nullptr;
    PFNGLVIEWPORTPROC viewport = nullptr;
    PFNGLCLEARCOLORPROC clearColor = nullptr;
    PFNGLCLEARPROC clear = nullptr;
};

// En Windows, algunos drivers devuelven 1, 2, 3 o -1 en vez de nulo para una función que no
// tienen (o para una de la 1.1, que wglGetProcAddress no da), y SDL no los filtra: busca en
// opengl32.dll solo cuando wglGetProcAddress da nulo (SDL_windowsopengl.c, WIN_GL_GetProcAddress).
// Por eso LoadGlFunctions reintenta ahí. En GLX pasa lo contrario: cualquier nombre da un puntero
// no nulo, así que este chequeo no garantiza nada ahí; lo que garantiza es la versión del contexto.
[[nodiscard]] bool IsValidProcAddress(std::uintptr_t address);

// El driver puede dar un contexto más nuevo que el pedido (Mesa da 4.5 core), nunca más viejo:
// 3.3 o más.
[[nodiscard]] bool HasRequiredGlVersion(int major, int minor);

// Si el GL_CONTEXT_PROFILE_MASK del contexto trae GL_CONTEXT_CORE_PROFILE_BIT. Un 3.3
// "Compatibility Profile" no alcanza (D-C3), y en Windows, sin wglCreateContextAttribsARB, SDL da
// un contexto legado y solo deja el error (SDL_windowsopengl.c).
[[nodiscard]] bool IsCoreProfile(int profileMask);

// Pide cada función de la tabla al contexto actual. El error nombra la primera que falta.
[[nodiscard]] Core::Expected<GlFunctions, std::string> LoadGlFunctions();

// glGetString como texto, o "(sin dato)" si el driver da nulo.
[[nodiscard]] const char* GlString(const GlFunctions& gl, GLenum name);
} // namespace Client::Platform
