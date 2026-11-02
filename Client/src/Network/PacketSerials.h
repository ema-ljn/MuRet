// Los dos seriales de los C3/C4 de una conexión: el de envío y el de recepción.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "C3/C4", y el plan de la CL2 (CL2.3).
//
// Cada C3/C4 lleva un byte de serial como primer byte de lo cifrado. El que sale sube de a uno
// desde 0, y del otro lado se compara con el que se espera: un paquete perdido o repetido no
// coincide. En el original son dos bytes globales (0x05767913 y 0x05767914), y 0x4c1340 los vuelve
// a 0 al conectar al GameServer; acá son de cada conexión.

#pragma once

#include <cstdint>

namespace Client::Network
{

// Qué pasó con el serial de un paquete que llegó.
enum class SerialCheck : std::uint8_t
{
    InOrder,  // era el que se esperaba
    Resynced, // era otro, y desde ahí se espera el que le sigue (Q-C04)
};

class PacketSerials
{
public:
    // El serial del paquete que sale, y suma 1: de 255 vuelve a 0.
    [[nodiscard]] std::uint8_t TakeSend();

    // Compara el serial de un paquete que llegó con el que se esperaba, y desde ahí espera el que
    // le sigue. El original, si no coincide, se queda con el recibido sin sumarle 1, así que el
    // próximo tampoco coincide y el desfase queda (Q-C04): acá se resincroniza, y Resynced le dice
    // a quien lo usa que lo anote una vez.
    [[nodiscard]] SerialCheck Receive(std::uint8_t received);

    // Un paquete que llegó y no se pudo descifrar: no se sabe su serial, y se espera el siguiente,
    // como hace el original con un checksum malo.
    void SkipReceive();

    // Los dos a 0, como 0x4c1340 al conectar al GameServer.
    void Reset();

private:
    std::uint8_t send = 0;    // el del próximo paquete que sale
    std::uint8_t receive = 0; // el que se espera en el próximo que llega
};

} // namespace Client::Network
