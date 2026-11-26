#pragma once

#include "Core/Expected.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"

#include <cstddef>
#include <span>

// The layer between the wire and the client when the server expects another packet shape: the XOR of what arrives,
// and FoxAnti's format translations each way (Client/docs/research/03-red-y-cifrado.md, "La capa de FoxAnti").
//
// An implementation either leaves the packets alone (a server that speaks like main.exe) or translates them for a
// server that expects another shape. The composition root chooses which one the client gets; the rest of the client
// always reads and writes main.exe's shape.
//
// The original's order. Receiving: the PacketReader unmasks with InboundMask while framing, then AfterCut, decryption
// if it is a C3/C4, and BeforeDispatch. Sending: Protocol builds the packet (with XOR32 if it carries it); if
// encrypted, EncryptPacket calls BeforeEncrypt first; and everything that leaves goes through BeforeSend. AfterCut and
// BeforeDispatch keep WasDecrypted() when rebuilding a Packet: unmasking or translating does not count as decrypting.
namespace Client::Network
{
class IObfuscation
{
public:
    virtual ~IObfuscation() = default;

    // S->C: the XOR on every byte of the packet that starts with this wire byte; 0 if none.
    [[nodiscard]] virtual std::byte InboundMask(std::byte firstWireByte) const = 0;

    // S->C: just framed and unmasked, still encrypted if it is a C3/C4.
    [[nodiscard]] virtual Core::Expected<Packet, ObfuscationError> AfterCut(Packet packet) const = 0;

    // S->C: an already decrypted C1/C2, before the dispatcher.
    [[nodiscard]] virtual Core::Expected<Packet, ObfuscationError> BeforeDispatch(Packet packet) const = 0;

    // C->S: what will be encrypted in a C3/C4, in place: the serial at byte 0, the opcode at 1, the fields with their
    // XOR32, and the padding byte at the end.
    virtual void BeforeEncrypt(std::span<std::byte> sealed) const = 0;

    // C->S: a packet leaving for the wire, encrypted or not.
    [[nodiscard]] virtual Packet BeforeSend(Packet packet) const = 0;

protected:
    // Copied and moved only from a derived class: it stops assignment through two IObfuscation& from copying only the
    // base part and leaving the rest of each derived object as it was (slicing).
    IObfuscation() = default;
    IObfuscation(const IObfuscation&) = default;
    IObfuscation& operator=(const IObfuscation&) = default;
    IObfuscation(IObfuscation&&) = default;
    IObfuscation& operator=(IObfuscation&&) = default;
};
} // namespace Client::Network
