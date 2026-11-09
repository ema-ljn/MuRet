#include "App/CrtPacketPadding.h"

#include "Render/Models/Cloth.h"

#include <cstddef>

namespace Client::App
{
CrtPacketPadding::CrtPacketPadding(Render::Models::CrtRandom& random) : m_random(random) {}

std::byte CrtPacketPadding::NextPadding()
{
    // 03 § C3/C4: un rand síncrono y su byte bajo, después de XOR32 y antes del cifrado.
    constexpr unsigned int LowByteMask = 0xffu;
    return static_cast<std::byte>(static_cast<unsigned int>(m_random.Next()) & LowByteMask);
}
} // namespace Client::App
