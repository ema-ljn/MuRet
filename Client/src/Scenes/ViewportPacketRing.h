#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// La cola de los últimos 0x12 (CL9 hito 1): diez ranuras de 0x100 bytes en 0x77f27b4, sus tamaños en 0x77e1bb0 y la
// cuenta, un char con signo, en 0x77f3ba0. La escriben el dispatcher antes de 0x4c8860 (0x4e372e..0x4e37b8) y
// 0x4c8860 otra vez con cada registro de estado 1 (0x4c8a43..0x4c8ad4). Su único lector es 0x557830, un volcado en
// hexadecimal que nadie llama, y 0x4c12a0, otra copia del alta, tampoco tiene llamadores (*estático*): nada la lee.
// En la .bss: todo en 0.
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
