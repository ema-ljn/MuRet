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
    float yaw = -45.0f;             // 0x7d36870, escrito por el primer paso de la escena 5.
    float distance = 1000.0f;       // 0x7d36ba4, D antes del suavizado del siguiente frame.
    float zoom = 0.0f;              // 0x7d3689c.
    int level = 0;                  // 0x7d3a0ec, leído para el far antes de volver a 0.
    float farPlane = 2000.0f;       // 0x6d5d44, sin el factor 1,4 del pase 3D.
    float fixedHeight = 1200.0f;    // 0x6c9d5c, queda en 1.201 desde el primer atributo 0x40.
    float targetDistance = 1000.0f; // 0x6d5ee0, meta del suavizado.
    // D se suaviza después de calcular la posición: los dibujos intermedios usan la D anterior.
    std::optional<float> lastFrameDistance;
};

struct GameCameraInput
{
    glm::vec3 hero{};
    // Byte bajo del atributo de la celda del servidor del héroe (+0x41c/+0x420), Q-C173.
    std::uint8_t cellAttribute = 0;
    std::uint8_t map = 0;
    CullingScene scene = CullingScene::Game;
    float shake = 0.0f;     // 0x7d325c8, ya reducido por los pasos lógicos.
    float worldTime = 0.0f; // 0x5767b88, en milisegundos, para el balanceo del mapa 5.
    bool webzen = false;    // El nombre contiene "webzen" (0x576960).
    // 0x724588/0x44dc60: elige el desvío si alguna componente es ordenada y distinta de cero.
    // Con invalid enmascarada, ceros con signo y NaN por sí solos conservan el héroe.
    glm::vec3 effectTarget{};
};

// 0x44dc60: el punto que sigue la cámara y que el dibujo del juego pasa a 0x5fbb00 como centro del
// trapecio (0x637316): el desvío de 0x724588 si alguna componente es ordenada y distinta de cero; si no,
// el héroe.
[[nodiscard]] glm::vec3 CameraTargetOf(const GameCameraInput& input);

// 0x635ae0: un frame original dibujado, con el héroe del último paso lógico.
[[nodiscard]] Render::Camera3D StepGameCamera(GameCamera& state, const GameCameraInput& input);

// Usa el héroe del último paso lógico con la D, el zoom y el far del último frame original,
// sin avanzar el estado. Antes del primer frame usa la D y el far iniciales.
[[nodiscard]] Render::Camera3D GameCameraOf(const GameCamera& state, const GameCameraInput& input);
} // namespace Client::World
