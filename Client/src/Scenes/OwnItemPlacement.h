#pragma once

#include "Core/Expected.h"
#include "Protocol/Character/InventoryList.h"
#include "Scenes/ActorActions.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Un ítem del F3 10 en el inventario propio: 0x5bc740 con la grilla 0x78a9bb0, 8 columnas, 8 filas y el
// último argumento en 1, como la llama 0x4c55a0 (0x4c5e7f..0x4c5e8c), y el tipo de 0x54b3a0. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "El F3 10, el inventario".
namespace Client::World
{
struct ItemState;
}

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Scenes
{
class WorldState;
struct WorldEntryResources;

using InventoryItemBytes = std::span<const std::byte, Protocol::Character::InventoryItemDataSize>;

// 0x5bc75d: con este tipo 0x5bc740 vuelve sin tocar nada.
inline constexpr std::uint16_t NoInventoryItem = 0x1fff;
// 0x5bc776 y 0x5bcc11: los lugares del equipo, del 0 al 11, y desde el 76 los de la segunda grilla.
inline constexpr std::uint8_t FirstInventoryGridSlot = 12;
inline constexpr std::uint8_t FirstSecondGridSlot = 0x4c;

// 0x54b3a0: el byte 0, el bit 0x80 del byte 3 como bit 8 y los cuatro de arriba del byte 5 como bits 9 a
// 12. Los bytes 1, 2, 4 y 6 y los cuatro de abajo del 5 no cuentan.
[[nodiscard]] std::uint16_t InventoryItemType(InventoryItemBytes bytes);

// 0x5bc740 con el lugar y los siete bytes de una entrada del F3 10. Del 0 al 11, en el equipo del registro
// propio, abierto (en línea, 0x5bc77f..0x5bc93c): la durabilidad, el D+0x1c, el tipo, el D+0x1b, ocupado,
// 0x54bf40 y 0x512a30 sobre el héroe publicado; después, lo de las mascotas (el lugar 8), el cuervo (el 1) y
// el anillo 0x1a14 (el 10 y el 11), y el cierre en línea, con 3.848 rand() si lo deja sin accesos. Del 12
// en adelante, en una grilla, con el ancho y el alto del registro de Item.bmd, y después, con
// WorldRuntime::slot78a6758 en menos de 12, el registro abierto para 0x54fe10 si los WORD +0x3e o +0x42 no
// son 0. Lo que MuRet no tiene corta con error después de lo que el original hace antes, y el registro se
// cierra igual. Devuelve lo que pidió 0x4f2250 dentro de 0x512a30, si corrió.
[[nodiscard]] Core::Expected<ActorStopResult, std::string>
PlaceInventoryItem(std::uint8_t slot, InventoryItemBytes bytes, const WorldEntryResources& resources);

// 0x4225d0, la defensa del Dark Horse, que 0x54bf40 llama en 0x54cc5c con el 0x1a04: la opción 0x96 con
// 5 + la agilidad del registro propio / 20 + 2 · el nivel del caballo (WorldRuntime::pets), en un byte, calculada con
// el registro abierto (3.848 rand() al cerrarlo si queda sin accesos). Si ya tiene una 0x96 desde la segunda opción, le
// reescribe el valor; si no, o si es la primera (Q-C415), la agrega al final.
void AddTheDarkHorseOption(World::ItemState& item, const WorldEntryResources& resources);
// Lo mismo con la cuenta de accesos del registro propio y el rand() dados: el tooltip de las mascotas (0x423b07) la
// llama con el registro abierto alrededor (0x5f7bc0), o con las copias de un dibujo intermedio. No mira el tipo: quien
// la llama ya sabe que es el 0x1a04 (0x4225dc).
void AddTheDarkHorseOption(World::ItemState& item, const WorldState& world, std::uint8_t& ownRecordDepth,
                           Render::Models::CrtRandom& random);
// Dónde escribe 0x4225d0 su 0x96: el lugar de una 0x96 desde la segunda opción, o el que sigue a la última (Q-C415).
[[nodiscard]] std::uint8_t DarkHorseOptionPlace(const World::ItemState& item);
} // namespace Client::Scenes
