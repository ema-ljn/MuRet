#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"
#include "Network/IObfuscation.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"

#include <cstddef>
#include <span>

// For a server that sends and expects packets as main.exe does.
namespace Client::Network
{
class ENGINE_API IdentityObfuscation final : public IObfuscation
{
public:
    // The PacketReader and EncryptPacket share it when given no other. Stateless, so one is enough, and it lives until
    // the program ends.
    [[nodiscard]] static const IdentityObfuscation& Shared();

    [[nodiscard]] std::byte InboundMask(std::byte firstWireByte) const override;
    [[nodiscard]] Core::Expected<Packet, ObfuscationError> AfterCut(Packet packet) const override;
    [[nodiscard]] Core::Expected<Packet, ObfuscationError> BeforeDispatch(Packet packet) const override;
    void BeforeEncrypt(std::span<std::byte> sealed) const override;
    [[nodiscard]] Packet BeforeSend(Packet packet) const override;
};
} // namespace Client::Network
