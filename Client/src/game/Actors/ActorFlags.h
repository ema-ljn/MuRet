#pragma once

#include <cstdint>
#include <span>

// Las banderas de estado de un personaje (+0x78 y +0x7c) que bajan las skills, y los efectos y joints que se van con
// ellas (CL9 hito 4). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Las skills que llegan".
namespace Client::World
{
class ModelEffects;
}
namespace Client::Scenes
{
struct ActorState;
struct JointState;
class WorldState;

// 0x518ac0(tipo, dueño, subtipo): apaga los efectos de modelo vivos de ese tipo (MOVSX del +2), de ese dueño (+0x108)
// y de ese subtipo (+4; −1, cualquiera), en los 200. Devuelve si apagó alguno. Sin rand(). El dueño de MuRet es el
// ownerContext del efecto (*inferido*, Q-C1200: los efectos con dueño personaje lo guardan ahí).
bool KillModelEffectsOf(World::ModelEffects& effects, std::int16_t type, const void* owner, std::int32_t subtype);

// 0x534f90(tipo, dueño, subtipo): lo mismo en los 500 joints (tipo en +4, dueño en +0x54 y subtipo en +0xc), sin
// devolver nada. Sin rand().
void KillJointsOf(std::span<JointState> joints, std::int32_t type, const void* owner, std::int32_t subtype);

// 0x4e6910(máscara, personaje): baja la bandera de +0x78 (o de +0x7c) si está y, según cuál, apaga los efectos y los
// joints que la acompañan (0x518ac0 y 0x534f90), en `world`. Con otra máscara, nada. Ver la tabla de 06.
void RemoveActorFlag(std::uint32_t mask, ActorState& actor, WorldState& world);

// 0x439d40: en un personaje que no es un 0x20a, con 0x100 en +0x78 lo baja y pone +0x110 en 0x21; si no, con 8, lo
// baja y pone 0x22.
void SettleMonsterFlags(ActorState& actor);
} // namespace Client::Scenes
