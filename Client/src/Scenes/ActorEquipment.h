#pragma once

#include "Core/Expected.h"
#include "Scenes/ActorActions.h"
#include "Scenes/OwnStatHelpers.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Data::Tables
{
struct ItemSetTables;
}

// El equipo propio en el personaje: 0x512a30, con 0x505820 y 0x550d10. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "El equipo propio en el personaje", "El conjunto completo
// de 0x505820" y "Los stats del registro propio", y 04, "El F3 10".
namespace Client::Scenes
{
struct ActorState;
struct ItemSetState;
struct OwnCharacterRecord;
struct Scan505820;

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
    // [0x743a178] y [0x743a17c] (WorldRuntime::fullSetScan): los escribe 0x505820 con el héroe publicado, y
    // 0x550d10 los lee en 0x550990 con lo que haya.
    std::optional<Scan505820>& fullSetScan;
    // Lo de 0x4f2250, que 0x512a30 llama en el medio (0x512bd4). Su random es el de la tabla, y sus
    // items, el Item.bmd que también lee 0x550d10.
    ActorStopContext stop;
    // [0x6eed3c], para 0x550d10: las tablas de 0x40c600 y lo que escriben 0x40e1b0 y 0x40da20, que
    // dura de una llamada a otra (WorldState::ItemSets). Sin alguno de los dos, 0x550d10 corta en
    // 0x40e1b0 antes de tocar nada (Scenes::OwnStatsContext).
    const Data::Tables::ItemSetTables* itemSetTables = nullptr;
    ItemSetState* itemSets = nullptr;
    // El ítem del cursor para 0x54efe0, 0x54f9b0 y 0x54fe10 (HeldItemOf): lo que hay en sus globales al llamar.
    OwnEquipmentOverride held{};
};

// 0x4053f0: en los mapas 18 a 23 (`map`), 0x4053a0 (las manos, las alas y la mascota sin ítem, con el nivel en 0) y el
// arma del evento por la clase (+0x1d4 & 7): el bastón 5 5 en la derecha de la 0, la ballesta 4 19 en la de la 2 y
// la espada 0 16 en las dos de la 1, la 3 y la 4. En los otros mapas, nada.
void ArmForTheEventMaps(ActorState& actor, std::int32_t map);

// 0x512930: con +0x3e3 en 0, la cabeza de la clase bajo un casco abierto (+0x1f8) y la escala por la clase y la piel;
// con +0x3e3 distinto de 0, nada.
void ApplyHeadAndScale(ActorState& actor);

// 0x512a30 sobre `actor`: con el tipo 0x20a, los modelos, los niveles y los bytes del equipo propio, la
// acción de parado y, con +0x3e3 en 0, la ropa, las armas de los mapas 18 a 23, la cabeza, la escala, el
// conjunto completo si es el héroe publicado (0x505820, Scenes/FullSetScan.h) y los stats del registro
// (0x550d10, Scenes::RecalculateOwnStats). Lo que MuRet no tiene corta con error después de lo que el
// original hace antes, y el acceso al registro se cierra igual. Devuelve lo que pidió 0x4f2250, si corrió.
[[nodiscard]] Core::Expected<ActorStopResult, std::string> ApplyActorEquipment(ActorState& actor,
                                                                               const ActorEquipmentContext& context);
} // namespace Client::Scenes
