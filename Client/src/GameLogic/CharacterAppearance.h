#pragma once

#include "Protocol/Character/Opcodes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// La ropa de un personaje de la selección: de la clase y los 17 bytes del equipo de la lista a los
// modelos de cada parte, como 0x517270, 0x5139f0 y 0x512930.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los personajes de la lista", y el plan
// de la CL6 (CL6.3). Todo *estático*, del desensamblado.
namespace Client::GameLogic
{
// Las cinco partes del cuerpo, en el orden de 0x5139f0 (+0x218 a +0x298 del personaje).
enum class BodyPart : std::uint8_t
{
    Helm,
    Armor,
    Pants,
    Gloves,
    Boots,
};
inline constexpr std::size_t BodyPartCount = 5;

// Lo que lleva en una parte: el modelo (0x212 más el tipo del ítem, o una parte de la clase), el
// nivel (0x513340) y si es excelente o de un set (los bits de b[9] y b[10], *inferido* qué es el
// segundo).
struct WornItem
{
    std::uint16_t model = 0;
    std::uint8_t level = 0;
    bool excellent = false;
    bool ancient = false;

    friend bool operator==(const WornItem&, const WornItem&) = default;
};

struct CharacterAppearance
{
    // La clase en los bits 0 a 2 y la segunda clase en el 3 (Protocol::Character::CharacterClassOf).
    std::uint8_t characterClass = 0;
    // El tercer argumento de 0x517270: 0 desde la lista (0x4c20a0).
    std::uint8_t skin = 0;
    std::array<WornItem, BodyPartCount> bodyParts{};
    std::optional<WornItem> rightHand;
    std::optional<WornItem> leftHand;
    std::optional<std::uint16_t> wings;
    std::optional<std::uint16_t> pet;
    // 0x512930: la cabeza de la clase, que va debajo de los cascos abiertos.
    std::optional<std::uint16_t> head;
    // 0x512930: por clase y según la piel sea 0 o no.
    float scale = 0.9f;
};

using Equipment = std::span<const std::byte, Protocol::Character::EquipmentSize>;

// El modelo de `part` de la clase: HelmClass a BootClass (0x2212, 0x221a, 0x2222, 0x222a y 0x2232)
// más (clase >> 3) · 5 + (clase & 7).
[[nodiscard]] std::uint16_t ClassPartModel(BodyPart part, std::uint8_t characterClass);

// 0x513340: los 3 bits de una parte, de 0 a 7, al nivel del ítem (la tabla de 0x51338c).
[[nodiscard]] std::uint8_t ItemLevelOf(std::uint8_t bits);

// 0x512930: la cabeza de la clase (la parte del casco de la clase) debajo de un casco abierto, 0x1012,
// 0x1014 o 0x101c a 0x101f; con cualquier otro, std::nullopt (el original deja −1 en +0x1f8).
[[nodiscard]] std::optional<std::uint16_t> HeadUnder(std::uint16_t helm, std::uint8_t characterClass);

// 0x512930: la escala por clase y según la piel sea 0 o no. Las clases 5 a 7 no tienen caso en sus dos
// tablas de saltos (0x512a00 y 0x512a14) y no la tocan: std::nullopt.
[[nodiscard]] std::optional<float> ClassScale(std::uint8_t characterClass, std::uint8_t skin);

// 0x517270 fuera de la escena 2: las cinco partes de la clase y nada en las manos, las alas ni la
// mascota; la cabeza y la escala de 0x512930.
[[nodiscard]] CharacterAppearance ClassAppearance(std::uint8_t characterClass, std::uint8_t skin);

// Y 0x5139f0 con el equipo, campo por campo (06): las manos, las cinco partes (un índice 0x1ff es la
// de la clase), las alas, la mascota, los niveles y los bits de excelente y de set; después, la
// cabeza y la escala. Una mano de tipo 0xfff es "sin ítem": el original la compara con 0x1fff y le
// deja el modelo 0x1211, que nadie carga (Q-C102, diferencia nombrada).
[[nodiscard]] CharacterAppearance AppearanceOf(std::uint8_t characterClass, std::uint8_t skin, Equipment equipment);
} // namespace Client::GameLogic
