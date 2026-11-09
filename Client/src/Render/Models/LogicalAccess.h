#pragma once

#include <cstdint>

namespace Client::Render::Models
{
class CrtRandom;
inline constexpr std::uint16_t SmallLogicalCloseRandoms = 4;
inline constexpr std::uint16_t OwnRecordCloseRandoms = 3848;

// Proyección en claro de un acceso normal coherente: profundidad byte con wrap y costo del
// último cierre en su sitio. No representa lookup, payloads, vptr, fallos ni estado nativo inicial.
class LogicalAccess
{
public:
    LogicalAccess(std::uint8_t& depth, CrtRandom& random, std::uint16_t closeRandoms);
    LogicalAccess(const LogicalAccess&) = delete;
    LogicalAccess& operator=(const LogicalAccess&) = delete;
    ~LogicalAccess();

private:
    std::uint8_t& m_depth;
    CrtRandom& m_random;
    std::uint16_t m_closeRandoms;
};
} // namespace Client::Render::Models
