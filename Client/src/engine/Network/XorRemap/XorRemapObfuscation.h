#pragma once

#include "Core/Expected.h"
#include "Network/IObfuscation.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"
#include "Network/XorRemap/XorRemapTables.h"

#include <cstddef>
#include <span>

// FoxAnti.dll's packet layer, the one the lab server expects, with its values injected (XorRemapTables): the XOR of
// what arrives and the translations between the lab's shape of some packets and main.exe's (03-red-y-cifrado.md, "La
// capa de FoxAnti", and the research repo's analysis/foxanti/minimal/src/packet.c). Only the packet layer: the DLL's
// protection is not part of the client.
// - InboundMask: a packet whose first byte without the 0xAA is C1 to C4 (6B, 68, 69 and 6E) carries it in every byte.
//   The original decides it by the first packet of each recv and applies it to the whole read (decode_inbound); here,
//   per packet (Q-C18).
// - AfterCut: the lab's C2 F4 02 list becomes main.exe's C2 F4 06 (parse_f4_02).
// - BeforeDispatch: puts a 00 at pkt[5] of a C1 F3 11 and adds 1 to the size (reset_queue).
// - BeforeEncrypt: remaps opcodes 0x10, 0x19 and 0x1E of what goes encrypted (send_forward).
// - BeforeSend: in a C1, remaps D3 and D7 and translates the server code of C1 06 F4 03 (encode_outbound). C2, C3 and
//   C4 pass unchanged.
// Where the original does not check a length (Q-C09) this is ObfuscationError::InvalidPacket. With well-formed
// packets the output matches the original byte for byte, also where it loses information (server codes from 21 on,
// Q-C17).
namespace Client::Network::XorRemap
{
class XorRemapObfuscation final : public IObfuscation
{
public:
    explicit XorRemapObfuscation(const XorRemapTables& tables);

    [[nodiscard]] std::byte InboundMask(std::byte firstWireByte) const override;
    [[nodiscard]] Core::Expected<Packet, ObfuscationError> AfterCut(Packet packet) const override;
    [[nodiscard]] Core::Expected<Packet, ObfuscationError> BeforeDispatch(Packet packet) const override;
    void BeforeEncrypt(std::span<std::byte> sealed) const override;
    [[nodiscard]] Packet BeforeSend(Packet packet) const override;

private:
    XorRemapTables m_tables;
};
} // namespace Client::Network::XorRemap
