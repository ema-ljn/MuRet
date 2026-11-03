#pragma once

#include "Core/Expected.h"
#include "Network/IObfuscation.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"

#include <cstddef>
#include <span>

// La capa de paquetes de FoxAnti, la que espera el servidor del laboratorio: el XOR 0xAA de lo que
// llega y las traducciones entre su forma de algunos paquetes y la de main.exe.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "La capa de FoxAnti", el plan de la CL2
// (CL2.4) y la FoxAnti mínima (analysis/foxanti/minimal/src/packet.c en el repo de research), que
// porta cada función con su dirección en FoxAnti.dll.
//
// Solo la capa de paquetes: la protección de la DLL no es parte del cliente.
//
// - InboundMask: un paquete que llega con un primer byte que, sin el 0xAA, es C1 a C4 (6B, 68, 69
//   y 6E) lo trae en cada byte. El original lo decide por el primer paquete de cada recv y lo
//   aplica a la lectura entera (decode_inbound); acá, por paquete (Q-C18).
// - AfterCut: la lista C2 F4 02 del laboratorio pasa a la C2 F4 06 de main.exe (parse_f4_02).
// - BeforeDispatch: a un C1 F3 11 le mete un 00 en pkt[5] y suma 1 al tamaño (reset_queue).
// - BeforeEncrypt: remapea los opcodes 0x10, 0x19 y 0x1E de lo que va cifrado (send_forward).
// - BeforeSend: en un C1, remapea D3 y D7, y le traduce el código del servidor a C1 06 F4 03
//   (encode_outbound). C2, C3 y C4 pasan igual.
//
// Donde el original no mira un largo (Q-C09), acá es ObfuscationError::InvalidPacket. Con
// paquetes bien formados, lo que sale es lo mismo que en el original byte por byte, también donde
// el original pierde información (los códigos de servidor desde 21, Q-C17).
namespace Client::Network
{
class FoxAntiObfuscation final : public IObfuscation
{
public:
    [[nodiscard]] std::byte InboundMask(std::byte firstWireByte) const override;
    [[nodiscard]] Core::Expected<Packet, ObfuscationError> AfterCut(Packet packet) const override;
    [[nodiscard]] Core::Expected<Packet, ObfuscationError> BeforeDispatch(Packet packet) const override;
    void BeforeEncrypt(std::span<std::byte> sealed) const override;
    [[nodiscard]] Packet BeforeSend(Packet packet) const override;
};
} // namespace Client::Network
