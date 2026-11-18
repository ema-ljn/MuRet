#pragma once

#include "Render/Pass3D.h"
#include "World/TerrainCulling.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>

// 06 § La cámara del juego: 0x635ae0 por frame dibujado, con el héroe del último paso. Con el recorrido CCameraMove
// de 0x6e57a8 andando (el BD 00, Scenes::CameraTour), la cámara mira su posición (0x635ead) y toma su +0x150 de nivel.
namespace Client::World
{
// 0x635ec0: con el recorrido andando, la z del héroe ([0x743a170] + 0x18) en 300 antes de la altura de la cámara. La
// escribe la escena, dueña del héroe, y la cámara la usa para la altura.
inline constexpr float CameraTourHeroHeight = 300.0f;

// 0x4034b0: lo que el recorrido de la cámara (Scenes::CameraTour) le da a la cámara del juego mientras anda (0x4033d0).
struct GameCameraTour
{
    glm::vec3 position{};   // +4..+0xc: pisa el punto de 0x44dc60.
    std::int32_t level = 0; // +0x150: lo devuelve en EAX y la cámara guarda AX en el word 0x7d3a0ec.
};

struct GameCamera
{
    float yaw = -45.0f;             // 0x7d36870, escrito por el primer paso de la escena 5.
    float distance = 1000.0f;       // 0x7d36ba4, D antes del suavizado del siguiente frame.
    float zoom = 0.0f;              // 0x7d3689c.
    int level = 0;                  // 0x7d3a0ec, leído para el far antes de volver a 0 o al +0x150 del recorrido.
    float farPlane = 2000.0f;       // 0x6d5d44, sin el factor 1,4 del pase 3D.
    float fixedHeight = 1200.0f;    // 0x6c9d5c, queda en 1.201 desde el primer atributo 0x40.
    float targetDistance = 1000.0f; // 0x6d5ee0, meta del suavizado: 1.000 + 100 · nivel (0x6361d0).
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
    // 0x635ead y 0x637324 (0x4033d0): el recorrido de la cámara andando, que llena el dibujo del juego. Las escenas 2
    // y 4 no lo llenan: el recorrido no anda ahí (0x4c26f2 lo vuelve a 0x403300 al volver a la selección).
    std::optional<GameCameraTour> tour{};
};

// 0x44dc60: el desvío de 0x724588 si alguna componente es ordenada y distinta de cero; si no, el héroe.
[[nodiscard]] glm::vec3 CameraTargetOf(const GameCameraInput& input);

// El punto que sigue la cámara (0x635e9f..0x635ed8) y que el dibujo del juego pasa a 0x5fbb00 como centro del trapecio
// (0x637316..0x637337): CameraTargetOf y, con el recorrido andando, su posición en lugar de ese punto (0x4034b0).
[[nodiscard]] glm::vec3 FollowedPointOf(const GameCameraInput& input);

// 0x635ae0: un frame original dibujado, con el héroe del último paso lógico. Con el recorrido (fuera del mapa 6 con
// "webzen", que sale antes), la posición sale de FollowedPointOf, la altura base es CameraTourHeroHeight (salvo el 30,
// que usa la suya) y el nivel queda en el word de +0x150 (0x635ed2); sin él, en 0 (0x635eda). Ese nivel elige al final
// la meta de D (fuera de los mapas 5, 6 con "webzen" y 30) y el far del frame siguiente.
[[nodiscard]] Render::Camera3D StepGameCamera(GameCamera& state, const GameCameraInput& input);

// Usa el héroe del último paso lógico con la D, el zoom y el far del último frame original,
// sin avanzar el estado. Antes del primer frame usa la D y el far iniciales.
[[nodiscard]] Render::Camera3D GameCameraOf(const GameCamera& state, const GameCameraInput& input);
} // namespace Client::World
