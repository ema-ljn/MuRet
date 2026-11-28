#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Kernel
{
enum class JointOwnerKind : std::uint8_t;
struct JointRequest;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct JointState;
} // namespace Client::Scenes

namespace Client::Actors
{
// 0x530860, where the characters' joints are born: what hangs, the full set, the auras of the flags, the raven's
// orders and the model effects of the characters. The joints' table and the global rand() are the caller's;
// game/Joints creates them.
class IJointHooks
{
public:
    virtual ~IJointHooks() = default;

    // `owner` says what is behind the request's `ownerContext` (Kernel/JointRequest.h).
    [[nodiscard]] virtual Core::Expected<std::optional<std::size_t>, std::string>
    CreateTheJoint(std::span<Scenes::JointState> joints, const Kernel::JointRequest& request, Crt::CrtRandom& random,
                   Kernel::JointOwnerKind owner) const = 0;
};
} // namespace Client::Actors
