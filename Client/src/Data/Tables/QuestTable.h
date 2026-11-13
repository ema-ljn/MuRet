#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Data\Local\quest.bmd, las misiones del objeto [0x710148] (Client/docs/research/05-formatos-de-datos.md,
// "quest.bmd"; en el original, 0x418d10, que la carga de los datos del juego, 0x61ccb0, llama en 0x61eb7c con
// "Data\Local\quest.bmd", después de Dialog.bmd y antes de movereq.bmd).
//
// 0x418d10 pone en 0 los 200 registros de +0xc (34.000 dwords) y lee 200 veces 0x2a8 bytes con fread en un bloque de
// operator new, les pasa el XOR3 desde su primer byte (0x418de0, FC CF AB con la clave de 0x6c64b4) y los copia en su
// lugar. No mira cuánto leyó: un archivo corto deja en el bloque lo del registro anterior y lo vuelve a descifrar (en
// MuRet, FileTooShort). Sin el archivo, devuelve 0 sin tocar los registros, y 0x61ccb0 no lo mira. *Verificado* con el
// del repo: 136.000 bytes, y solo los cuatro primeros registros no están en cero.
namespace Client::Data::Tables
{
inline constexpr std::size_t QuestCount = 200;
inline constexpr std::size_t QuestRecordSize = 0x2a8;
inline constexpr std::size_t QuestTableSize = QuestCount * QuestRecordSize;
inline constexpr std::size_t QuestNameSize = 0x20;
inline constexpr std::size_t QuestEntryCount = 16;
inline constexpr std::size_t QuestConditionCount = 16;
// +7 a +0xb de cada pedido: una marca por clase, con la clase & 7 de +4 del objeto como índice (0x418f40 y 0x418fb0).
// 0x418e20 no la recorta: una clase 5, 6 o 7 lee los bytes de +0xc de la entrada (sus diálogos).
inline constexpr std::size_t QuestClassFlagCount = 5;
inline constexpr std::size_t QuestDialogCount = 4;

// Un pedido de la misión (16 de 0x14 bytes desde +0x26): qué ítem hay que traer y los diálogos de cada estado.
struct QuestEntry
{
    // +0: siempre 1 en el archivo; nadie lo lee (*estático*).
    std::uint8_t byte0 = 0;
    // +1: 1 si el pedido es de ítems (0x4194c0 y 0x419b10 solo miran esos).
    std::uint8_t kind = 0;
    // +2 y +3: el grupo y el índice del ítem (el tipo es grupo · 0x200 + índice, 0x4194e6).
    std::uint8_t itemGroup = 0;
    std::uint8_t itemIndex = 0;
    // +4: el nivel que pide 0x558410 ((+4 >> 3) & 0xf del ítem).
    std::uint8_t itemLevel = 0;
    // +5: cuántos.
    std::uint8_t itemCount = 0;
    // +6: el enlace con las condiciones (+1 de cada una; 0xff en la condición vale para todos).
    std::uint8_t condition = 0;
    // +7 a +0xb: la marca de cada clase (0 no; en el archivo, 1 o 2).
    std::array<std::uint8_t, QuestClassFlagCount> classes{};
    // +0xc: los diálogos de Dialog.bmd (0x418f40 con el lugar): 0 el de antes de aceptar, 1 el de faltan ítems, 2 el de
    // los tiene y 3 el de terminada.
    std::array<std::int16_t, QuestDialogCount> dialogs{};

    friend bool operator==(const QuestEntry&, const QuestEntry&) = default;
};

// Una condición (16 de 0x14 bytes desde +0x168), la que mira 0x418fb0.
struct QuestCondition
{
    // +0: siempre 1 en el archivo; nadie lo lee (*estático*).
    std::uint8_t byte0 = 0;
    // +1: el pedido al que se aplica (+6 de la entrada), o 0xff para todos.
    std::uint8_t link = 0;
    // +2: la misión que tiene que estar terminada (estado 2), o −1.
    std::int16_t needQuest = 0;
    // +4 y +6: el nivel mínimo y el máximo (0 es sin tope; los dos se comparan sin signo).
    std::uint16_t minLevel = 0;
    std::uint16_t maxLevel = 0;
    // +8 a +0xb: en cero en el archivo; nadie lo lee (*estático*).
    std::uint32_t dword8 = 0;
    // +0xc: el zen (sin signo; 0 es sin pedido).
    std::uint32_t zen = 0;
    // +0x10: el diálogo cuando no se cumple.
    std::int16_t failDialog = 0;
    // +0x12: en cero en el archivo; nadie lo lee.
    std::int16_t word12 = 0;

    friend bool operator==(const QuestCondition&, const QuestCondition&) = default;
};

// Un registro de 0x2a8 bytes.
struct QuestRecord
{
    // +0 y +2: cuántos pedidos y cuántas condiciones (con signo: 0x418fb0 los compara con JLE y JL).
    std::int16_t entryCount = 0;
    std::int16_t conditionCount = 0;
    // +4: el tipo del NPC que la da (0xeb, 0xe5 en el archivo).
    std::uint8_t npcType = 0;
    // +5: el nombre, tal cual (NameOf corta en el primer cero).
    std::array<char, QuestNameSize> name{};
    std::array<QuestEntry, QuestEntryCount> entries{};
    std::array<QuestCondition, QuestConditionCount> conditions{};

    friend bool operator==(const QuestRecord&, const QuestRecord&) = default;
};

// Siempre QuestCount registros (en el montón: son 136 KB).
using QuestTable = std::vector<QuestRecord>;

// El nombre hasta su primer cero, o los 32 bytes si no tiene (el original seguiría en +0x25 y en los pedidos).
[[nodiscard]] std::string_view NameOf(const QuestRecord& quest);

// Descifra los primeros QuestTableSize bytes de `file` como 0x418d10. FileTooShort si son menos. Lo que sobra se
// ignora.
[[nodiscard]] Core::Expected<QuestTable, DataError> LoadQuestTable(std::span<const std::byte> file);
} // namespace Client::Data::Tables
