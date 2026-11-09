#pragma once

#include "App/CharacterEvent.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"

#include <string>

namespace Client::App
{
class ServerSession;

// Cada handler entrega antes del siguiente evento de Poll. Los argumentos son prestados sólo
// durante la llamada: un consumidor que conserve payload debe copiarlo. El contexto de sesión es
// contemporáneo, pero el enlace puede estar Closed: Connection::Poll ya pudo observar el cierre
// remoto mientras los paquetes anteriores aún se despachan en orden.
// Apply no llama Poll, Start, ChooseServer, SubmitCredentials, ChooseCharacter, EnterWorld,
// CreateCharacter, DeleteCharacter ni SetKeepaliveSpeeds, y no cambia escena. La referencia World
// mutable se reserva exclusivamente al futuro PublishWorldEntry en el punto nativo state3d;
// mientras ese comando no esté integrado, no habilita otros mutadores ni reentrada.
class ISessionEventSink
{
public:
    virtual ~ISessionEventSink() = default;
    virtual Core::Expected<void, std::string> ApplyCharacter(const CharacterEvent& event,
                                                             const ServerSession& session) = 0;
    virtual Core::Expected<void, std::string> ApplyWorld(const WorldEvent& event, ServerSession& session) = 0;
};
} // namespace Client::App
