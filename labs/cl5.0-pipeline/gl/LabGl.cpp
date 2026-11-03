#include "LabGl.h"

#include <string>
#include <utility>

namespace Labs
{
Client::Core::Expected<LabGl, std::string> LoadLabGl(const Client::Platform::GlFunctions& base)
{
    LabGl gl;
    static_cast<Client::Platform::GlFunctions&>(gl) = base;
    std::string error;
    // Una línea por función de la lista: la primera que falta corta.
#define MURET_LAB_GL_LOAD(type, member, name)                                                                          \
    if (!Client::Platform::LoadGlFunction(gl.member, name, error))                                                     \
    {                                                                                                                  \
        return Client::Core::Unexpected(std::move(error));                                                             \
    }
    MURET_LAB_GL_FUNCTIONS(MURET_LAB_GL_LOAD)
#undef MURET_LAB_GL_LOAD
    return gl;
}
} // namespace Labs
