#pragma once

#include "Core/Expected.h"
#include "Models/Skeleton.h"

#include <cstdint>
#include <optional>
#include <string>

// 0x60dc20..0x60dc7a: the BMDs constructed before the logical base.
namespace Client::Assets
{
class ConstructedBmdPrefix final
{
public:
    // The count of objects constructed before the base, not a physical address.
    [[nodiscard]] Core::Expected<void, std::string> Initialize(std::uint16_t constructedBeforeBase);
    // Only the action count of 0 (0x6154b3) is proven, by 0x4ea030; the BMD's other fields are not modeled.
    [[nodiscard]] const Models::Skeleton* BorrowForSetAction() const;
    [[nodiscard]] std::optional<std::uint16_t> Offset() const;

private:
    std::optional<std::uint16_t> m_offset;
    Models::Skeleton m_actionGuard;
};
} // namespace Client::Assets
