#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Models/Model.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <span>

// The model .bmd as 0x4f13d0 reads it (Client/docs/research/05-formatos-de-datos.md, "Modelos .bmd"). The .bmd tables
// (Item, Skill, Gate) are another format with the same extension: they do not start with "BMD".
namespace Client::Data::Models
{
// 0x4f13d0
inline constexpr std::size_t BmdVersionOffset = 3;
inline constexpr std::uint8_t EncryptedBmdVersion = 0x0C;
// Plain content starts after the version; encrypted content, after the size.
inline constexpr std::size_t BmdPlainOffset = 4;
inline constexpr std::size_t BmdEncryptedSizeOffset = 4;
inline constexpr std::size_t BmdEncryptedOffset = BmdEncryptedSizeOffset + sizeof(std::uint32_t);

// The research script walks the 1,962 models with these and no bytes are left over (05).
inline constexpr std::size_t BmdNameSize = 32;
inline constexpr std::size_t BmdVertexSize = 16;   // bone, padding and position
inline constexpr std::size_t BmdNormalSize = 20;   // bone, padding, normal, vertex and padding
inline constexpr std::size_t BmdTexCoordSize = 8;  // u and v
inline constexpr std::size_t BmdTriangleSize = 64; // 0x4f13d0 copies 36 of them
inline constexpr std::size_t BmdVectorSize = 3 * sizeof(float);
static_assert(BmdVertexSize == 2 * sizeof(std::int16_t) + BmdVectorSize, "el vértice de 05");
static_assert(BmdNormalSize == 4 * sizeof(std::int16_t) + BmdVectorSize, "la normal de 05");

// (05)
inline constexpr std::size_t BmdTriangleVerticesOffset = 2;
inline constexpr std::size_t BmdTriangleNormalsOffset = 10;
inline constexpr std::size_t BmdTriangleTexCoordsOffset = 18;

// Each posed bone takes one BoneKeys per action even when the action has no keys, which take no bytes, so a small
// file could ask for a lot of memory. The original has no cap; the lab model with the most is Player.bmd.
inline constexpr std::size_t MaxPosedBoneActions = std::size_t{1} << 16;

// As 0x4f13d0, on purpose: ignores "BMD" (Q-C86) and reads any version other than 0x0C in clear; 0x0C is decrypted
// with K16 from the first byte of the block at +8; nothing after the last bone is read (Model::trailingBytes).
// Unlike the original, which never checks sizes and reads past the buffer: a part that does not fit, or an encrypted
// size past the file, is DataError::FileTooShort; a negative count is DataError::InvalidData (the original would
// allocate with the mesh and key counts, and skips the model's three as 0 at 0x4f1882 but stores them); posed bones
// over MaxPosedBoneActions are DataError::InvalidData.
[[nodiscard]] ENGINE_API Core::Expected<Model, DataError> LoadBmd(std::span<const std::byte> file);
} // namespace Client::Data::Models
