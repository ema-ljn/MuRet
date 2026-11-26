#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// Reads the process's own memory without crashing when it cannot: the original's IsBadReadPtr (0x68289b, 0x6828c2,
// 0x682bcd).
namespace Client::Diagnostics
{
class IMemoryReader
{
public:
    virtual ~IMemoryReader() = default;

    // false if any byte is unreadable. Allocates nothing: it runs after the crash.
    [[nodiscard]] virtual bool Read(std::uint64_t address, std::span<std::byte> out) const = 0;

protected:
    IMemoryReader() = default;
    IMemoryReader(const IMemoryReader&) = default;
    IMemoryReader& operator=(const IMemoryReader&) = default;
    IMemoryReader(IMemoryReader&&) = default;
    IMemoryReader& operator=(IMemoryReader&&) = default;
};
} // namespace Client::Diagnostics
