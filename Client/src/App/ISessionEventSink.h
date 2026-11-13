#pragma once

#include "App/CharacterEvent.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"

#include <cstddef>
#include <span>
#include <string>

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
    // El dispatcher (0x4e2b10) copia el paquete entero en la cola de 0x77f27b4 (Scenes/ViewportPacketRing.h) antes
    // del handler del 0x0B, el 0x12, el 0x1C, el 0x24 y el 0x2A, sin mirar el flag de cifrado ni si se puede leer: la
    // sesión lo entrega acá, crudo, antes de su ApplyWorld o de su rechazo. El 0x0B todavía no se atiende (hito 11).
    virtual void CopyToPacketRing(std::span<const std::byte> packet) = 0;
};
} // namespace Client::App
