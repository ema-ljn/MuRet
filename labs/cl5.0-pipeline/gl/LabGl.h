#pragma once

#include "Core/Expected.h"
#include "Platform/GlLoader.h"

#include <string>

// La función de OpenGL que el lab de la CL5.0 pide además de las del cliente
// (Client::Platform::GlFunctions, que arma Platform/GlFunctionList.h). Es la X-macro de la
// pregunta 5 del plan de la CL5, en chico: una línea por función, con su typedef PFNGL...PROC, el
// nombre del miembro y el de GL. Hasta la CL5.1 tenía 41; la CL5.1 se llevó a Platform todas las
// que el cliente usa y acá queda glDrawElements, con el que LabQuadRenderer dibuja el quad con
// índices. El cliente dibuja con glDrawArrays, de a dos triángulos por quad.
#define MURET_LAB_GL_FUNCTIONS(X) X(PFNGLDRAWELEMENTSPROC, drawElements, "glDrawElements")

namespace Labs
{
// La tabla del cliente y las de arriba. Un LabGl{} tiene todos los punteros en nulo.
struct LabGl : Client::Platform::GlFunctions
{
#define MURET_LAB_GL_MEMBER(type, member, name) type member = nullptr;
    MURET_LAB_GL_FUNCTIONS(MURET_LAB_GL_MEMBER)
#undef MURET_LAB_GL_MEMBER
};

// Pide las de MURET_LAB_GL_FUNCTIONS al contexto actual, sobre la tabla del cliente que ya trae
// `base`, con el mismo pedido que LoadGlFunctions (Client::Platform::LoadGlFunction, con el
// reintento en opengl32.dll de Windows). El error nombra la primera que falta.
[[nodiscard]] Client::Core::Expected<LabGl, std::string> LoadLabGl(const Client::Platform::GlFunctions& base);
} // namespace Labs
