// La ofuscación que no toca nada: para un servidor que manda y espera los paquetes como main.exe.
// Contrato: el plan de la CL2 (CL2.4).

#pragma once

#include "Network/Obfuscation.h"

namespace Client::Network
{

class IdentityObfuscation final : public Obfuscation
{
public:
    // Una que comparten el PacketReader y EncryptPacket cuando no les pasan otra. No tiene estado,
    // así que una sola alcanza, y vive hasta que termina el programa.
    [[nodiscard]] static const IdentityObfuscation& Shared();

    [[nodiscard]] std::byte InboundMask(std::byte firstWireByte) const override;
    [[nodiscard]] Core::Expected<Packet, ObfuscationError> AfterCut(Packet packet) const override;
    [[nodiscard]] Core::Expected<Packet, ObfuscationError> BeforeDispatch(Packet packet) const override;
    void BeforeEncrypt(std::span<std::byte> sealed) const override;
    [[nodiscard]] Packet BeforeSend(Packet packet) const override;
};

} // namespace Client::Network
