#include "Platform/GlLoader.h"

#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <string>
#include <utility>

// La tabla de punteros de GL de la CL0.3, ahora fuera de main.cpp.
namespace Client::Platform
{
namespace
{
#ifdef _WIN32
// SDL busca en opengl32.dll solo cuando wglGetProcAddress da nulo (WIN_GL_GetProcAddress, en
// SDL_windowsopengl.c). Con los drivers que dan 1, 2, 3 o -1, las funciones de la 1.1
// (glGetString, glClear) quedarían afuera aunque opengl32.dll las exporta: se piden ahí.
SDL_FunctionPointer LoadFromOpenGl32(const char* name)
{
    SDL_SharedObject* opengl32 = SDL_LoadObject("opengl32.dll");
    if (opengl32 == nullptr)
    {
        return nullptr;
    }
    const SDL_FunctionPointer address = SDL_LoadFunction(opengl32, name);
    // La DLL sigue cargada: SDL tiene su propia referencia mientras viva la ventana GL.
    SDL_UnloadObject(opengl32);
    return address;
}
#endif

template <typename Function> bool LoadGlFunction(Function& function, const char* name, std::string& error)
{
    SDL_FunctionPointer address = SDL_GL_GetProcAddress(name);
#ifdef _WIN32
    if (!IsValidProcAddress(reinterpret_cast<std::uintptr_t>(address)))
    {
        address = LoadFromOpenGl32(name);
    }
#endif
    if (!IsValidProcAddress(reinterpret_cast<std::uintptr_t>(address)))
    {
        error = std::string("el driver no tiene ") + name;
        return false;
    }
    function = reinterpret_cast<Function>(address);
    return true;
}
} // namespace

bool IsValidProcAddress(std::uintptr_t address)
{
    // La última dirección chica que esos drivers de Windows dan por una función que no tienen.
    constexpr std::uintptr_t LastInvalidAddress = 3;
    return address > LastInvalidAddress && address != UINTPTR_MAX;
}

bool HasRequiredGlVersion(int major, int minor)
{
    return major > GlMajorVersion || (major == GlMajorVersion && minor >= GlMinorVersion);
}

bool IsCoreProfile(int profileMask)
{
    return (profileMask & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
}

Core::Expected<GlFunctions, std::string> LoadGlFunctions()
{
    GlFunctions gl;
    std::string error;
    const bool loaded =
        LoadGlFunction(gl.getString, "glGetString", error) && LoadGlFunction(gl.getIntegerv, "glGetIntegerv", error) &&
        LoadGlFunction(gl.viewport, "glViewport", error) && LoadGlFunction(gl.clearColor, "glClearColor", error) &&
        LoadGlFunction(gl.clear, "glClear", error);
    if (!loaded)
    {
        return Core::Unexpected(std::move(error));
    }
    return gl;
}

const char* GlString(const GlFunctions& gl, GLenum name)
{
    const GLubyte* value = gl.getString(name);
    return value != nullptr ? reinterpret_cast<const char*>(value) : "(sin dato)";
}
} // namespace Client::Platform
