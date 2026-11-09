#pragma once

#include "Network/IPacketPaddingSource.h"

#include <cstddef>

namespace Client::Render::Models
{
class CrtRandom;
}

// Une Network con el mismo rand de la CRT que avanza el dibujo; Network no conoce Render.
namespace Client::App
{
class CrtPacketPadding final : public Network::IPacketPaddingSource
{
public:
    // random vive más que este adapter; el adapter vive más que la Connection que lo presta.
    explicit CrtPacketPadding(Render::Models::CrtRandom& random);
    CrtPacketPadding(Render::Models::CrtRandom&& random) = delete;
    [[nodiscard]] std::byte NextPadding() override;

private:
    Render::Models::CrtRandom& m_random;
};
} // namespace Client::App
