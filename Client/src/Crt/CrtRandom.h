#pragma once

#include <cstdint>

namespace Client::Crt
{
// 0x69fd70, the original's CRT rand(): s = s * 0x343fd + 0x269ec3, returns (s >> 16) & 0x7fff. Seeded once by
// srand(time(NULL)) (0x4bad9a).
class CrtRandom
{
public:
    explicit CrtRandom(std::uint32_t seed = 1) noexcept : m_state(seed) {}
    [[nodiscard]] int Next() noexcept;
    // `count` calls of Next() whose results nobody reads, in O(log count) steps.
    void Discard(std::uint32_t count) noexcept;
    [[nodiscard]] std::uint32_t State() const noexcept
    {
        return m_state;
    }

private:
    std::uint32_t m_state;
};
} // namespace Client::Crt
