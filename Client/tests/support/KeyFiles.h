#pragma once

#include "Core/Expected.h"
#include "Crypto/CryptoError.h"

#include "support/FileBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// Loading a real key file in a golden test: Enc1.dat and Dec2.dat from the golden folder, and Dec1.dat and Enc2.dat
// from the wire's (GoldenDir.h). No message shows a key value.
namespace Client::Tests
{
// The key at `path`, loaded with `load` (LoadEncryptionKey or LoadDecryptionKey). If the path is missing, the test
// already failed; if the file cannot be read or does not load, it fails here. In all three cases, std::nullopt.
template <typename Key>
std::optional<Key> LoadKeyFile(const std::optional<std::filesystem::path>& path,
                               Core::Expected<Key, Crypto::CryptoError> (*load)(std::span<const std::byte>))
{
    if (!path)
    {
        return std::nullopt;
    }
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    if (!file)
    {
        ADD_FAILURE() << "no se pudo leer " << *path;
        return std::nullopt;
    }
    auto loaded = load(std::as_bytes(std::span(*file)));
    if (!loaded.HasValue())
    {
        ADD_FAILURE() << "no carga " << path->filename();
        return std::nullopt;
    }
    return std::move(loaded).Value();
}
} // namespace Client::Tests
