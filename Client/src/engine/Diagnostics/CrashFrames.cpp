#include "Diagnostics/CrashFrames.h"

#include "Diagnostics/CrashReport.h"
#include "Diagnostics/IMemoryReader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Diagnostics
{
namespace
{
constexpr std::size_t MaxPointerSize = 8;
constexpr std::size_t DwordSize = 4;
constexpr unsigned BitsPerByte = 8;

// Little-endian, `size` 4 or 8.
std::optional<std::uint64_t> ReadUnsigned(const IMemoryReader& memory, std::uint64_t address, std::size_t size)
{
    std::array<std::byte, MaxPointerSize> bytes{};
    if (size > bytes.size() || !memory.Read(address, std::span(bytes).first(size)))
    {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t i = size; i > 0; --i)
    {
        value = (value << BitsPerByte) | std::to_integer<std::uint64_t>(bytes[i - 1]);
    }
    return value;
}

std::uint32_t Low32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value);
}

// 0x682890
std::optional<CrashFrame> ReadFrame(std::uint64_t framePointer, std::size_t pointerSize, const IMemoryReader& memory)
{
    if (!ReadUnsigned(memory, framePointer, pointerSize))
    {
        return std::nullopt;
    }
    CrashFrame frame;
    frame.framePointer = Low32(framePointer);
    frame.returnAddress = Low32(ReadUnsigned(memory, framePointer + pointerSize, pointerSize).value_or(0));
    const std::uint64_t arguments = framePointer + 2 * pointerSize;
    for (std::size_t i = 0; i < frame.arguments.size(); ++i)
    {
        const auto value = ReadUnsigned(memory, arguments + i * DwordSize, DwordSize);
        if (!value)
        {
            break; // the rest stay 0 (0x6828ca)
        }
        frame.arguments[i] = Low32(*value);
    }
    return frame;
}
} // namespace

std::size_t WalkCrashFrames(std::uint64_t framePointer, std::size_t pointerSize, const IMemoryReader& memory,
                            std::span<CrashFrame, MaxCrashFrames> frames)
{
    std::size_t count = 0;
    std::uint64_t current = framePointer;
    while (count < frames.size())
    {
        const std::optional<CrashFrame> frame = ReadFrame(current, pointerSize, memory);
        if (!frame)
        {
            break;
        }
        frames[count++] = *frame;
        // The saved previous frame; followed only if readable (0x682bc8..0x682bd5).
        const std::optional<std::uint64_t> previous = ReadUnsigned(memory, current, pointerSize);
        if (!previous || !ReadUnsigned(memory, *previous, pointerSize))
        {
            break;
        }
        current = *previous;
    }
    return count;
}
} // namespace Client::Diagnostics
