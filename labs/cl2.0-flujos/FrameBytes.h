// Ayudas de los tests de la CL2.0 (FrameCutterTest.cpp y jefe/OldCutTest.cpp): armar paquetes
// con bytes literales y pasarle un flujo al corte de a un byte.

#pragma once

#include "FrameCutter.h"

#include <cstddef>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

namespace Labs::Tests
{

inline Frame Bytes(std::initializer_list<int> values)
{
    Frame bytes;
    for (const int value : values)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

inline Frame Join(const Frame& first, const Frame& second)
{
    Frame joined = first;
    joined.insert(joined.end(), second.begin(), second.end());
    return joined;
}

// Pasa una lectura entera al corte, de a un byte, y junta los paquetes que salen.
inline std::vector<Frame> FeedAll(FrameCutter& cutter, std::span<const std::byte> chunk)
{
    std::vector<Frame> frames;
    for (const std::byte value : chunk)
    {
        if (auto frame = cutter.Feed(value))
        {
            frames.push_back(std::move(*frame));
        }
    }
    return frames;
}

// Los dos paquetes del caso 1 de red_reference.py, que también usa el 1b.
inline const Frame FirstC1 = Bytes({0xC1, 0x05, 0x10, 0x01, 0x02});
inline const Frame SecondC2 = Bytes({0xC2, 0x00, 0x06, 0x20, 0x03, 0x04});

} // namespace Labs::Tests
