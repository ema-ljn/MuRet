#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Data\Local\Dialog.bmd, los diálogos de los NPC y de las misiones (Client/docs/research/05-formatos-de-datos.md,
// "Dialog.bmd"; en el original, 0x54b2b0, que la carga de los datos del juego, 0x61ccb0, llama en 0x61eb69 con
// "Data\Local\Dialog.bmd", antes de quest.bmd).
//
// 0x54b2b0 lee 200 veces 0x400 bytes con fread en un bloque de operator new, les pasa el XOR3 desde su primer byte
// (0x5496c0, la clave de 0x6c8c74) y los copia a 0x7688ac0 + 0x400 i. No mira cuánto leyó (en MuRet, FileTooShort).
// Sin el archivo, avisa con un MessageBox y cierra la ventana (WM_DESTROY); MuRet sigue sin diálogos (P-C170).
// *Verificado* con el del repo: 204.800 bytes, 75 registros que no están en cero, a lo sumo 3 respuestas, y las
// acciones −1, 0, 1, 2 y 3.
namespace Client::Data::Text
{
inline constexpr std::size_t DialogCount = 200;
inline constexpr std::size_t DialogRecordSize = 0x400;
inline constexpr std::size_t DialogTableSize = DialogCount * DialogRecordSize;
inline constexpr std::size_t DialogTextSize = 0x12c;
// Las respuestas que caben: 0x419710 y 0x632f70 las escriben en 10 renglones de 0x26 (0x7d3691c, 0x17c bytes).
inline constexpr std::size_t DialogAnswerCount = 10;
inline constexpr std::size_t DialogAnswerSize = 0x40;

// +0x158 de cada respuesta: lo que hace 0x419bd0 con ella (los demás valores no hacen nada).
enum class DialogAction : std::int32_t
{
    // Mira las condiciones de la misión con zen (0x418fb0(registro, 1)) y, si se cumplen, manda el A2.
    CheckAndAccept = 1,
    // Cierra la ventana de las misiones (0x419900, con el C1 03 31).
    Close = 2,
    // Manda el A2 sin mirar.
    Accept = 3,
};

struct DialogRecord
{
    // +0: el texto, tal cual (DialogTextOf corta en el primer cero).
    std::array<char, DialogTextSize> text{};
    // +0x12c: cuántas respuestas (con signo: 0x419765 sale con JLE).
    std::int32_t answerCount = 0;
    // +0x130: el diálogo al que lleva cada respuesta (negativo: ninguno).
    std::array<std::int32_t, DialogAnswerCount> next{};
    // +0x158: la acción de cada respuesta (DialogAction).
    std::array<std::int32_t, DialogAnswerCount> actions{};
    // +0x180: el texto de cada respuesta, tal cual.
    std::array<std::array<char, DialogAnswerSize>, DialogAnswerCount> answers{};

    friend bool operator==(const DialogRecord&, const DialogRecord&) = default;
};

// Siempre DialogCount registros (en el montón: son 200 KB).
using DialogTable = std::vector<DialogRecord>;

// El texto hasta su primer cero, o los 0x12c bytes si no tiene (el original seguiría en +0x12c).
[[nodiscard]] std::string_view DialogTextOf(const DialogRecord& dialog);
// La respuesta `answer` hasta su primer cero, o los 0x40 bytes.
[[nodiscard]] std::string_view DialogAnswerOf(const DialogRecord& dialog, std::size_t answer);

// Descifra los primeros DialogTableSize bytes de `file` como 0x54b2b0. FileTooShort si son menos; InvalidData si una
// cuenta pasa de 10 (0x419710 y 0x632f70 escribirían fuera de 0x7d3691c). Lo que sobra se ignora.
[[nodiscard]] Core::Expected<DialogTable, DataError> LoadDialogTable(std::span<const std::byte> file);
} // namespace Client::Data::Text
