#include "LabGl.h"

#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <string>
#include <utility>

namespace Labs
{
namespace
{
#ifdef _WIN32
// El mismo reintento que LoadFromOpenGl32 de Platform/GlLoader.cpp, que es interno: con los drivers
// que dan 1, 2, 3 o -1 por una función de la 1.1 (glGetError, glBindTexture), SDL no busca en
// opengl32.dll y hay que pedirla ahí. La CL5.1 lo deja en un solo lugar, en Platform.
SDL_FunctionPointer LoadFromOpenGl32(const char* name)
{
    SDL_SharedObject* opengl32 = SDL_LoadObject("opengl32.dll");
    if (opengl32 == nullptr)
    {
        return nullptr;
    }
    const SDL_FunctionPointer address = SDL_LoadFunction(opengl32, name);
    SDL_UnloadObject(opengl32);
    return address;
}
#endif

// Como LoadGlFunction de Platform/GlLoader.cpp.
template <typename Function> bool LoadOne(Function& function, const char* name, std::string& error)
{
    SDL_FunctionPointer address = SDL_GL_GetProcAddress(name);
#ifdef _WIN32
    if (!Client::Platform::IsValidProcAddress(reinterpret_cast<std::uintptr_t>(address)))
    {
        address = LoadFromOpenGl32(name);
    }
#endif
    if (!Client::Platform::IsValidProcAddress(reinterpret_cast<std::uintptr_t>(address)))
    {
        error = std::string("el driver no tiene ") + name;
        return false;
    }
    function = reinterpret_cast<Function>(address);
    return true;
}
} // namespace

Client::Core::Expected<LabGl, std::string> LoadLabGl(const Client::Platform::GlFunctions& base)
{
    LabGl gl;
    static_cast<Client::Platform::GlFunctions&>(gl) = base;
    std::string error;
    // Una línea por función de la lista: la primera que falta corta.
#define MURET_LAB_GL_LOAD(type, member, name)                                                                          \
    if (!LoadOne(gl.member, name, error))                                                                              \
    {                                                                                                                  \
        return Client::Core::Unexpected(std::move(error));                                                             \
    }
    MURET_LAB_GL_FUNCTIONS(MURET_LAB_GL_LOAD)
#undef MURET_LAB_GL_LOAD
    return gl;
}
} // namespace Labs
