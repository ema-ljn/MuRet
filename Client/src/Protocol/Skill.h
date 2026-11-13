#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>
#include <vector>

// 04 § Las skills (CL9 hito 4): lo que llega de las skills y de los estados de los personajes. Los lectores son la
// parte pura de los handlers de 0x4e2b10; lo que hacen con el mundo es de Scenes (SkillReplies). Como los demás
// lectores, WrongPacket con otro tipo u opcode, TooShort si faltan campos, y los bytes de más se ignoran.
namespace Client::Protocol::Skill
{
inline constexpr std::uint8_t ActorStateOpcode = 0x07;
inline constexpr std::uint8_t MagicAttackOpcode = 0x19;
inline constexpr std::uint8_t MagicCancelOpcode = 0x1b;
inline constexpr std::uint8_t DurationMagicOpcode = 0x1e;
inline constexpr std::uint8_t ActorEffectOpcode = 0x48;
inline constexpr std::uint8_t SkillTargetOpcode = 0x69;
inline constexpr std::uint8_t Report73Opcode = 0x73;
// Del F3 (Character::CharacterOpcode).
inline constexpr std::uint8_t SkillListSubcode = 0x11;
inline constexpr std::uint8_t SummonLifeSubcode = 0x20;

// 0x4cc530: C1/C3 08 19 [skill] [fuente H L] [blanco H L], en big endian. El bit 15 del blanco dice si la skill
// salió (va a +0x38d de la fuente) y el original busca el blanco con los 15 de abajo; la fuente, con los 16.
struct MagicAttack
{
    std::uint8_t skill = 0;
    std::uint16_t source = 0;
    std::uint16_t target = 0; // ya sin el bit 15
    bool succeeded = false;
};

// 0x4ce000: C1/C3 09 1E [skill] [clave H L] [x] [y] [rumbo].
struct DurationMagic
{
    std::uint8_t skill = 0;
    std::uint16_t key = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t direction = 0;
};

// 0x4cbe90: C1 06 1B [skill] [clave H L], el fin de una skill con duración.
struct MagicCancel
{
    std::uint8_t skill = 0;
    std::uint16_t key = 0;
};

// 0x4cbe00: C1 0A 07 [tipo] [máscara, 4 bytes en little endian] [clave H L]. El GS 0.97 arma otro, de 8 bytes, y no lo
// manda (08, P-C260).
struct ActorState
{
    std::uint8_t type = 0;
    std::uint32_t mask = 0;
    std::uint16_t key = 0;
};

// 0x4dbf50: C1 06 48 [clave H L] [tipo]. El GS 0.97 no lo manda (08, P-C262).
struct ActorEffect
{
    std::uint16_t key = 0;
    std::uint8_t kind = 0;
};

// 0x4cc290: C1 08 69 [skill] [fuente, 2 bytes] [blanco, 2 bytes], las claves en little endian y sin máscara: un
// blanco con el bit 15 no lo encuentra 0x5110d0 (07, Q-C1205). El GS 0.97 no lo manda (08, P-C262).
struct SkillTarget
{
    std::uint8_t skill = 0;
    std::uint16_t source = 0;
    std::uint16_t target = 0; // entero, con el bit 15
};

// 0x4c5070: C1 .. F3 11 [cantidad] [lista] y desde pkt[6] lo que dice la cantidad: con 0xff, el lugar a vaciar; con
// 0xfe, el lugar y la skill; si no, con la lista en 2, el lugar a vaciar; y si no, `cantidad` entradas de tres bytes
// (lugar, skill y el tercero, que el cliente no lee). El GS arma la entrada desde pkt[5]: el byte de pkt[5] lo mete el
// FoxAnti del repack (08, P-C261).
struct SkillListEntry
{
    std::uint8_t slot = 0;
    std::uint8_t skill = 0;
};
struct SkillList
{
    std::uint8_t count = 0;
    std::uint8_t list = 0;
    // Con 0xff, 0xfe o la lista en 2: pkt[6] y pkt[7] (este solo con 0xfe). Si no, las entradas.
    std::uint8_t slot = 0;
    std::uint8_t skill = 0;
    std::vector<SkillListEntry> entries;
};
inline constexpr std::uint8_t SkillListRemoveOne = 0xff;
inline constexpr std::uint8_t SkillListAddOne = 0xfe;
inline constexpr std::uint8_t SkillListClearSlot = 2;

// 0x4e31e1: C1 05 F3 20 [vida], la vida del invocado (0x5767960).
struct SummonLife
{
    std::uint8_t life = 0;
};

[[nodiscard]] Core::Expected<MagicAttack, ProtocolError> ReadMagicAttack(const Network::Packet& packet);
[[nodiscard]] Core::Expected<DurationMagic, ProtocolError> ReadDurationMagic(const Network::Packet& packet);
[[nodiscard]] Core::Expected<MagicCancel, ProtocolError> ReadMagicCancel(const Network::Packet& packet);
[[nodiscard]] Core::Expected<ActorState, ProtocolError> ReadActorState(const Network::Packet& packet);
[[nodiscard]] Core::Expected<ActorEffect, ProtocolError> ReadActorEffect(const Network::Packet& packet);
[[nodiscard]] Core::Expected<SkillTarget, ProtocolError> ReadSkillTarget(const Network::Packet& packet);
[[nodiscard]] Core::Expected<SkillList, ProtocolError> ReadSkillList(const Network::Packet& packet);
[[nodiscard]] Core::Expected<SummonLife, ProtocolError> ReadSummonLife(const Network::Packet& packet);

// El aviso de los que no llegaron cifrados: C1 06 F1 03 00 [cabeza] con el XOR32 y en C3, sin leer el paquete. La
// cabeza es la del opcode en el 19 (0x4cc562..0x4cce21) y en el 1E (0x4ce032..), y 0xf1 en el 73 (0x4d5d8c),
// *estático*.
[[nodiscard]] Outgoing WriteClearSkillReport(std::uint8_t head);

inline constexpr std::uint8_t Report73Head = 0xf1;

// El 19 que manda el cliente (0x55b6f0, 0x559890, 0x55cbc0, 0x55faa0 y las ramas de 0x57cfc0): C1 06 19 [skill]
// [clave H] [clave L], con el XOR32 sobre los bytes 3 a 5 y en C3 (sus dos rand(), el relleno y el serial, los saca
// quien lo manda). La clave es +0x1f4 del blanco, entera. Es el PMSG_MAGICATTACK del GameServer (CGMagicAttack).
[[nodiscard]] Outgoing WriteMagicAttack(std::uint8_t skill, std::uint16_t key);

// Lo que mandan las skills del héroe (CL9 hito 4, fase 2c; 04 § Las skills que salen). Las arman 0x57cfc0, las
// funciones por clase y 0x55cbc0 en línea, con el XOR32 desde pkt[3] y cifradas como C3: el rand() del relleno y el
// serial (0x5767913) los saca quien manda, antes de send(). La rama del Wizard (0x58c1b6 y los demás) y 0x55cbc0
// también mandan el 1E de abajo: [hacia] en 0, [espalda] en 0 salvo con la 0x4c (la celda de 0x4e9920, como el
// caballero) y la clave de la magia (0x77f3bad) en la 8, la 9 y la 0x4c (08, P-C221).
inline constexpr std::uint8_t TeleportMagicOpcode = 0x1c;
inline constexpr std::uint8_t TargetTeleportOpcode = 0xb0;
// La clave que va sin blanco (0xffff en 0x58c8f0, 0x591865 y 0x55e683).
inline constexpr std::uint16_t NoSkillTargetKey = 0xffff;

// C1 06 1C 00 [x] [y] (0x593151..0x59350e): el Teleport (la skill 6) a una celda. El GS 0.97 (CGTeleportMagicRecv,
// 0x426450) toma el 0 como la skill 6.
[[nodiscard]] Outgoing WriteTeleportMagic(std::uint8_t x, std::uint8_t y);

// C1 07 B0 [clave L H] [x] [y] (0x59293b..0x592d1b): el Teleport Ally (0xf) del elegido a una celda, con la clave en
// little endian (el WORD de +0x1f4 tal cual), como la lee el GS 0.97 (CGTargetTeleportRecv, 0x426870).
[[nodiscard]] Outgoing WriteTargetTeleport(std::uint16_t key, std::uint8_t x, std::uint8_t y);

// El 1E que manda el cliente (el elfo, 0x561f60 en 0x562b36 y 0x563f5a, y las ramas del caballero, 0x582439..0x5896a3):
// C1 0C 1E [skill] [X] [Y] [rumbo] [hacia] [espalda] [clave H] [clave L] [clave de la magia], con el XOR32 sobre los
// bytes 3 a 11 y en C3 como el 19. El GameServer lee solo los 7 primeros (PMSG_DURATION_MAGIC_RECV,
// CGDurationMagicRecv 0x427870).
struct DurationMagicSend
{
    std::uint8_t skill = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t direction = 0;
    // ((dx + 8) << 4) | ((dy − 8) & 0xf), con la distancia de la celda lógica al blanco entre −8 y 7.
    std::uint8_t towards = 0;
    // El rumbo dado vuelta (+180°) en la misma escala de 0 a 255; las 0x2a, 0x3e, 0x41 y 0x4c del caballero ponen la
    // celda de 0x4e9920.
    std::uint8_t back = 0;
    std::uint16_t key = 0;
    // La clave de la magia (0x57cf90, +0x1bd) de la 0x4c del caballero; 0 en las demás.
    std::uint8_t magicKey = 0;
};
[[nodiscard]] Outgoing WriteDurationMagic(const DurationMagicSend& send);

} // namespace Client::Protocol::Skill
