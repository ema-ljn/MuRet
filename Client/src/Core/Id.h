#pragma once

#include <concepts>
#include <cstdint>

namespace Client::Core
{
template <typename Rep>
concept IdRepresentation =
    std::same_as<Rep, std::int8_t> || std::same_as<Rep, std::uint8_t> || std::same_as<Rep, std::int16_t> ||
    std::same_as<Rep, std::uint16_t> || std::same_as<Rep, std::int32_t> || std::same_as<Rep, std::uint32_t>;

// Keeps two id domains apart; it promises no wire layout and no calling convention (CODING_RULES §7).
template <class Tag, IdRepresentation Rep> class Id
{
public:
    constexpr explicit Id(Rep value) noexcept : m_value(value) {}

    [[nodiscard]] constexpr Rep Value() const noexcept
    {
        return m_value;
    }

    constexpr bool operator==(const Id&) const noexcept = default;

private:
    Rep m_value;
};
} // namespace Client::Core
