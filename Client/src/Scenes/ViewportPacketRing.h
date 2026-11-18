#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// La cola de los últimos paquetes (CL9 hito 1): diez ranuras de 0x100 bytes en 0x77f27b4, sus tamaños en 0x77e1bb0 y la
// cuenta, un char con signo, en 0x77f3ba0. El nombre viene del 0x12, pero no es solo suya. La escriben el dispatcher
// 0x4e2b10, con la misma secuencia antes de cinco handlers, sin mirar el flag de cifrado ni si el paquete se lee: el
// 0x0B (0x4e3594..0x4e361c, antes de su caso en el mismo dispatcher, 0x4e3621..0x4e365e, que llama a 0x4455c0; CL9
// hito 11), el 0x12 (0x4e372e..0x4e37b8, antes de 0x4c8860), el 0x1C
// (0x4e3e98..0x4e3f1d, antes de 0x4c6e10), el 0x24 (0x4e38ca..0x4e394f, antes de 0x4d1400) y el 0x2A
// (0x4e3ace..0x4e3b53, antes de 0x4d3cc0); en MuRet, la sesión con ISessionEventSink::CopyToPacketRing. Y 0x4c8860 otra
// vez con cada registro de estado 1 del 0x12 (0x4c8a43..0x4c8ad4, Q-C801). Su único lector es 0x557830, un volcado en
// hexadecimal que nadie llama, y 0x4c12a0, otra copia del alta, tampoco tiene llamadores (*estático*): nada la lee. En
// la .bss: todo en 0, y nadie la vacía.
namespace Client::Scenes
{
inline constexpr std::size_t ViewportPacketSlots = 10;
inline constexpr std::size_t ViewportPacketSlotSize = 0x100;

struct ViewportPacketRing
{
    std::int8_t count = 0;
    std::array<std::int32_t, ViewportPacketSlots> sizes{};
    std::array<std::array<std::byte, ViewportPacketSlotSize>, ViewportPacketSlots> slots{};
};

// El alta en la cola: con la cuenta en más de 9, la deja en 9 y corre las ranuras 1..9 a 0..8 con sus tamaños (copia
// `sizes[i]` bytes); después, si el paquete mide hasta 0x100 (con signo), lo copia en la ranura de la cuenta, guarda
// su tamaño y suma 1. Un paquete más largo no entra, pero el corrimiento ya se hizo.
void StoreViewportPacket(ViewportPacketRing& ring, std::span<const std::byte> packet);
} // namespace Client::Scenes
