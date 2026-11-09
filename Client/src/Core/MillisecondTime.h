#pragma once

#include <cstdint>

namespace Client::Core
{
// Publicación T (4eb620, store 4eb67b): entero unsigned completo a binary32, sin origen ni
// escala. Perfil nominal RN, ties-to-even: cuantiza con enteros y convierte exactamente el
// resultado, sin consultar ni modificar FE/x87. No reproduce el RC dinámico del original.
// No muestrea: qT y el punto de publicación pertenecen al caller, separados de qD.
[[nodiscard]] float Milliseconds32ToFloatRn(std::uint32_t sample) noexcept;
} // namespace Client::Core
