#pragma once

#include "Session/CharacterEvent.h"
#include "Session/WorldEvent.h"
#include "Core/Expected.h"

#include <cstddef>
#include <span>
#include <string>

namespace Client::Scenes
{
struct WorldEntryResources;
}

namespace Client::App
{
class ServerSession;

// Cada handler entrega antes del siguiente evento de Poll. Los argumentos son prestados sólo
// durante la llamada: un consumidor que conserve payload debe copiarlo. El contexto de sesión es
// contemporáneo, pero el enlace puede estar Closed: Connection::Poll ya pudo observar el cierre
// remoto mientras los paquetes anteriores aún se despachan en orden.
// Apply no llama Poll, Start, ChooseServer, SubmitCredentials, ChooseCharacter, EnterWorld,
// CreateCharacter, DeleteCharacter ni SetKeepaliveSpeeds, y no cambia escena: el F1 02 con 1 deja la
// orden, que el SceneManager sigue en su próximo paso (Scenes::WorldRuntime::orderedScene). La referencia World
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
    // The dispatcher (0x4e2b10) copies the whole packet to the queue of 0x77f27b4 (Scenes/ViewportPacketRing.h) before
    // the handler of the 0x0B, the 0x12, the 0x1C, the 0x24 and the 0x2A, without looking at the encryption flag or
    // whether it can be read: the session hands it here, raw, before its row's handler or its rejection.
    virtual void CopyToPacketRing(std::span<const std::byte> packet) = 0;
    // 0x4c273f: todo F1/02 legible libera la salida pedida, incluso en claro o con motivo desconocido.
    // Los consumidores sin UI no conservan ese global.
    virtual void ClearLogoutRequest() {}
    // The world a routed row's handler takes its parts from (Session/Route.h). A sink without one drops those packets.
    [[nodiscard]] virtual const Scenes::WorldEntryResources* RoutedWorld() const
    {
        return nullptr;
    }
    // What follows each routed reception, before its result is checked; ApplyWorld does it itself.
    virtual void AfterWorldReception(ServerSession&) {}
};
} // namespace Client::App
