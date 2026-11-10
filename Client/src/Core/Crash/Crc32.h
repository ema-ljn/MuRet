#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// El CRC-32 con que 0x683bd0 firma el ejecutable en MuError.dmp: el de siempre (el de zlib y
// PNG), polinomio reflejado 0xEDB88320 (la tabla de 0x683e00), arranca en 0xFFFFFFFF
// (0x683cea) y se niega al final (0x683d72). Se alimenta de a pedazos, como el original recorre el
// archivo mapeado de a 10 MB, y no pide memoria: la tabla es constexpr.
//
// Propio y no zlib: son pocas líneas y el cliente no enlaza zlib (regla 10). El manejador de
// caídas lo corre después de la caída.
namespace Client::Core::Crash
{
class Crc32
{
public:
    void Update(std::span<const std::byte> bytes);
    [[nodiscard]] std::uint32_t Value() const;

private:
    std::uint32_t m_state = 0xFFFFFFFF;
};
} // namespace Client::Core::Crash
