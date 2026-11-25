#pragma once

#include "Core/Expected.h"
#include "Data/Models/Model.h"

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

// El esqueleto de un modelo como lo deja 0x4f13d0 después de leerlo: cada key con su cuaternión
// (0x5fcd20 por key) y cada acción con lo que la pose y el avance miran
// (Client/docs/research/06-render-escenas-y-ui.md, "Cuánto avanza la animación").
namespace Client::Render::Models
{
struct BoneKey
{
    glm::vec3 position{};
    // Los ángulos del archivo, en radianes: el hueso de la cabeza los usa para girar (0x4eb860).
    glm::vec3 angles{};
    glm::quat rotation = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
};

struct SkeletonBone
{
    // No se posa: su matriz queda como estaba (0x4eb860).
    bool dummy = false;
    std::int16_t parent = -1;
    // [acción][key]. Un dummy no tiene ninguna.
    std::vector<std::vector<BoneKey>> actions;
};

// La acción de 0x10 bytes del original.
struct ActionInfo
{
    bool repeats = true;        // +0 en 0, como lo escribe 0x4f13d0
    float speed = 0.0f;         // +4, keys por paso: 0x4f13d0 no lo escribe (Q-C87)
    std::int16_t keyCount = 0;  // +8
    bool lockPositions = false; // +10
};

struct Skeleton
{
    std::vector<SkeletonBone> bones;
    std::vector<ActionInfo> actions;
    // +0x54: −1 como lo deja 0x4f1f20(0); el del personaje lo pone 0x60dbe0
    // (Data::Models::PlayerHeadBone).
    std::int16_t headBone = -1;
};

// Un hueso cuyo padre no va antes que él: la pose lo concatenaría con una matriz que todavía no
// calculó (Q-C108).
struct SkeletonError
{
    std::size_t bone = 0;
    std::int16_t parent = -1;
};

// Los cuaterniones de cada key, como 0x4f13d0. Un padre que no es −1 ni un hueso de antes es un
// error: NPC\BloodCastle01.bmd tiene dos (Q-C108), y qué hacer con ellos es de la CL9.
[[nodiscard]] Core::Expected<Skeleton, SkeletonError> MakeSkeleton(const Data::Models::Model& model);

// MakeSkeleton más lo que 0x60dbe0 le pone a Player.bmd: la velocidad de cada acción
// (Data::Models::PlayerActionSpeed; las 57 sin escritura quedan en 0, Q-C87), las cuatro que no
// repiten (PlayerActionsWithoutRepeat) y el hueso de la cabeza (PlayerHeadBone).
[[nodiscard]] Core::Expected<Skeleton, SkeletonError> MakePlayerSkeleton(const Data::Models::Model& model);
} // namespace Client::Render::Models
