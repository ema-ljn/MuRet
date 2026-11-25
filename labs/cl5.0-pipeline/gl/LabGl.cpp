#include "LabGl.h"

#include <string>
#include <utility>

namespace Labs
{
Client::Core::Expected<LabGl, std::string> LoadLabGl(const Client::Platform::Sdl::GlFunctions& base)
{
    LabGl gl;
    static_cast<Client::Platform::Sdl::GlFunctions&>(gl) = base;
    std::string error;
    // Una línea por función de la lista: la primera que falta corta.
#define MU_LAB_GL_LOAD(type, member, name)                                                                             \
    if (!Client::Platform::LoadGlFunction(gl.member, name, error))                                                     \
    {                                                                                                                  \
        return Client::Core::Unexpected(std::move(error));                                                             \
    }
    MU_LAB_GL_FUNCTIONS(MU_LAB_GL_LOAD)
#undef MU_LAB_GL_LOAD
    return gl;
}
} // namespace Labs
