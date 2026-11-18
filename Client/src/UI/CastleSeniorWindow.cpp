#include "UI/CastleSeniorWindow.h"

#include "UI/OldWindowBase.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Client::UI
{
std::array<OldButton, 7> CastleSeniorWindow::DefaultButtons()
{
    std::array<OldButton, 7> buttons{};
    for (std::size_t i = 0; i < buttons.size(); ++i)
    {
        buttons[i].base.id = -320 - static_cast<std::int32_t>(i);
        buttons[i].base.width = 49;
        buttons[i].base.height = 16;
    }
    return buttons;
}
} // namespace Client::UI
