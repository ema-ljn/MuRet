#pragma once
#include <array>
#include <cstdint>
#include <optional>

// Globales durables de MoveHero y 1C: research06, Caminar y Los cambios de mapa.
namespace Client::Movement
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
    bool warpPending = false;
    std::int32_t gateRequestBlocked = 0;
    std::uint32_t lastTeleportMs = 0;
    std::int32_t arrivalYaw = 0;
    std::int32_t movementTarget = 0;
    std::int32_t facingMode = 0; // 0x78abb18, cero desde la selección
    bool inputBlocked = false;
    bool movementBlocked = false;
    float heldSince = 0.0f;
    float heldElapsed = 0.0f;
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
    // 0x576791c (.bss 0): el GetTickCount del último envío que pasó el freno de 300 ms (PassesTheActionBrake).
    std::uint32_t lastActionTick = 0;
    // CL9 hito 4. 0x77f3ba4 (.bss 0): lo pone en 0 el Aqua Beam del Wizard (0x590afd). Quién más lo escribe y lo lee,
    // sin leer.
    std::int32_t dword77f3ba4 = 0;
    // 0x6c8e50 (.data −1): el personaje hacia el que camina MoveHero (0x59b94b, sin portar). Lo pone el comando de
    // seguir de la ventana D (0x40a3e0, GameCommandsWindow) y lo vuelven a −1 0x57cfc0 al usar una skill (0x57d294),
    // el 43 que deja la party vacía (0x4e415e), la vuelta a la selección (0x4c254b), el clic (0x59b8f3, 0x59b972) y
    // el 0x4d6977, estos dos sin portar.
    std::int32_t followTarget = -1;
    WeaponSkillRequest weaponSkill{};
    // 0x77f3bad (.bss 0): el contador de la clave de las magias (0x57cf90, Skill57cf90.h), de 1 a 0x32.
    std::uint8_t magicKeyCounter = 0;
};
} // namespace Client::Movement
