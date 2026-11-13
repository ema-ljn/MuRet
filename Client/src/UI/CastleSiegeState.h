#pragma once

#include "UI/OldWindows.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

// Lo que guardan las ventanas del castillo (CL9 hito 12; 06 § Castle Siege): los tres objetos que 0x657450 arma al
// arrancar ([0x7d536f8], [0x7d536fc] y [0x7d53700]), sus dos listas y la zona de la .bss 0x7d5c504..0x7d5c658 que
// llenan los B2 y B3. El byte de abierto de cada objeto vive en DelegatedPanels (castleSiege, castleSiegeList y
// secondCastleSiege) y la pestaña de [0x7d536fc] en castleSiegeListMode, que el registro ya leía.
namespace Client::UI
{
// 0x7d5c504..0x7d5c658, plana: el servidor elige el índice de cada registro sin que el cliente lo mire (0x668fd0,
// 0x669030), y los registros se pisan (la estatua 4 es la puerta 0). MuRet guarda los bytes como el original y corta
// una escritura que saldría de la zona (Q-C1360).
inline constexpr std::uint32_t CastleDataBase = 0x7d5c504;
inline constexpr std::size_t CastleDataSize = 0x154;
// 0x668fd0: el tipo 0x11b (las estatuas) desde 0x7d5c504 y el 0x115 (las puertas) desde 0x7d5c574, 0x1c cada uno.
inline constexpr std::uint32_t StatueRecordsAt = 0x7d5c504;
inline constexpr std::uint32_t GateRecordsAt = 0x7d5c574;
inline constexpr std::uint32_t NpcRecordSize = 0x1c;
inline constexpr std::uint32_t GateType = 0x115;
inline constexpr std::uint32_t StatueType = 0x11b;
// Las ventanas miran desde el índice 1: 0x7d5c520 + elegida × 0x1c (las estatuas) y 0x7d5c590 + elegida × 0x1c.
inline constexpr std::uint32_t FirstStatueAt = 0x7d5c520;
inline constexpr std::uint32_t FirstGateAt = 0x7d5c590;
inline constexpr std::uint32_t SelectedGateAt = 0x7d5c638;
inline constexpr std::uint32_t SelectedStatueAt = 0x7d5c63c;
// 0x7d5c640 y 0x7d5c644: los impuestos que muestra y manda la pestaña 2; 0x7d5c648 y 0x7d5c64c, los del servidor.
inline constexpr std::uint32_t ChaosTaxAt = 0x7d5c640;
inline constexpr std::uint32_t StoreTaxAt = 0x7d5c644;
inline constexpr std::uint32_t ServerChaosTaxAt = 0x7d5c648;
inline constexpr std::uint32_t ServerStoreTaxAt = 0x7d5c64c;
// 0x7d5c650..0x7d5c657: la plata del castillo, 64 bits.
inline constexpr std::uint32_t CastleMoneyAt = 0x7d5c650;

// Los campos de un registro de 0x1c (B3, 0x4df7d0 y 0x4df950).
inline constexpr std::uint32_t RecordType = 0;
inline constexpr std::uint32_t RecordIndex = 4;
inline constexpr std::uint32_t RecordDefense = 8;
inline constexpr std::uint32_t RecordRecovery = 0xc;
inline constexpr std::uint32_t RecordMaxLife = 0x10;
inline constexpr std::uint32_t RecordLife = 0x14;
// +0x1a: el NPC está (0x6623e0 elige B2 05 con 0 y B2 06 con otro valor).
inline constexpr std::uint32_t RecordAlive = 0x1a;

struct CastleData
{
    std::array<std::uint8_t, CastleDataSize> bytes{};

    // false si [address, address + size) sale de la zona.
    [[nodiscard]] static bool Holds(std::uint32_t address, std::uint32_t size);
    [[nodiscard]] std::uint8_t U8(std::uint32_t address) const;
    [[nodiscard]] std::int32_t S32(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t U32(std::uint32_t address) const;
    [[nodiscard]] std::uint64_t U64(std::uint32_t address) const;
    void SetU8(std::uint32_t address, std::uint8_t value);
    void SetU32(std::uint32_t address, std::uint32_t value);
    void SetU64(std::uint32_t address, std::uint64_t value);
};

// La fila de la lista de 0x46f320 (0x18 bytes en su deque, 0x46f7e0): +0 la marca de la fila (0x484d00), el nombre
// en +4 (strncpy de 8 y +0xc en 0), las marcas en +0x10 (la clave del orden, 0x46f920) y dos bytes en +0x14 y +0x15.
struct SiegeGuildRow
{
    std::int32_t flag = 0;
    std::array<char, 9> name{};
    std::int32_t marks = 0;
    std::uint8_t first = 0;
    std::uint8_t second = 0;
};

// La fila de la lista de 0x4706f0 (0x10 bytes, 0x470bb0): +0, el nombre en +4 (+0xc en 0) y dos bytes en +0xd y +0xe.
struct RegisteredGuildRow
{
    std::int32_t flag = 0;
    std::array<char, 9> name{};
    std::uint8_t first = 0;
    std::uint8_t second = 0;
};

// Lo que las dos listas (0x46f320 y 0x4706f0, el molde de la lista vieja) guardan además de sus filas. Las dos arman
// lo mismo: 150 filas como mucho (+0xfc), 18 a la vista (+0x104), la primera a la vista en +0x100 y la elegida en
// +0xc4. +0xc8 (la segunda deque) queda en 0: nadie de las ventanas la prende.
inline constexpr std::int32_t SiegeListCapacity = 0x96;
inline constexpr std::int32_t SiegeListRows = 0x12;
template <typename Row>
struct SiegeList
{
    // +0x7c..+0xa8: la deque, las nuevas adelante (push_front).
    std::deque<Row> rows;
    std::int32_t selected = 0;               // +0xc4
    std::int32_t capacity = SiegeListCapacity; // +0xfc
    std::int32_t first = 0;                  // +0x100
    std::int32_t visibleRows = SiegeListRows; // +0x104
    // +0x40 de la ventana (0x45a210): del contador de 0x459ef0.
    std::int32_t id = 0;
};

// Los ids que 0x459ef0 da a los botones y las listas de los tres objetos (*inferido*: la ventana de la lista es el 10,
// 0x657450 arma después el gremio, 0x64f3e0, con 10 ids; el gremio 2, 0x654900, con 5; y estos tres en este orden).
inline constexpr std::int32_t RegistrationButtonId = 26;
inline constexpr std::int32_t SiegeGuildListId = 27;
inline constexpr std::int32_t RegisteredGuildListId = 28;
inline constexpr std::int32_t FirstSeniorButtonId = 29;
inline constexpr std::int32_t FirstHuntZoneButtonId = 36;

// [0x7d536f8] (0x64aca0, 0x344 bytes): la ventana del registro para el asedio, el panel 0x80000.
struct SiegeRegistrationWindow
{
    // +8 y +0xc: el dibujo (0x64d860) los pone en cada cuadro en (0x1c2, 0). *Inferido*: antes del primer dibujo, lo
    // que dejó new (Q-C1361); MuRet arranca con lo que pone el dibujo.
    std::int32_t x = 0x1c2;
    std::int32_t y = 0;
    // +0x18: la pestaña (0, 1 o 2).
    std::int32_t tab = 0;
    // +0x1c: el estado del asedio (pkt[5] del B2 00 con MOVSX); el constructor lo deja en −1.
    std::int32_t state = -1;
    // +0x20: el botón (0x45ad80, 0x32 × 0x12 sin texto). Su lugar lo pone el dibujo (0x64c840 y 0x64d190).
    OldButton button{};
    // +0xa8 (8 bytes y el 0 de +0xb0) y +0xb1 (10 y el 0 de +0xbb): el gremio dueño y su maestro (pkt+0x18 y pkt+0x20).
    std::array<char, 9> ownerGuild{};
    std::array<char, 11> ownerMaster{};
    // +0xbc..+0xcd: tres fechas, el año en un word (el primero arriba) y cuatro bytes: pkt[6..0x17].
    std::array<std::uint16_t, 3> years{};
    std::array<std::array<std::uint8_t, 4>, 3> dates{};
    // +0xd0: pkt[0x2a..0x2d], el primero arriba.
    std::uint32_t remaining = 0;
    // +0xd4 (0x64d2b0): las marcas registradas; +0xd8 (0x64d290): registrado (el byte de abajo del argumento).
    std::uint32_t marks = 0;
    std::uint32_t registered = 0;
    // +0xe4 y +0x214.
    SiegeList<SiegeGuildRow> guilds{.id = SiegeGuildListId};
    SiegeList<RegisteredGuildRow> registeredGuilds{.id = RegisteredGuildListId};

    SiegeRegistrationWindow();
};

// [0x7d536fc] (0x661090, 0x3dc bytes): el Castle Senior, el panel 0x100000. La pestaña (+0x18) es
// DelegatedPanels::castleSiegeListMode.
struct CastleSeniorWindow
{
    // +8 y +0xc: el dibujo (0x6694c0) los pone en cada cuadro; *inferido* como el 0x80000 (Q-C1361).
    std::int32_t x = 0x1c2;
    std::int32_t y = 0;
    // +0x1c, +0xa4, +0x12c, +0x1b4, +0x23c, +0x2c4 y +0x34c: los siete botones (0x45ad80, 0x31 × 0x10).
    std::array<OldButton, 7> buttons{};
    // +0x3d4: el 30 lo pone con pkt[4] en 1 (0x4d25cd, hito 6); el constructor, en 1. Con él, cerrar manda C1 03 87.
    std::uint8_t closesTheMix = 1;
    // +0x3d8: el diálogo que abrió (lo que devolvió 0x6600d0).
    std::int32_t dialog = 0;

    CastleSeniorWindow();
};
inline constexpr std::size_t SeniorBuyButton = 0;      // +0x1c
inline constexpr std::size_t SeniorLifeButton = 1;     // +0xa4
inline constexpr std::size_t SeniorDefenseButton = 2;  // +0x12c
inline constexpr std::size_t SeniorRecoveryButton = 3; // +0x1b4
inline constexpr std::size_t SeniorTaxButton = 4;      // +0x23c
inline constexpr std::size_t SeniorMoneyButton = 5;    // +0x2c4
inline constexpr std::size_t SeniorMixButton = 6;      // +0x34c

// [0x7d53700] (0x6485f0, 0x24c bytes, vtable 0x6baa50): el guardia de la zona de caza, el panel 0x200000.
struct HuntZoneWindow
{
    // +4 y +8: el dibujo (0x64a060) los pone en (0x1c2, 0) en cada cuadro (Q-C1361).
    std::int32_t x = 0x1c2;
    std::int32_t y = 0;
    // +0x15: la entrada abierta (B2 1F 01, 0x4e5f..); +0x16: el modo (0 no muestra nada, 1 entrar, 3 el dueño).
    std::uint8_t allowed = 0;
    std::uint8_t mode = 0;
    // +0x18: el precio (B2 09 con pkt[5] en 3).
    std::uint32_t fee = 0;
    // +0x1c, +0x20 y +0x24: el precio que arma el dueño, el paso y el tope. *Inferido*: nadie fuera del dibujo los
    // escribe; el constructor los deja en 0.
    std::int32_t newFee = 0;
    std::int32_t feeStep = 0;
    std::int32_t feeTop = 0;
    // +0x28: el diálogo que abrió.
    std::int32_t dialog = 0;
    // +0x2c, +0xb4, +0x13c y +0x1c4.
    std::array<OldButton, 4> buttons{};

    HuntZoneWindow();
};
inline constexpr std::size_t HuntZoneFeeButton = 0;  // +0x2c
inline constexpr std::size_t HuntZoneUpButton = 1;   // +0xb4
inline constexpr std::size_t HuntZoneDownButton = 2; // +0x13c
inline constexpr std::size_t HuntZoneEnterButton = 3; // +0x1c4

struct CastleSiegeState
{
    CastleData data{};
    SiegeRegistrationWindow registration{};
    CastleSeniorWindow senior{};
    HuntZoneWindow huntZone{};
};
} // namespace Client::UI
