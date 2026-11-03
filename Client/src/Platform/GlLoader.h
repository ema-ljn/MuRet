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

#include "Platform/GlFunctionList.h"

#include <SDL3/SDL_stdinc.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// Las funciones de OpenGL no se enlazan como las de una biblioteca: las da el driver, en tiempo
// de ejecución, y recién cuando hay un contexto. La tabla, que en la CL0.3 eran cinco, la arma la
// X-macro de GlFunctionList.h (CL5.1), y se pide a SDL_GL_GetProcAddress (D-C2: sin GLAD ni GLEW).
// El tipo de cada puntero es su typedef PFNGL…PROC, con la convención de llamada de GL: los de la
// 1.x los declara SDL_opengl.h con SDL_OPENGL_1_FUNCTION_TYPEDEFS (PUBLIC en ClientPlatform,
// Client/CMakeLists.txt) y los de la 3.3 (shaders, buffers) SDL_opengl_glext.h, siempre.
namespace Client::Platform
{
// OpenGL 3.3 core (D-C3 del roadmap): la versión que se pide antes de crear la ventana y la mínima
// que se acepta del driver.
inline constexpr int GlMajorVersion = 3;
inline constexpr int GlMinorVersion = 3;

// "3.3", para los mensajes de error.
[[nodiscard]] const std::string& GlVersionText();

// Un puntero por función de MURET_GL_FUNCTIONS. Un GlFunctions{} los tiene todos en nulo.
struct GlFunctions
{
#define MURET_GL_MEMBER(type, member, name) type member = nullptr;
    MURET_GL_FUNCTIONS(MURET_GL_MEMBER)
#undef MURET_GL_MEMBER
};

// Los nombres de GL de la tabla, en el orden en que se piden.
#define MURET_GL_NAME(type, member, name) std::string_view{name},
inline constexpr std::array GlFunctionNames{MURET_GL_FUNCTIONS(MURET_GL_NAME)};
#undef MURET_GL_NAME

// En Windows, algunos drivers devuelven 1, 2, 3 o -1 en vez de nulo para una función que no
// tienen (o para una de la 1.1, que wglGetProcAddress no da), y SDL no los filtra: busca en
// opengl32.dll solo cuando wglGetProcAddress da nulo (SDL_windowsopengl.c, WIN_GL_GetProcAddress).
// Por eso GlProcAddress reintenta ahí. En GLX pasa lo contrario: cualquier nombre da un puntero
// no nulo, así que este chequeo no garantiza nada ahí; lo que garantiza es la versión del contexto.
[[nodiscard]] bool IsValidProcAddress(std::uintptr_t address);

// El driver puede dar un contexto más nuevo que el pedido (Mesa da 4.5 core), nunca más viejo:
// 3.3 o más.
[[nodiscard]] bool HasRequiredGlVersion(int major, int minor);

// Si el GL_CONTEXT_PROFILE_MASK del contexto trae GL_CONTEXT_CORE_PROFILE_BIT. Un 3.3
// "Compatibility Profile" no alcanza (D-C3), y en Windows, sin wglCreateContextAttribsARB, SDL da
// un contexto legado y solo deja el error (SDL_windowsopengl.c).
[[nodiscard]] bool IsCoreProfile(int profileMask);

// La dirección de `name` en el contexto actual (con el reintento en opengl32.dll de Windows), o
// nulo si no es válida (IsValidProcAddress).
[[nodiscard]] SDL_FunctionPointer GlProcAddress(const char* name);

// Cómo se pide una función por su nombre, nulo si no está: GlProcAddress, o un driver falso en los
// tests (test_gl_loader.cpp), que así prueban el orden de LoadGlFunctions sin ventana.
using GlProcLookup = SDL_FunctionPointer (*)(const char* name);

// Pide `name` y lo deja en `function`, con su tipo PFNGL...PROC. Si el driver no la tiene, deja en
// `error` cuál falta y devuelve false. Es el pedido de cada función de LoadGlFunctions, y el lab de
// la CL5.0 lo usa para la suya (labs/cl5.0-pipeline/gl/LabGl.cpp).
template <typename Function>
[[nodiscard]] bool LoadGlFunction(Function& function, const char* name, std::string& error,
                                  GlProcLookup lookup = GlProcAddress)
{
    const SDL_FunctionPointer address = lookup(name);
    if (address == nullptr)
    {
        error = std::string("el driver no tiene ") + name;
        return false;
    }
    function = reinterpret_cast<Function>(address);
    return true;
}

// Con un contexto actual: pide las dos de MURET_GL_QUERY_FUNCTIONS, mira que el contexto sea 3.3 o
// más y core, y recién ahí pide el resto. El error dice qué faltó: la versión que dio el driver, el
// perfil o la primera función que no tiene.
[[nodiscard]] Core::Expected<GlFunctions, std::string> LoadGlFunctions(GlProcLookup lookup = GlProcAddress);

// glGetString como texto, o "(sin dato)" si el driver da nulo.
[[nodiscard]] const char* GlString(const GlFunctions& gl, GLenum name);
} // namespace Client::Platform
