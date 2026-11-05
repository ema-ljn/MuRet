#pragma once

#include "Render/Pass3D.h"
#include "World/TerrainCulling.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>

// 06 § La cámara del juego: 0x635ae0 por frame dibujado, con el héroe del último paso.
// El recorrido CCameraMove de 0x6e57a8 (paquete BD 00) queda fuera de esta cámara.
namespace Client::World
{
struct GameCamera
{
    float yaw = -45.0f;
    float distance = 1000.0f;
    float zoom = 0.0f;
    int level = 0;
    float farPlane = 2000.0f;
    float fixedHeight = 1200.0f;
    float targetDistance = 1000.0f;
    // D se suaviza después de calcular la posición: los dibujos intermedios usan la D anterior.
    std::optional<float> lastFrameDistance;
};

struct GameCameraInput
{
    glm::vec3 hero{};
    std::uint8_t cellAttribute = 0;
    std::uint8_t map = 0;
    CullingScene scene = CullingScene::Game;
    float shake = 0.0f;
    float worldTime = 0.0f;
    bool webzen = false;
    glm::vec3 effectTarget{};
};

[[nodiscard]] Render::Camera3D StepGameCamera(GameCamera& state, const GameCameraInput& input);

// Usa el héroe del último paso lógico con la D, el zoom y el far del último frame original,
// sin avanzar el estado. Antes del primer frame usa la D y el far iniciales.
[[nodiscard]] Render::Camera3D GameCameraOf(const GameCamera& state, const GameCameraInput& input);
} // namespace Client::World
