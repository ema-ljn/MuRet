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
// mutable se reserva a dos cosas: mandar el C1 04 BD 00 que pide la carga del mapa 34 (en la
// respuesta del F3 03, fila 1 de la tabla de 0x4c3940 en 06, 0x4c421a), cuyo comando todavía no
// está en la sesión, y PublishWorldEntry en el store del 0x3d de esa respuesta (fila 9,
// 0x4c4854). No habilita otros mutadores ni reentrada.
class ISessionEventSink
{
public:
    virtual ~ISessionEventSink() = default;
    virtual Core::Expected<void, std::string> ApplyCharacter(const CharacterEvent& event,
                                                             const ServerSession& session) = 0;
    virtual Core::Expected<void, std::string> ApplyWorld(const WorldEvent& event, ServerSession& session) = 0;
};
} // namespace Client::App
