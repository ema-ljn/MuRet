#pragma once

#include "Protocol/Character/Opcodes.h"
#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Core
{
class Log;
}

namespace Client::Protocol::Character
{
struct CharacterSelectResult;
}

// El registro del personaje propio, [0x7681eb0]: lo que la respuesta del F3 03 escribe en él y el
// vaciado de su equipo. Es una proyección tipada, no el layout de 0xf08 bytes; cada campo dice su
// offset. Contrato: Client/docs/research/04-protocolo.md, "La entrada al mundo" (la tabla de la
// respuesta y Q-C144), y 06-render-escenas-y-ui.md, "La lógica" de la selección (la identidad) y
// "La entrada al mundo" (las filas 5 y 7 de 0x4c3940).
//
// Arranca en cero, como el bloque que WinMain llena de ceros (04, 0x4bafbc..0x4bafc2). El original
// lo guarda cifrado en la tabla de protección (06, "La tabla de protección"): abrirlo y cerrarlo,
// con sus rand(), es de quien llama, no de este registro.
namespace Client::Scenes
{
// 06, fila 7 de 0x4c3940: doce lugares desde +0xb68, de a 0x48, y el tipo en −1 al vaciarlos.
inline constexpr std::size_t OwnEquipmentSlotCount = 12;
inline constexpr std::uint16_t EmptyOwnItemId = 0xffff;
// +0x26c: las teclas de las skills de cada uno de los cinco lugares de la selección, de a 0x100.
inline constexpr std::size_t OwnSkillKeyPlaces = 5;
inline constexpr std::size_t OwnSkillKeyBytes = 0x100;

struct OwnCharacterRecord
{
    // Los stats de la respuesta del F3 03 (0x4c3940), según la tabla de 04; el PK y el CtlCode van
    // al héroe, no acá. No toca la identidad ni el equipo: 0x512a30 viste al héroe después con el
    // equipo todavía viejo (06, fila 5).
    // Q-C144: el original copia a +0x48 y +0x4a los WORD de pkt[0x34..0x37], fuera de los 0x34
    // bytes del paquete; MuRet los deja en 0 y lo avisa en `log`.
    void ApplyStats(const Protocol::Character::CharacterSelectResult& reply, Core::Log& log);

    // 0x4c3940, fila 7 (0x4c47c8..0x4c47da): en los doce lugares, el tipo en −1, el dword de +4
    // (scalar1Low) y el byte de +0x1b (flags1b) en 0, porque el equipo lo trae el F3 10. El resto de
    // cada ItemState queda como estaba, como en el original.
    void ClearEquipmentForEntry();

    // La inicialización común del F3 10 (0x4c563c..0x4c565f, 06 "El F3 10, el inventario"), también
    // sin cifrar y con cantidad 0: en los doce lugares, el tipo en −1, el dword de +0x3c (occupied) y
    // el byte de +0x1b (flags1b) en 0. No es el vaciado de la entrada: este deja el dword de +4
    // (scalar1Low), y ninguno de los dos toca la durabilidad (Q-C203).
    void ClearEquipmentForInventory();

    // 0x633f20: +0x6a y +0x6c..+0xab en 0; solo D+4 de los ocho primeros ítems en 0.
    void ResetForSelection();

    // El byte `index` de las skills (+0x6c + index), el que leen la barra, la ventana del personaje y el tooltip de la
    // skill con el lugar de la skill del héroe (+0x425). Los 256 de +0x6c a +0x16b: los 64 de words6c y los de
    // skillsAc; con uno de más, nada.
    [[nodiscard]] std::optional<std::uint8_t> SkillAt(std::size_t index) const;
    // Escribe el byte `index` de las skills (el F3 11, 0x4c5070). Con uno de más, nada.
    void SetSkillAt(std::size_t index, std::uint8_t skill);

    // 06, "La lógica": lo copia la selección (0x6342c0) y la respuesta no lo cambia.
    std::array<std::byte, Protocol::Character::NameSize + 1> name{}; // +0, hasta la clase
    std::uint8_t characterClass = 0;                                 // +0xb
    std::uint8_t skin = 0;                                           // +0xc
    std::uint16_t level = 0;                                         // +0xe

    // 04, la tabla de la respuesta del F3 03: adónde va cada campo.
    std::uint32_t experience = 0;     // +0x10
    std::uint32_t nextExperience = 0; // +0x14
    std::uint16_t strength = 0;       // +0x18
    std::uint16_t agility = 0;        // +0x1a
    std::uint16_t vitality = 0;       // +0x1c
    std::uint16_t energy = 0;         // +0x1e
    std::uint16_t leadership = 0;     // +0x20, de pkt[0x32], el relleno del struct (Q-C144)
    std::uint16_t life = 0;           // +0x22
    std::uint16_t mana = 0;           // +0x24
    std::uint16_t maxLife = 0;        // +0x26
    std::uint16_t maxMana = 0;        // +0x28
    // +0x2a a +0x36: 0x550d10 los pone en 0 (0x54ef00) y les suma lo de los ítems (06, "Los stats del
    // registro propio"); qué son no está acreditado.
    std::uint16_t word2a = 0;
    std::uint16_t word2c = 0;
    std::uint16_t word2e = 0;
    std::uint16_t word30 = 0;
    std::uint16_t word32 = 0;
    std::uint16_t word34 = 0;
    std::uint16_t word36 = 0;
    std::uint16_t bp = 0;    // +0x38
    std::uint16_t maxBp = 0; // +0x3a
    // +0x3c: los bits del ajuste que 0x4bee60 aplica a las velocidades del keepalive (04, el 0E).
    // 0x54fe10 suma 20 a las velocidades con el 0x01 o el 0x08, y prende o apaga el 0x04.
    std::uint8_t speedAdjustBits = 0;
    // +0x3e, +0x40 y +0x42: la respuesta los pone en 0; qué son no está acreditado.
    std::uint16_t word3e = 0;
    std::uint16_t word40 = 0;
    std::uint16_t word42 = 0;
    std::uint16_t addedPoints = 0;    // +0x44
    std::uint16_t maxAddedPoints = 0; // +0x46
    // +0x48 y +0x4a: los de Q-C144; qué son no está acreditado.
    std::uint16_t word48 = 0;
    std::uint16_t word4a = 0;
    // +0x4c a +0x66: los escriben los ocho helpers de 0x550d10 (06, "Los ocho helpers de los stats
    // propios"). +0x4c y +0x58 son las velocidades que manda el keepalive (04, el 0E); qué son los otros
    // no está acreditado. La cola de 0x550d10 lee +0x4e, +0x60 y +0x62.
    std::uint16_t attackSpeed = 0; // +0x4c
    std::uint16_t word4e = 0;
    std::uint16_t word50 = 0;
    std::uint16_t word52 = 0;
    std::uint16_t word54 = 0;
    std::uint16_t word56 = 0;
    std::uint16_t magicSpeed = 0; // +0x58
    std::uint16_t word5a = 0;
    std::uint16_t word5c = 0;
    std::uint16_t word60 = 0;
    std::uint16_t word62 = 0;
    std::uint16_t word64 = 0;
    std::uint16_t word66 = 0;
    std::uint16_t levelUpPoints = 0; // +0x68
    // +0x6a: cuántas skills hay en los 64 primeros lugares y +0x6b, cuántas de ellas tienen el +0x31 de Skill.bmd en 1
    // (las que la barra no muestra). Los cuenta el F3 11 (0x4c5070).
    std::uint8_t byte6a = 0;
    std::uint8_t byte6b = 0;
    std::array<std::uint32_t, 16> words6c{};
    // +0xac a +0x16b: los lugares 64 a 255 de las skills. Solo los escribe el F3 11 (0x4c5070, que pone en 0 los 256
    // con la lista entera y los 0x78 a 0x7b del Dark Lord); 0x633f20 no los toca.
    std::array<std::uint8_t, 0xc0> skillsAc{};
    // +0x26c a +0x76b: las teclas de las skills, 0x100 bytes por cada lugar de la selección (0x6d5edc, de 0 a 4). El
    // F3 30 llena el del elegido (0x4dd632..0x4dd6dc): 0xff en todos y, en el lugar de cada skill del registro, el
    // número de su tecla. Las lee el HUD (0x5a61f9) y las usa la CL9.
    std::array<std::array<std::uint8_t, OwnSkillKeyBytes>, OwnSkillKeyPlaces> skillKeys{};
    // +0x76c a +0xb67: la espera de cada skill, por el índice de la barra (0x5531d0, que la dibuja sobre el ícono).
    // Nadie la escribe hasta la CL9.2: queda en 0.
    std::array<std::int32_t, 255> skillDelays{};

    std::array<World::ItemState, OwnEquipmentSlotCount> equipment{}; // +0xb68

    // [0x7681eb4] + 0xecc: desde el login, el mismo bloque que [0x7681eb0] (0x4bafc9).
    std::uint32_t zen = 0;

    // +0xee2 a +0xf03, la cola de 0x550d10 (0x5512bf..0x5513b1): lee los primeros siete y escribe los
    // otros. Los ocho helpers de antes no escriben esos siete; quién, sin leer (08, P-C38). Qué son no
    // está acreditado.
    std::uint16_t wordee2 = 0;
    std::uint16_t wordee4 = 0;
    std::uint16_t wordee6 = 0;
    std::uint16_t wordeea = 0;
    std::uint16_t wordeec = 0;
    std::uint16_t wordef0 = 0;
    std::uint16_t wordef2 = 0;
    // +0xef4: 0x54f9b0 le suma 0x40d690 sin ponerlo antes en 0, lo suma a +0x5a y +0x5c, y lo deja con
    // lo de 0x40d7f0.
    std::uint16_t wordef4 = 0;
    std::uint16_t wordef8 = 0;
    std::uint16_t wordefa = 0;
    std::uint16_t wordefc = 0;
    std::uint16_t wordefe = 0;
    std::uint16_t wordf00 = 0;
    std::uint8_t bytef02 = 0;
    std::uint8_t bytef03 = 0;
    // +0xf04: la cuenta que 0x518cf0 sube en algunas altas entre la apertura y el cierre del registro (la del
    // terremoto 0xfa, 0x51a8f3..0x51a8fd, Q-C426); la de antes queda en el +0x8c del efecto. Qué cuenta, sin
    // acreditar.
    std::uint8_t bytef04 = 0;
};

// El byte `index` de las skills del registro propio (+0x6c + index), el que leen el F3 30 que llega (0x4dd632) y el
// que sale (0x61c2e4): SkillAt, con 0 de más allá de +0x16b. Los de +0xac a +0x16b los escribe solo el F3 11
// (0x4c5070).
[[nodiscard]] std::uint8_t OwnSkillByte(const OwnCharacterRecord& own, std::size_t index);
} // namespace Client::Scenes
