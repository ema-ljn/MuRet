#pragma once

#include "Core/Expected.h"
#include "Scenes/ActorActions.h"

#include <cstdint>
#include <string>

namespace Client::Data::Tables
{
struct ItemSetTables;
}

// El equipo propio en el personaje: 0x512a30, con 0x550d10 hasta donde llega. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "El equipo propio en el personaje" y "Los stats del
// registro propio", y 04, "El F3 10".
namespace Client::Scenes
{
struct ActorState;
struct ItemSetState;
struct OwnCharacterRecord;

// Lo que 0x512a30 lee y escribe fuera del personaje. Quien llama lo presta durante la llamada.
struct ActorEquipmentContext
{
    // [0x7681eb4], el registro propio: los nueve lugares que lee, y lo que escribe 0x550d10, un
    // thiscall sobre él (0x512ef5).
    OwnCharacterRecord& own;
    // La cuenta de accesos de su entrada en la tabla de protección (Render::Models::LogicalAccess):
    // 0x512a30 lo abre al empezar y lo cierra al terminar, con los 3.848 rand() si lo deja en 0.
    std::uint8_t& ownRecordAccesses;
    // DAT_0743a170, el héroe publicado en ese momento: con él, 0x505820 (0x512ee2). 0x550d10 lee su
    // clase y escribe su +0x358 (0x40d010).
    ActorState& publishedHero;
    // Lo de 0x4f2250, que 0x512a30 llama en el medio (0x512bd4). Su random es el de la tabla, y sus
    // items, el Item.bmd que también lee 0x550d10.
    ActorStopContext stop;
    // [0x6eed3c], para 0x550d10: las tablas de 0x40c600 y lo que escriben 0x40e1b0 y 0x40da20, que
    // dura de una llamada a otra (WorldState::ItemSets). Sin alguno de los dos, 0x550d10 corta en
    // 0x40e1b0 antes de tocar nada (Scenes::OwnStatsContext).
    const Data::Tables::ItemSetTables* itemSetTables = nullptr;
    ItemSetState* itemSets = nullptr;
};

// 0x512a30 sobre `actor`: con el tipo 0x20a, los modelos, los niveles y los bytes del equipo propio, la
// acción de parado y, con +0x3e3 en 0, la ropa, las armas de los mapas 18 a 23, la cabeza, la escala y
// los stats del registro (0x550d10). Lo que MuRet no tiene corta con error después de lo que el original
// hace antes, y el acceso al registro se cierra igual: 0x505820 con el héroe publicado, y 0x550d10, que
// corre siempre que hay ropa y hoy corta en sus ocho helpers (Scenes::RecalculateOwnStats).
// Devuelve lo que pidió 0x4f2250, si corrió.
[[nodiscard]] Core::Expected<ActorStopResult, std::string> ApplyActorEquipment(ActorState& actor,
                                                                               const ActorEquipmentContext& context);
} // namespace Client::Scenes
