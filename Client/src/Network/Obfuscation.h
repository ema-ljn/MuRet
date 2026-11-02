// La capa que se pone entre el cable y el cliente cuando el servidor espera otra forma de los
// paquetes: el XOR de lo que llega, y las traducciones de formato que hace FoxAnti en cada sentido.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "La capa de FoxAnti", y el plan de la CL2
// (CL2.4).
//
// Hay dos: IdentityObfuscation, que no toca nada (un servidor que habla como main.exe), y
// FoxAntiObfuscation, la del servidor del laboratorio. La elige la configuración (CL4); el resto
// del cliente lee y escribe siempre la forma de main.exe.
//
// El orden es el del original. Al recibir: el PacketReader desenmascara con InboundMask mientras
// corta, después AfterCut, el descifrado si es un C3/C4, y BeforeDispatch. Al mandar: Protocol arma
// el paquete (con el XOR32 si lo lleva); si va cifrado, EncryptPacket llama a BeforeEncrypt antes
// de cifrar; y todo lo que sale pasa por BeforeSend. AfterCut, BeforeDispatch y BeforeSend los
// llama la conexión (CL4).

#pragma once

#include "Core/Expected.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"

#include <cstddef>
#include <span>

namespace Client::Network
{

class Obfuscation
{
public:
    virtual ~Obfuscation() = default;

    // S->C: el XOR que lleva cada byte del paquete que empieza con este byte del cable; 0 si no
    // lleva.
    [[nodiscard]] virtual std::byte InboundMask(std::byte firstWireByte) const = 0;

    // S->C: un paquete recién cortado y desenmascarado, todavía cifrado si es un C3/C4.
    [[nodiscard]] virtual Core::Expected<Packet, ObfuscationError> AfterCut(Packet packet) const = 0;

    // S->C: un C1/C2 ya descifrado, antes del dispatcher.
    [[nodiscard]] virtual Core::Expected<Packet, ObfuscationError> BeforeDispatch(Packet packet) const = 0;

    // C->S: lo que se va a cifrar en un C3/C4, en el lugar: el serial en el byte 0, el opcode en el
    // 1, los campos con su XOR32, y el byte de relleno al final.
    virtual void BeforeEncrypt(std::span<std::byte> sealed) const = 0;

    // C->S: un paquete que sale al cable, cifrado o no.
    [[nodiscard]] virtual Packet BeforeSend(Packet packet) const = 0;

protected:
    // Se copia y se mueve solo desde una clase hija. Copiar un Obfuscation entero ya no se puede,
    // porque es abstracta; lo que esto evita es asignar a través de dos Obfuscation&, que copiaría
    // solo la parte de la base y dejaría el resto de cada hija como estaba (slicing). Y si un día
    // la base tiene implementaciones por defecto, copiarla tampoco va a compilar.
    Obfuscation() = default;
    Obfuscation(const Obfuscation&) = default;
    Obfuscation& operator=(const Obfuscation&) = default;
    Obfuscation(Obfuscation&&) = default;
    Obfuscation& operator=(Obfuscation&&) = default;
};

} // namespace Client::Network
