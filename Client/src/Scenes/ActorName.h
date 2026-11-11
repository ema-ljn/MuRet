#pragma once

#include <string_view>

namespace Client::Scenes
{
struct ActorState;

// 0x576960(personaje + 0x1d9, palabra, 0): si `word` está en el nombre del personaje, hasta su 0. Sin 0 en
// los diez bytes, el original sigue en +0x1e3, que la respuesta del F3 03 pone en 0. Lo usan el GM de
// 0x413fe0 y la cámara de espectador del mapa 6 (0x635ae0, 0x5fb570 y 0x6371a0, Q-C152).
[[nodiscard]] bool ActorNameContains(const ActorState& actor, std::string_view word);
} // namespace Client::Scenes
