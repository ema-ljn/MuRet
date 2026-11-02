#pragma once

#include "Core/Log.h"
#include "Network/Packet.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>

// El despacho de los paquetes que llegan: una tabla opcode → handler, con un segundo nivel por
// subcódigo, en vez del switch de 10,5 KB de 0x4e2b10. Contrato: Client/docs/research/
// 04-protocolo.md, "El dispatcher 0x4e2b10", y la sub-ola CL4.2 del plan de la CL4.
//
// El original tiene tres niveles: el opcode, pkt[3] para F1 (y para F3 y F4, pkt[3] en un C1 y
// pkt[4] en un C2), y pkt[4] debajo. Acá son dos: el subcódigo es el byte que sigue al opcode, en
// cualquier tipo, y el tercer nivel lo lee el handler (como el resultado del login,
// ReadLoginResult). Lo que no tiene handler da Unknown, y la primera vez de cada opcode, o de cada
// opcode y subcódigo, una línea de aviso al log, sin los bytes del paquete; el original lo tira al
// default 0x4E5B4A sin avisar. Malformed no va al log: lo anota quien llama (la ServerSession de
// la CL4.3).
//
// Un F1 que llegara como C2 (hoy no pasa): el original lee el subcódigo en pkt[3], que en un C2 es
// el mismo opcode, y acá sale de pkt[4].
//
// Un Dispatcher por servidor (CL4.3): el opcode 00 es el saludo en el ConnectServer y el chat en
// el GameServer, y cada uno registra lo suyo. Protocol no sabe de la ofuscación, así que el cuarto
// argumento de 0x4e2b10 (el flag de FoxAnti) no está.
namespace Client::Protocol
{
// Lo que recibe un handler: el C1/C2 entero, con su cabecera, ya descifrado y traducido.
using Handler = std::function<void(const Network::Packet& packet)>;

enum class DispatchResult : std::uint8_t
{
    Handled,
    Unknown,   // sin handler: el aviso va al log la primera vez
    Malformed, // un C3/C4, o un opcode con subcódigos en un paquete que termina en el opcode; sin
               // aviso al log, lo anota quien llama
};

class Dispatcher
{
public:
    // `log` tiene que vivir más que el Dispatcher.
    explicit Dispatcher(Core::Log& log);
    // Un handler para todo el opcode. false si ya había uno (para el opcode o sus subcódigos): no
    // se pisa, porque registrar dos veces es un error de quien arma la tabla. También false, sin
    // registrar nada, si `handler` está vacío.
    [[nodiscard]] bool On(std::uint8_t opcode, Handler handler);
    // Un handler para el opcode y el byte que lo sigue. false si ya había uno para los dos, o uno
    // para todo el opcode, o si `handler` está vacío.
    [[nodiscard]] bool On(std::uint8_t opcode, std::uint8_t subcode, Handler handler);
    // Llama al handler del paquete, si tiene.
    DispatchResult Dispatch(const Network::Packet& packet);

private:
    // Uno por cada valor de un byte: los opcodes y los subcódigos posibles.
    static constexpr std::size_t ByteValues = 256;

    // Lo registrado para un opcode: un handler para todo el opcode, o uno por subcódigo, nunca los
    // dos. Uno que está en m_opcodes tiene uno de los dos.
    struct OpcodeHandlers
    {
        Handler whole;
        std::map<std::uint8_t, Handler> bySubcode;
        // Los subcódigos sin handler que ya se avisaron.
        std::bitset<ByteValues> warnedSubcodes;
    };

    DispatchResult DispatchBySubcode(std::uint8_t opcode, OpcodeHandlers& handlers, const Network::Packet& packet);

    Core::Log& m_log;
    std::map<std::uint8_t, OpcodeHandlers> m_opcodes;
    // Los opcodes sin handler que ya se avisaron.
    std::bitset<ByteValues> m_warnedOpcodes;
};
} // namespace Client::Protocol
