#include "Kernel/HelperTable.h"

#include <cstddef>

namespace Client::Kernel
{
void TurnOffHelpersOf(HelperTable& helpers, std::size_t rider)
{
    for (HelperState& helper : helpers)
    {
        if (helper.alive && helper.rider == rider)
        {
            helper.alive = false;
        }
    }
}
} // namespace Client::Kernel
