#pragma once

#include <cstdint>

// 0x4e9c50(con, en), CL9 hito 4, fase 2c: la acción que 0x4ea030 pone en `en` por las manos de `con` (+0x2b8 y +0x2d8
// distintos de −1): las dos 0x63, solo la derecha 0x64, solo la izquierda 0x65 y ninguna 0x62. La llaman la skill
// 0x4c del caballero (0x5896d2, con el héroe en los dos), las ramas 0x5817ce y 0x58eb7a de 0x57cfc0 y las skills que
// llegan (SkillReplies, 0x4cc530). Acá, solo la acción: quien llama la pone con su esqueleto.
namespace Client::Scenes
{
struct ActorState;

[[nodiscard]] std::uint8_t ActionByTheHands(const ActorState& by);
} // namespace Client::Scenes
