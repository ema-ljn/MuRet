#pragma once

#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>
#include <optional>
#include <span>

// El dibujo de un personaje en el ModelBatch, como 0x50ba30 en la selección: el cuerpo de
// Player.bmd posado, las partes con las matrices de esa pose (0x60cf70), lo que cuelga desde su
// hueso (0x509340, Render/Models/Attachment.h) y los pases de brillo de cada parte por su nivel
// (0x60b3d0, Render/Models/ItemGlow.h), cada malla con su textura y la mezcla de 0x4ecc30.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario" y "La selección
// de personaje"; el plan de la CL6 (CL6.3). Sin la luz por vértice (en la selección el personaje va
// con un color por malla), sin el contorno bajo el mouse (P-C18) y sin el color de 0x609640 (P-C14).
namespace Client::Scenes
{
// Los modelos que pide el dibujo: los del personaje y los de sus ítems, con su esqueleto y el slot
// de la textura de cada malla. Los carga la escena de carga (GameModels).
class IModelLibrary
{
public:
    virtual ~IModelLibrary() = default;
    [[nodiscard]] virtual const Data::Models::Model* Find(std::uint16_t model) const = 0;
    // El de Player.bmd para el 0x20a, con sus velocidades y su cabeza (MakePlayerSkeleton).
    [[nodiscard]] virtual const Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const = 0;
    // Uno por malla, en su orden; vacío si no se cargaron sus texturas.
    [[nodiscard]] virtual std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const = 0;
};

struct DrawnCharacter
{
    GameLogic::CharacterAppearance appearance;
    Render::Models::AnimationState body;
    Render::Models::ObjectTransform object;
    glm::vec2 headAngles{};
    // La mascota y las alas avanzan por tiempo (Render/Models/Animation.h).
    Render::Models::AnimationState pet;
    Render::Models::AnimationState wings;
    // La luz del personaje: la base de 0x50ba30 más +0xf4, 0,4 y 1,4 el elegido en la selección.
    float light = 0.4f;
};

// Los slots de las texturas de los pases de brillo (06): 0x5fc el 0x8, 0x5c0 el 0x4 y 0x620 los
// 0x200, 0x800 y 0x1000.
inline constexpr Render::TextureSlot ShinyTextureSlot{0x5fc};
inline constexpr Render::TextureSlot ChromeTextureSlot{0x5c0};
inline constexpr Render::TextureSlot MetalTextureSlot{0x620};

// La piel de una clase, `modelo + 0x98` (0x50d41c..0x50d431): 2 · (clase & 7) + (clase >> 3).
[[nodiscard]] constexpr std::uint8_t SkinIndexOf(std::uint8_t characterClass)
{
    return static_cast<std::uint8_t>(2 * (characterClass & 7) + (characterClass >> 3));
}

// El slot que usa una malla: la de la piel (0x12e) va a 0x12e + la piel, la del pelo (0x137) a
// 0x12f + la piel, y la oculta (0x12d) no se dibuja (06 § El modelo en el binario).
[[nodiscard]] std::optional<Render::TextureSlot> MeshSlotOf(Render::TextureSlot assigned, std::uint8_t characterClass);

// 0x50ba30: el cuerpo posado con `body` y la cabeza, las cinco partes y la cabeza de la apariencia,
// las manos, las alas y la mascota que se dibuja (Q-C98). Cada malla va con su textura, opaca o, con
// una OZT (de cuatro componentes), con la mezcla normal, y el color de la luz por el factor de su
// nivel; después, sus pases de brillo, aditivos. Un modelo que la biblioteca no tiene no se dibuja.
void DrawCharacter(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnCharacter& character,
                   Core::Duration frameClock);
} // namespace Client::Scenes
