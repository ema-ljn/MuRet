#pragma once
#include <cstdint>
#include <array>

// Globales durables de MoveHero y 1C: research06, Caminar y Los cambios de mapa.
namespace Client::Scenes
{
// 0x7758580, 0x7758584 y 0x7758588 (.bss 0): lo que 0x57cfc0 deja antes de la skill del arma de cada mano (0x57d5ed..
// 0x57d61b): 1, el lugar de la skill del héroe (+0x425) y el elegido (0x6c8e30) si 0x558bb0 dice que se ataca, o −1.
// Sin línea de vista, 0, 0 y −1 (0x57d760). Los leen 0x559890, 0x559510 y las cuatro funciones por clase (CL9 hito 4).
struct WeaponSkillRequest
{
    std::uint32_t active = 0;
    std::uint32_t slot = 0;
    std::int32_t target = 0;
};

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
    // 0x6c8e50 (.data −1): el personaje hacia el que camina MoveHero (0x59b94b, sin portar). Lo ponen en −1 0x57cfc0 al
    // usar una skill (0x57d294), el clic (0x59b8f3, 0x59b972) y otros paquetes (0x4d6977, 0x4e415e), sin portar.
    std::int32_t followTarget = -1;
    WeaponSkillRequest weaponSkill{};
};
} // namespace Client::Scenes
