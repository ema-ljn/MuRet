#pragma once

#include "UI/CastleSeniorWindow.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <deque>

// Lo que guardan los paquetes del castillo fuera de la batalla (CL9 hito 12; 04 § Castle Siege, las ventanas del
// castillo): la zona de la .bss 0x7d5c504..0x7d5c658 que llenan el B3 y los B2 05..10, los campos del paquete de la
// ventana del registro ([0x7d536f8], 0x80000) y de la del guardia de la zona de caza ([0x7d53700], 0x200000), y los dos
// globales del B2 1A. El byte de abierto de cada ventana vive en DelegatedPanels (castleSiege en +4 y
// secondCastleSiege en +0x14); los botones, listas y pestañas se conservan aquí entre frames.
namespace Client::UI
{
// Los botones reciben ids internos negativos, como los del diálogo y del gremio (P-C280); los nativos son del
// contador 0x5762eb0. No se usa su valor para el wire.
[[nodiscard]] inline OldButton CastleButton(std::int32_t id, std::int32_t width, std::int32_t height)
{
    OldButton button;
    button.base.id = id;
    button.base.width = width;
    button.base.height = height;
    return button;
}

// 0x46f320 y 0x4706f0: listas ancladas abajo, 18 renglones, capacidad 150.
[[nodiscard]] inline OldList CastleGuildList(std::int32_t id)
{
    OldList list;
    list.base.id = id;
    list.base.x = 470;
    list.base.y = 360;
    list.base.width = 157;
    list.base.height = 235;
    list.base.rectMode = 2;
    list.rows = 18;
    return list;
}

// 0x46f7e0 y 0x470bb0: el toggle del renglón, strncpy del nombre y los campos copiados por B4/B5.
struct CastleGuildRow
{
    std::int32_t toggled = 0;
    std::array<char, 9> name{};
    std::uint32_t marks = 0;
    std::uint8_t first = 0;
    std::uint8_t second = 0;
};

// 0x7d5c504..0x7d5c658, plana: el servidor elige el índice de cada registro sin que el cliente lo mire (0x668fd0,
// 0x669030: base + índice × 0x1c, en 32 bits), y los registros se pisan (la estatua 4 es la puerta 0; la puerta 7 cae
// sobre las elegidas y los impuestos). MuRet guarda los bytes como el original y descarta el paquete que escribiría
// fuera de la zona (Q-C1360).
inline constexpr std::uint32_t CastleDataBase = 0x7d5c504;
inline constexpr std::size_t CastleDataSize = 0x154;
// 0x668fd0: el tipo 0x11b (las estatuas) desde 0x7d5c504 y el 0x115 (las puertas) desde 0x7d5c574, 0x1c cada uno.
// 0x669030 elige con 0 las puertas y con otro valor las estatuas.
inline constexpr std::uint32_t StatueRecordsAt = 0x7d5c504;
inline constexpr std::uint32_t GateRecordsAt = 0x7d5c574;
inline constexpr std::uint32_t NpcRecordBytes = 0x1c;
inline constexpr std::uint32_t GateType = 0x115;
inline constexpr std::uint32_t StatueType = 0x11b;
// 0x7d5c640 y 0x7d5c644: los impuestos que muestra y cambia la ventana del Castle Senior; 0x7d5c648 y 0x7d5c64c, los
// que dio el servidor (0x669070 y 0x6690f0 escriben los dos pares).
inline constexpr std::uint32_t ChaosTaxAt = 0x7d5c640;
inline constexpr std::uint32_t StoreTaxAt = 0x7d5c644;
inline constexpr std::uint32_t ServerChaosTaxAt = 0x7d5c648;
inline constexpr std::uint32_t ServerStoreTaxAt = 0x7d5c64c;
// 0x7d5c650..0x7d5c657: la plata del castillo, 64 bits (0x669070 byte a byte, 0x669160).
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
    // Quien llama ya miró Holds: fuera de la zona, std::out_of_range.
    [[nodiscard]] std::uint32_t U32(std::uint32_t address) const;
    [[nodiscard]] std::uint64_t U64(std::uint32_t address) const;
    void SetU32(std::uint32_t address, std::uint32_t value);
    void SetU64(std::uint32_t address, std::uint64_t value);
    void SetBytes(std::uint32_t address, std::span<const std::uint8_t> values);
};

// 0x669030: el registro de un tipo (0x115 elige las puertas, cualquier otro las estatuas) y un índice, en 32 bits.
[[nodiscard]] std::uint32_t NpcRecordAddress(std::uint32_t type, std::uint32_t index);

// Las elegidas de las ventanas del Castle Senior (las ponen 0x663b20 para las puertas y 0x666190 para las estatuas) y
// lo que miran las respuestas del diálogo: las puertas desde 0x7d5c590 y las estatuas desde 0x7d5c520, el registro 1 de
// cada lista (0x6623e0, 0x664370).
inline constexpr std::uint32_t SelectedGateAt = 0x7d5c638;
inline constexpr std::uint32_t SelectedStatueAt = 0x7d5c63c;
inline constexpr std::uint32_t FirstGateAt = 0x7d5c590;
inline constexpr std::uint32_t FirstStatueAt = 0x7d5c520;

// 0x660da0: el nivel de la vida de un registro por su vida máxima (+0x10, con signo). Una puerta: hasta 500000 el 0,
// hasta 800000 el 1, 1500000 justo el 2 y si no el 3. Una estatua: hasta 400000 el 0, hasta 750000 el 1, hasta
// 1300000 el 2 y si no el 3. Otro tipo, −1.
[[nodiscard]] std::int32_t LifeLevelOf(std::uint32_t type, std::int32_t maxLife);
// 0x660d20: la vida máxima de un nivel. Una puerta: 500000, 800000, 1500000 con el 2 y 3000000 con cualquier otro;
// una estatua: 400000, 750000, 1300000 con el 2 y 2500000 con cualquier otro. Otro tipo, −1.
[[nodiscard]] std::int32_t MaxLifeOfLevel(std::uint32_t type, std::int32_t level);
// 0x66205d..0x6620ed: FILD/FIDIV de enteros, FSTP del cociente y del producto de la barra roja.
[[nodiscard]] float NpcLifeBarWidth(std::int32_t life, std::int32_t maximum, float width);
// 0x661050: aumento de recuperación según tipo y nivel, incluso en registros alias de la zona plana.
[[nodiscard]] std::int32_t NpcRecoveryIncrease(std::uint32_t type, std::int32_t recovery);

// Una de las tres fechas de +0xbc..+0xcd: el año en un word (el primero arriba en el paquete) y cuatro bytes.
struct SiegeDate
{
    std::uint16_t year = 0;
    std::array<std::uint8_t, 4> rest{};
};

// Los campos de [0x7d536f8] (0x64aca0, 0x344 bytes) que escriben los paquetes. El constructor deja +0x1c en −1, +0xd4 y
// +0xd8 en 0 y los nombres en 0; +0xbc..+0xd0 quedan con lo que dejó new (Q-C1362): MuRet los arranca en 0.
struct SiegeRegistrationWindow
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
    // +0x18: pestaña inicial; 0x64b200 la vuelve a 0 sólo cuando la ventana estaba abierta.
    std::int32_t tab = 0;
    // +0x1c: el estado del asedio (pkt[5] del B2 00 con MOVSX).
    std::int32_t state = -1;
    // +0xa8 (8 bytes y el 0 de +0xb0) y +0xb1 (10 y el 0 de +0xbb): el gremio dueño y su maestro, con strncpy.
    std::array<std::uint8_t, 9> ownerGuild{};
    std::array<std::uint8_t, 11> ownerMaster{};
    // +0xbc..+0xcd.
    std::array<SiegeDate, 3> dates{};
    // +0xd0: pkt[0x2a..0x2d], el primero arriba.
    std::uint32_t remaining = 0;
    // +0xd4 (0x64d2b0): las marcas registradas; +0xd8 (0x64d290): el gremio está registrado (el byte de abajo del
    // argumento, en un dword).
    std::uint32_t marks = 0;
    std::uint32_t registered = 0;
    OldButton button = CastleButton(-300, 50, 18);
    OldList siegeGuildList = CastleGuildList(-301);
    OldList registeredGuildList = CastleGuildList(-302);
    std::deque<CastleGuildRow> siegeGuilds;
    std::deque<CastleGuildRow> registeredGuilds;
};

// Los campos de [0x7d53700] (0x6485f0, 0x24c bytes) que escriben los paquetes; el constructor los deja en 0.
struct HuntZoneWindow
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
    // +0x15: la entrada abierta (B2 1F con 1: SETNZ de pkt[5]).
    std::uint8_t allowed = 0;
    std::uint8_t mode = 0;
    // +0x18: el precio (B2 09 con pkt[5] en 3).
    std::uint32_t fee = 0;
    std::int32_t editedFee = 0;
    std::int32_t step = 0;
    std::int32_t max = 0;
    std::uint32_t dialogId = 0;
    OldButton commit = CastleButton(-310, 50, 18);
    OldButton plus = CastleButton(-311, 16, 16);
    OldButton minus = CastleButton(-312, 16, 16);
    OldButton enter = CastleButton(-313, 50, 18);
};

struct CastleSiegeState
{
    CastleData data{};
    SiegeRegistrationWindow registration{};
    HuntZoneWindow huntZone{};
    CastleSeniorWindow senior{};
    // 0x78aba34 y 0x78aba30 (.bss 0): el B2 1A con pkt[4] en 1 y en 2, MOVZX de pkt[5]. Los leen 0x44d680, 0x5b1200,
    // 0x5b12f0, 0x5f4720 y 0x5febf0, la batalla (hito 13).
    std::uint32_t siegeFlag1 = 0;
    std::uint32_t siegeFlag2 = 0;
};
} // namespace Client::UI
