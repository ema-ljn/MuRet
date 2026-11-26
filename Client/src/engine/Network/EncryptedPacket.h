#pragma once

#include "Core/Expected.h"
#include "Crypto/CryptoError.h"
#include "Crypto/SimpleModulus.h"
#include "EngineApi.h"
#include "Network/IObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"

#include <cstddef>

// Contract: Client/docs/research/03-red-y-cifrado.md, "C3/C4". Encrypting, the serial overwrites the size (pkt[1] in
// a C1, pkt[2] in a C2) and 8-byte blocks are encrypted from there: [serial][opcode][fields], a padding byte after
// the useful bytes and the rest of the last block at 0. A C1 goes out as C3 if the encrypted length is below 0x100
// bytes (cmp eax,0x100, at 0x4e22a8), else as C4. A C2 always goes out as C4: so the letters window (0x498820), the
// only C2 sender, builds it. Decrypting, a C3 is rebuilt as C1 and a C4 as C2, without the serial and with the size it
// measures: a C1 of 186 bytes or more, sent as C4, comes back as C2, as in the original. The original declares one
// less in a C1 and two less in a C2 (Q-C10).
namespace Client::Network
{
// Uses up a send serial only if the packet leaves. The new C3/C4 has WasDecrypted() false, even if the C1/C2 came from
// a decryption. `padding` is the byte after the useful bytes: the original uses rand(), the tests fix it.
// WrongPacketType with a C3/C4 or a Packet whose bytes were taken (TakeBytes), and TooLarge if the C4 would exceed
// MaxPacketSize (a C2 over 5,954 bytes). What will be encrypted (serial, useful bytes and padding) first goes through
// `obfuscation`'s BeforeEncrypt, as the original runs FoxAnti before 0x685ac0; without one,
// IdentityObfuscation::Shared(), which does not touch it.
[[nodiscard]] ENGINE_API Core::Expected<Packet, Client::Crypto::CryptoError>
EncryptPacket(const Packet& plain, const Client::Crypto::EncryptionKey& key, PacketSerials& serials,
              const IObfuscation& obfuscation, std::byte padding = {});
[[nodiscard]] ENGINE_API Core::Expected<Packet, Client::Crypto::CryptoError>
EncryptPacket(const Packet& plain, const Client::Crypto::EncryptionKey& key, PacketSerials& serials,
              std::byte padding = {});

struct DecryptedPacket
{
    Packet packet; // the rebuilt C1/C2, with the size it measures
    SerialCheck serial;
};

// WasDecrypted() is true with both InOrder and Resynced. WrongPacketType with a C1/C2 or a Packet whose bytes were
// taken, without touching the serials. With NotWholeBlocks, ChecksumMismatch, InvalidBlock or TooShort the packet is
// not dispatched and the next serial is expected (PacketSerials::SkipReceive). With several problems the first of
// that list wins: a bad checksum in any block beats an impossible useful size, because the original stops at the
// first bad checksum without checking sizes, and that is the error the F1 03 notice carries.
[[nodiscard]] ENGINE_API Core::Expected<DecryptedPacket, Client::Crypto::CryptoError>
DecryptPacket(const Packet& encrypted, const Client::Crypto::DecryptionKey& key, PacketSerials& serials);
} // namespace Client::Network
