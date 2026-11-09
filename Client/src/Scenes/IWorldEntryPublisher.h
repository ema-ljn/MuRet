#pragma once

// Lo que la respuesta del F3 03 le avisa a la sesión: el estado 0x3d (0x4c4854), en su lugar del
// orden nativo. La sesión lo implementa con App::ServerSession::PublishWorldEntry y los tests lo
// registran.
namespace Client::Scenes
{
class IWorldEntryPublisher
{
public:
    virtual ~IWorldEntryPublisher() = default;
    virtual void PublishWorldEntry() = 0;
};
} // namespace Client::Scenes
