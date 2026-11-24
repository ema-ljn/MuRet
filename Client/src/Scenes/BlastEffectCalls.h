#pragma once

#include "Core/Expected.h"
#include "World/ModelEffects.h"

#include <cstddef>
#include <optional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

// Los llamadores de los efectos con modelo del lote "switch" de la CL11 (World/BlastModelEffects) desde los pasos de
// los personajes: 0x43a7a0 (Kalima), 0x4f5b70 (los monstruos), 0x4fd5f0 (las skills) y 0x4f97b0.
namespace Client::Scenes
{
struct ActorState;
struct CharacterStepContext;
class WorldState;

// +0x108 con un personaje: su tipo (+0x2), sus ángulos (+0x1c), su posición (+0x10), el byte vivo (+0) y +0x16c, que
// los pasos leen en vivo.
[[nodiscard]] World::ModelEffectOwner BlastOwnerOf(const ActorState& actor);

// 0x518cf0(`arguments`) para un tipo del lote, con `owner` (o nadie) en +0x108 y sus +0x8c y +0x8e (ModelEffect::
// itemType e itemLevel). Lo que pide el alta sale de `context`: el registro propio y el héroe del mundo, los blancos de
// 0x517a20 (Scenes/SkillHits, con context.send), los joints (context.joints, con el efecto de dueño cuando el alta se
// pone de dueño) y el sonido. El lugar del efecto, o nada con la tabla llena; lo que el alta no tiene, corta.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateBlastEffect(const World::ModelEffectArguments& arguments, ActorState* owner, const CharacterStepContext& context);

// 0x518cf0 fuera del paso de los personajes (los paquetes del viewport, como el 0xec del transformado que llega en
// 0x4c965c, y las respuestas de skill, como el 0x112 de la 0x3b en 0x4cddf5): el mismo dueño y los joints de la tabla
// global (WorldState::DrawTail), sin el registro propio, los blancos ni el sonido; si el alta los pide, corta.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateBlastEffectOutsideTheStep(const World::ModelEffectArguments& arguments, ActorState* owner, WorldState& world,
                                Crt::CrtRandom& random);
} // namespace Client::Scenes
