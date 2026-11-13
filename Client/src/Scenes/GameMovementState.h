#pragma once
#include <array>
#include <cstdint>
#include <optional>

// Globales durables de MoveHero y 1C: research06, Caminar y Los cambios de mapa.
namespace Client::Scenes
{
struct GameMovementState
{
    std::int32_t mouseUpdateTime = 0;
    // .data[6c8dcc] = 6 antes del primer SendMove.
    std::int32_t mouseUpdateTimeMax = 6;
    bool warpPending = false;
    std::int32_t gateRequestBlocked = 0;
    std::uint32_t lastTeleportMs = 0;
    std::int32_t arrivalYaw = 0;
    std::int32_t movementTarget = 0;
    std::int32_t facingMode = 0; // 0x78abb18, cero desde la selección
    // 0x7d366d4, el flanco del clic izquierdo: lo prende el paso con el apretón (antes de 0x623e70) y lo apagan
    // quien lo usa, la UI primero (06 § HUD y ventanas, "El clic es primero de la UI"), y el botón suelto.
    bool pendingClick = false;
    bool inputBlocked = false;
    bool movementBlocked = false;
    float heldSince = 0.0f;
    float heldElapsed = 0.0f;
    bool leftHeld = false;
    bool previousLeftDown = false;
    // 0x7d366ec, el flanco del soltado: lo prende WM_LBUTTONUP (0x4b3cea) con el mouse de ese momento (0x576358c y
    // 0x5763590), lo apagan WM_LBUTTONDOWN (0x4b3947), cualquier mensaje con el mouse en otro lugar (0x4b344d) y quien
    // lo usa (los botones viejos y la barra del título, 0x45b200 y 0x492ac0).
    bool releasedClick = false;
    std::int32_t releaseX = 0;
    std::int32_t releaseY = 0;
    // SetCursorPos(x, y) de 0x56b77f (CL9 hito 11): el pedido queda acá, en píxeles de la pantalla de Windows, y mover
    // el cursor es de la plataforma (como el pedido del 30 del hito 6). El último pedido gana; nadie lo consume
    // todavía.
    std::optional<std::array<std::int32_t, 2>> cursorWarpRequest{};
    std::array<std::uint8_t, 256> skillDepths{};
    float selectedSkillRange = 0.0f;
    std::uint32_t targetX = 0;
    std::uint32_t targetY = 0;
    float actionInputTime = 0.0f;
    float actionInputElapsed = 0.0f;
    // 0x6c8ebc (.data −1): el lugar del personaje al que va el héroe. Lo escriben el clic sobre el NPC elegido
    // (0x59dad3, CL9 hito 2) y sobre un personaje atacable (0x59d3de, hito 3); 0x5650c0 lo lee con +0x379 en 3.
    std::int32_t attackTarget = -1;
    // 0x6c8e54 (.data −1): el NPC al que le habla la llegada (0x5650c0 con +0x379 en 2). Lo escribe el clic sobre el
    // NPC elegido (0x59db1f) y lo vuelven a −1 la llegada (0x56959b) y la vuelta a la selección (0x42fdf8).
    std::int32_t chosenNpc = -1;
    // 0x7798bf0 y 0x77f36cc (.bss): el tipo del NPC elegido (+2, MOVSX) y su ángulo z (+0x24, el dword crudo), que
    // escribe el mismo clic (0x59db25 y 0x59db2b). Los lee otro caso de 0x5650c0 (0x569859.., sin portar).
    std::int32_t chosenNpcType = 0;
    std::uint32_t chosenNpcAngle = 0;
};
} // namespace Client::Scenes
