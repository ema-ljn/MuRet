#include "App/Resolution.h"

#include <algorithm>
#include <string>
#include <vector>

// Resolution: qué tamaños de ventana valen.
namespace Client::App
{
bool IsValidResolution(Resolution resolution, std::span<const Resolution> monitorModes)
{
    return std::ranges::find(OriginalResolutions, resolution) != OriginalResolutions.end() ||
           std::ranges::find(monitorModes, resolution) != monitorModes.end();
}

std::string DescribeValidResolutions(std::span<const Resolution> monitorModes)
{
    std::vector<Resolution> valid(OriginalResolutions.begin(), OriginalResolutions.end());
    valid.insert(valid.end(), monitorModes.begin(), monitorModes.end());
    std::ranges::sort(valid);
    const auto repeated = std::ranges::unique(valid);
    valid.erase(repeated.begin(), repeated.end());

    std::string text;
    for (const Resolution resolution : valid)
    {
        if (!text.empty())
        {
            text += ", ";
        }
        text += std::to_string(resolution.width) + " × " + std::to_string(resolution.height);
    }
    return text;
}
} // namespace Client::App
