#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Lo que guardan los paquetes del castillo fuera de la batalla (CL9 hito 12; 04 § Castle Siege, las ventanas del
// castillo): la zona de la .bss 0x7d5c504..0x7d5c658 que llenan el B3 y los B2 05..10, los campos del paquete de la
// ventana del registro ([0x7d536f8], 0x80000) y de la del guardia de la zona de caza ([0x7d53700], 0x200000), y los dos
// globales del B2 1A. El byte de abierto de cada ventana vive en DelegatedPanels (castleSiege en +4 y
// secondCastleSiege en +0x14); lo demás de los tres objetos (botones, listas, pestañas, el dibujo) es de las fases
// siguientes del hito.
namespace Client::UI
{
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
};

// Los campos de [0x7d53700] (0x6485f0, 0x24c bytes) que escriben los paquetes; el constructor los deja en 0.
struct HuntZoneWindow
{
    // +0x15: la entrada abierta (B2 1F con 1: SETNZ de pkt[5]).
    std::uint8_t allowed = 0;
    // +0x18: el precio (B2 09 con pkt[5] en 3).
    std::uint32_t fee = 0;
};

struct CastleSiegeState
{
    CastleData data{};
    SiegeRegistrationWindow registration{};
    HuntZoneWindow huntZone{};
    // 0x78aba34 y 0x78aba30 (.bss 0): el B2 1A con pkt[4] en 1 y en 2, MOVZX de pkt[5]. Los leen 0x44d680, 0x5b1200,
    // 0x5b12f0, 0x5f4720 y 0x5febf0, la batalla (hito 13).
    std::uint32_t siegeFlag1 = 0;
    std::uint32_t siegeFlag2 = 0;
};
} // namespace Client::UI
