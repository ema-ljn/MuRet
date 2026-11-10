#pragma once

namespace Client::Scenes
{
class IWorldTransitionPublisher
{
public:
    virtual ~IWorldTransitionPublisher() = default;
    virtual void ConfirmTeleport() = 0;
    virtual void RequestCrywolfState() = 0;
};
} // namespace Client::Scenes
