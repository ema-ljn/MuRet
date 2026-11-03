#include "Platform/GlLoader.h"

#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <string>
#include <utility>

// La tabla de punteros de GL: la de la CL0.3, que desde la CL5.1 arma GlFunctionList.h.
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

// La versión y el perfil del contexto actual, con las dos de MURET_GL_QUERY_FUNCTIONS. Vacío si
// sirve. GL_VERSION va también en el mensaje: antes de la 3.0, GL_MAJOR_VERSION no existe y da 0.0.
std::string CheckContext(const GlFunctions& gl)
{
    GLint major = 0;
    GLint minor = 0;
    gl.getIntegerv(GL_MAJOR_VERSION, &major);
    gl.getIntegerv(GL_MINOR_VERSION, &minor);
    const std::string driverVersion = GlString(gl, GL_VERSION);
    if (!HasRequiredGlVersion(major, minor))
    {
        return "se pidió OpenGL " + GlVersionText() + " y el driver dio " + std::to_string(major) + "." +
               std::to_string(minor) + " (" + driverVersion + ")";
    }
    GLint profileMask = 0;
    gl.getIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
    if (!IsCoreProfile(profileMask))
    {
        return "se pidió OpenGL " + GlVersionText() + " core y el driver dio un contexto sin el perfil core (" +
               driverVersion + ")";
    }
    return {};
}
} // namespace

const std::string& GlVersionText()
{
    static const std::string text = std::to_string(GlMajorVersion) + "." + std::to_string(GlMinorVersion);
    return text;
}

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

SDL_FunctionPointer GlProcAddress(const char* name)
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
        return nullptr;
    }
    return address;
}

Core::Expected<GlFunctions, std::string> LoadGlFunctions()
{
    GlFunctions gl;
    std::string error;
    // Una línea por función de la lista: la primera que falta corta.
#define MURET_GL_LOAD(type, member, name)                                                                              \
    if (!LoadGlFunction(gl.member, name, error))                                                                       \
    {                                                                                                                  \
        return Core::Unexpected(std::move(error));                                                                     \
    }
    MURET_GL_QUERY_FUNCTIONS(MURET_GL_LOAD)
    std::string contextError = CheckContext(gl);
    if (!contextError.empty())
    {
        return Core::Unexpected(std::move(contextError));
    }
    MURET_GL_RENDER_FUNCTIONS(MURET_GL_LOAD)
#undef MURET_GL_LOAD
    return gl;
}

const char* GlString(const GlFunctions& gl, GLenum name)
{
    const GLubyte* value = gl.getString(name);
    return value != nullptr ? reinterpret_cast<const char*>(value) : "(sin dato)";
}
} // namespace Client::Platform
