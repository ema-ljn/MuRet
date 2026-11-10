#include "Core/Crash/CrashFrames.h"

#include "Core/Crash/CrashReport.h"
#include "Core/Crash/IMemoryReader.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>

// Client::Core::Crash::WalkCrashFrames: la cadena de EBP de 0x6829f0 sobre una memoria inventada,
// con huecos que no se pueden leer.
namespace
{
using Client::Core::Crash::CrashFrame;
using Client::Core::Crash::IMemoryReader;
using Client::Core::Crash::MaxCrashFrames;
using Client::Core::Crash::WalkCrashFrames;

// Una memoria de a bytes: lo que no se escribió no se puede leer.
class FakeMemory final : public IMemoryReader
{
public:
    void Write(std::uint64_t address, std::uint64_t value, std::size_t size)
    {
        constexpr unsigned BitsPerByte = 8;
        for (std::size_t i = 0; i < size; ++i)
        {
            m_bytes[address + i] = static_cast<std::byte>(value >> (BitsPerByte * i));
        }
    }

    [[nodiscard]] bool Read(std::uint64_t address, std::span<std::byte> out) const override
    {
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            const auto found = m_bytes.find(address + i);
            if (found == m_bytes.end())
            {
                return false;
            }
            out[i] = found->second;
        }
        return true;
    }

private:
    std::map<std::uint64_t, std::byte> m_bytes;
};

// Un frame de 32 bits: el anterior guardado, la vuelta y `arguments` dwords.
void WriteFrame32(FakeMemory& memory, std::uint32_t frame, std::uint32_t previous, std::uint32_t returnAddress,
                  std::size_t arguments)
{
    memory.Write(frame, previous, 4);
    memory.Write(frame + 4, returnAddress, 4);
    for (std::uint32_t i = 0; i < arguments; ++i)
    {
        memory.Write(frame + 8 + 4 * i, 0xA000 + i, 4);
    }
}
} // namespace

// Tres frames encadenados; el último apunta a algo que no se puede leer y ahí termina.
TEST(CrashFramesTest, FollowsTheChainUntilItCannotBeRead)
{
    FakeMemory memory;
    WriteFrame32(memory, 0x1000, 0x1100, 0x401010, 16);
    WriteFrame32(memory, 0x1100, 0x1200, 0x402020, 16);
    WriteFrame32(memory, 0x1200, 0xDEAD0000, 0x403030, 16);
    std::array<CrashFrame, MaxCrashFrames> frames{};

    ASSERT_EQ(WalkCrashFrames(0x1000, 4, memory, frames), 3u);
    EXPECT_EQ(frames[0].framePointer, 0x1000u);
    EXPECT_EQ(frames[0].returnAddress, 0x401010u);
    EXPECT_EQ(frames[0].arguments[0], 0xA000u);
    EXPECT_EQ(frames[0].arguments[15], 0xA00Fu);
    EXPECT_EQ(frames[1].framePointer, 0x1100u);
    EXPECT_EQ(frames[2].framePointer, 0x1200u);
    EXPECT_EQ(frames[2].returnAddress, 0x403030u);
}

// Un frame que no se puede leer no cuenta.
TEST(CrashFramesTest, GivesNothingForAnUnreadableFramePointer)
{
    const FakeMemory memory;
    std::array<CrashFrame, MaxCrashFrames> frames{};

    EXPECT_EQ(WalkCrashFrames(0x1000, 4, memory, frames), 0u);
}

// Los dwords se leen de a uno hasta el primero que falta; los demás quedan en 0 (0x6828ca). La
// vuelta que no se puede leer, también en 0.
TEST(CrashFramesTest, LeavesWhatCannotBeReadInZero)
{
    FakeMemory memory;
    memory.Write(0x2000, 0xDEAD0000, 4);
    std::array<CrashFrame, MaxCrashFrames> frames{};
    ASSERT_EQ(WalkCrashFrames(0x2000, 4, memory, frames), 1u);
    EXPECT_EQ(frames[0].returnAddress, 0u);
    EXPECT_EQ(frames[0].arguments[0], 0u);

    WriteFrame32(memory, 0x3000, 0xDEAD0000, 0x404040, 5);
    ASSERT_EQ(WalkCrashFrames(0x3000, 4, memory, frames), 1u);
    EXPECT_EQ(frames[0].arguments[4], 0xA004u);
    EXPECT_EQ(frames[0].arguments[5], 0u);
}

// Una cadena que vuelve sobre sí misma corta en 64 (0x682be2).
TEST(CrashFramesTest, StopsAt64Frames)
{
    FakeMemory memory;
    WriteFrame32(memory, 0x5000, 0x5000, 0x405050, 16);
    std::array<CrashFrame, MaxCrashFrames> frames{};

    EXPECT_EQ(WalkCrashFrames(0x5000, 4, memory, frames), MaxCrashFrames);
}

// En MuRet de 64 bits los punteros miden 8: la vuelta va después de 8 bytes, los dwords después de
// 16, y cada dirección se guarda con sus 32 bits de abajo.
TEST(CrashFramesTest, ReadsEightBytePointersAndKeepsTheLow32Bits)
{
    FakeMemory memory;
    memory.Write(0x7FFE00001000, 0x7FFE00001100, 8);
    memory.Write(0x7FFE00001008, 0x00007FF712345678, 8);
    memory.Write(0x7FFE00001010, 0x11, 4);
    memory.Write(0x7FFE00001014, 0x22, 4);
    memory.Write(0x7FFE00001100, 0x7FFE0DEAD000, 8);
    memory.Write(0x7FFE00001108, 0x00007FF712340000, 8);
    std::array<CrashFrame, MaxCrashFrames> frames{};

    ASSERT_EQ(WalkCrashFrames(0x7FFE00001000, 8, memory, frames), 2u);
    EXPECT_EQ(frames[0].framePointer, 0x00001000u);
    EXPECT_EQ(frames[0].returnAddress, 0x12345678u);
    EXPECT_EQ(frames[0].arguments[0], 0x11u);
    EXPECT_EQ(frames[0].arguments[1], 0x22u);
    EXPECT_EQ(frames[0].arguments[2], 0u);
    EXPECT_EQ(frames[1].framePointer, 0x00001100u);
    EXPECT_EQ(frames[1].returnAddress, 0x12340000u);
}
