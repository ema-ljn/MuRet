#pragma once

#include "Core/Expected.h"
#include "PlatformApi.h"

// SDL_opengl.h includes <windows.h> (and its near, far and ERROR macros) only if APIENTRY is undefined. It is defined
// with windows.h's value and removed after: GL only uses it in declarations. So GL's types and constants come from
// this header, which re-exports them, and no other file includes SDL_opengl.h.
#if defined(_WIN32) && !defined(APIENTRY)
#define APIENTRY __stdcall
#define MU_GL_LOADER_DEFINED_APIENTRY
#endif
#include <SDL3/SDL_opengl.h> // IWYU pragma: export
#ifdef MU_GL_LOADER_DEFINED_APIENTRY
#undef APIENTRY
#undef APIENTRYP
#undef MU_GL_LOADER_DEFINED_APIENTRY
#endif

#include "Platform/Sdl/GlFunctionList.h"

#include <SDL3/SDL_stdinc.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// OpenGL functions are not linked like a library's: the driver gives them at runtime, and only once there is a
// context. GlFunctionList.h's X-macro builds the table, requested from SDL_GL_GetProcAddress (D-C2: no GLAD or GLEW).
// Each pointer's type is its PFNGL…PROC typedef, with GL's calling convention: SDL_opengl.h declares the 1.x ones with
// SDL_OPENGL_1_FUNCTION_TYPEDEFS (PUBLIC in Platform, Client/CMakeLists.txt) and SDL_opengl_glext.h the 3.3 ones
// (shaders, buffers), always.
namespace Client::Platform::Sdl
{
// OpenGL 3.3 core (D-C3): the version requested before creating the window and the minimum accepted from the driver.
inline constexpr int GlMajorVersion = 3;
inline constexpr int GlMinorVersion = 3;

[[nodiscard]] PLATFORM_API const std::string& GlVersionText();

// A GlFunctions{} has them all null.
struct GlFunctions
{
#define MU_GL_MEMBER(type, member, name) type member = nullptr;
    MU_GL_FUNCTIONS(MU_GL_MEMBER)
#undef MU_GL_MEMBER
};

// In request order.
#define MU_GL_NAME(type, member, name) std::string_view{name},
inline constexpr std::array GlFunctionNames{MU_GL_FUNCTIONS(MU_GL_NAME)};
#undef MU_GL_NAME

// On Windows some drivers return 1, 2, 3 or -1 instead of null for a function they lack (or for a 1.1 one, which
// wglGetProcAddress does not give), and SDL does not filter them: it looks in opengl32.dll only when wglGetProcAddress
// gives null (SDL_windowsopengl.c, WIN_GL_GetProcAddress). So GlProcAddress retries there. GLX is the opposite: any
// name gives a non-null pointer, so this check guarantees nothing there; the context version does.
[[nodiscard]] PLATFORM_API bool IsValidProcAddress(std::uintptr_t address);

// The driver can give a newer context than requested (Mesa gives 4.5 core), never an older one.
[[nodiscard]] PLATFORM_API bool HasRequiredGlVersion(int major, int minor);

// A 3.3 "Compatibility Profile" is not enough (D-C3), and on Windows, without wglCreateContextAttribsARB, SDL gives a
// legacy context and only sets the error (SDL_windowsopengl.c).
[[nodiscard]] PLATFORM_API bool IsCoreProfile(int profileMask);

// With the Windows retry in opengl32.dll; null if not valid (IsValidProcAddress).
[[nodiscard]] PLATFORM_API SDL_FunctionPointer GlProcAddress(const char* name);

// GlProcAddress, or a fake driver that lets the tests (test_gl_loader.cpp) check LoadGlFunctions' order without a
// window. Null if missing.
using GlProcLookup = SDL_FunctionPointer (*)(const char* name);

// If the driver lacks it, `error` names it and it returns false.
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

// With a current context: requests the two MU_GL_QUERY_FUNCTIONS, checks that the context is 3.3 or more and core,
// and only then requests the rest. The error says what was missing: the driver's version, the profile or the first
// missing function.
[[nodiscard]] PLATFORM_API Core::Expected<GlFunctions, std::string>
LoadGlFunctions(GlProcLookup lookup = GlProcAddress);

// "(sin dato)" if the driver gives null.
[[nodiscard]] PLATFORM_API const char* GlString(const GlFunctions& gl, GLenum name);
} // namespace Client::Platform::Sdl
