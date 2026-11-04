#pragma once

#include "Render/Models/HalfLifeMath.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

// Lo que cuelga de un personaje: las dos manos, las alas y la mascota, cada una de un hueso de
// Player.bmd con un desplazamiento (0x511290 y 0x50ba30), como las cuelga 0x509340 con la bandera en
// 0: la raíz de 0x4ec190 y la pose de 0x4eb860 con su séptimo argumento en 1 (PoseAttached). Los
// vértices van después con SkinMesh y el objeto del personaje, como las partes del cuerpo.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario". La espada en
// el 33 y la mascota en el 34 están **verificadas** contra la traza de la selección; las alas,
// *estáticas*.
namespace Client::Render::Models
{
enum class AttachmentSlot : std::uint8_t
{
    RightHand, // +0x2bd
    LeftHand,  // +0x2dd
    Wings,     // +0x2fd
    Pet,       // +0x31d
};

struct Attachment
{
    std::int16_t bone = 0;
    glm::vec3 offset{};
};

// La mano derecha en el hueso 33 y la izquierda en el 42, sin desplazamiento; las alas en el 47 con
// (0, 0, 15); la mascota en el 34 con (20, 0, 0).
[[nodiscard]] Attachment AttachmentOf(AttachmentSlot slot);

// Q-C98: de las mascotas, solo 0x1c13 (Helper02) se dibuja en este camino.
inline constexpr std::uint16_t DrawnPet = 0x1c13;
[[nodiscard]] constexpr bool DrawsThePet(std::uint16_t model)
{
    return model == DrawnPet;
}

// 0x4ec190: el hueso del que cuelga, con el desplazamiento girado por él y sumado a su traslación.
// La escala del personaje no: va en cada vértice (verificado: con ella, la mascota cae a 1,8).
[[nodiscard]] BoneTransform AttachmentRoot(const BoneTransform& parent, const glm::vec3& offset);

// El camino de 0x509340 con su bandera (el byte bajo: un modelo de 0x212 a 0x1011, un arma): un
// AngleMatrix por modelo, en grados (0x5fc8f0), con su traslación, encadenado al hueso
// (R_ConcatTransforms, 0x5fca90).
struct HangingPlacement
{
    glm::vec3 angles{};
    glm::vec3 translation{};
};

// El del carcaj, las saetas (0xa19) y las flechas (0xa21), que 0x50ba30 cuelga a la espalda del
// hueso de las alas: (70, 0, 90) y (−10, 5, 10) (0x509535..0x5095bf).
inline constexpr HangingPlacement QuiverPlacement{{70.0f, 0.0f, 90.0f}, {-10.0f, 5.0f, 10.0f}};

// La raíz de lo que cuelga por ese camino: el hueso por la matriz de su lugar. Como en el otro, la
// escala del personaje va en cada vértice.
[[nodiscard]] BoneTransform PlacedRoot(const BoneTransform& parent, const HangingPlacement& placement);
} // namespace Client::Render::Models
