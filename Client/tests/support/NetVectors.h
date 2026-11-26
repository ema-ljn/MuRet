#pragma once

#include "support/VectorFiles.h"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

// The CI network vectors (Client/tests/vectors/net/README.md): synthetic packets, too long to write in the test. They
// are in the repo, so they run without the lab's wire.
namespace Client::Tests
{
// CMake passes it in MU_CLIENT_NET_VECTORS_DIR.
inline std::filesystem::path NetVectorsDir()
{
    return MU_CLIENT_NET_VECTORS_DIR;
}

// `name` is like "c2-long.bin". If it is missing, the test fails and it returns nothing.
inline std::vector<std::byte> ReadNetVector(std::string_view name)
{
    return ReadVectorFile(NetVectorsDir(), name);
}
} // namespace Client::Tests
