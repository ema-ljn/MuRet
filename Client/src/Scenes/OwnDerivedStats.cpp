#include "Scenes/OwnDerivedStats.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
Core::Expected<void, std::string> UpdateOwnDerivedStatsTail(OwnDerivedStats& state, Render::Models::CrtRandom& random)
{
    state.wordef8 = static_cast<std::uint16_t>(state.wordef0 - state.wordee6);
    state.wordefa = static_cast<std::uint16_t>(state.wordef2 - state.wordee6);
    const std::int32_t minimum = state.wordee2;
    const std::int32_t first = random.Next();
    const std::int32_t divisor = static_cast<std::int32_t>(state.wordee4) - minimum + 1;
    if (divisor == 0)
        return Core::Unexpected(std::string("550d10: IDIV por cero en la cola de estadísticas"));
    const std::uint16_t saved60 = state.word60;
    state.wordefc = static_cast<std::uint16_t>((first % divisor) + minimum - state.word62);
    state.wordefe = static_cast<std::uint16_t>(state.word4e - state.wordeec);
    state.wordf00 = static_cast<std::uint16_t>(saved60 - state.wordeea);
    state.wordefe = std::min<std::uint16_t>(state.wordefe, 100);
    state.wordf00 = std::min<std::uint16_t>(state.wordf00, 100);
    const std::int32_t second = random.Next();
    state.bytef02 = static_cast<std::uint8_t>((second % 100) < state.wordefe);
    const std::int32_t third = random.Next();
    state.bytef03 = static_cast<std::uint8_t>((third % 100) < state.wordf00);
    return {};
}
} // namespace Client::Scenes
