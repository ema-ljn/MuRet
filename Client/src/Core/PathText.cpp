#include "Core/PathText.h"

namespace Client::Core
{
std::string PathText(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}
} // namespace Client::Core
