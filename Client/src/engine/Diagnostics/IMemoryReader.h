#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// Leer memoria del propio proceso sin caerse si no se puede: el IsBadReadPtr del original
// (0x68289b, 0x6828c2, 0x682bcd). El manejador de caídas tiene uno por sistema
// (Crash::CrashHandler) y los tests, una memoria inventada.
namespace Client::Core::Crash
{
class IMemoryReader
{
public:
    virtual ~IMemoryReader() = default;

    // Copia `out.size()` bytes desde `address` y devuelve true; si alguno no se puede leer,
    // devuelve false. No pide memoria: corre después de la caída.
    [[nodiscard]] virtual bool Read(std::uint64_t address, std::span<std::byte> out) const = 0;

protected:
    IMemoryReader() = default;
    IMemoryReader(const IMemoryReader&) = default;
    IMemoryReader& operator=(const IMemoryReader&) = default;
    IMemoryReader(IMemoryReader&&) = default;
    IMemoryReader& operator=(IMemoryReader&&) = default;
};
} // namespace Client::Core::Crash
