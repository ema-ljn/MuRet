#pragma once
#include <cstdint>
#include <array>

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
    bool cameraTour = false;
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
    std::array<std::uint8_t, 256> skillDepths{};
    float selectedSkillRange = 0.0f;
    std::uint32_t targetX = 0;
    std::uint32_t targetY = 0;
    float actionInputTime = 0.0f;
    float actionInputElapsed = 0.0f;
    // CL9 hito 3. 0x6c8ebc (.data −1): el lugar del personaje que el héroe va a pegar al llegar, que 0x5650c0 lee con
    // +0x379 en 3. Lo escribe el clic sobre un personaje atacable de MoveHero (0x59d3de, sin portar).
    std::int32_t attackTarget = -1;
    // 0x576791c (.bss 0): el GetTickCount del último envío que pasó el freno de 300 ms (PassesTheActionBrake).
    std::uint32_t lastActionTick = 0;
};
} // namespace Client::Scenes
