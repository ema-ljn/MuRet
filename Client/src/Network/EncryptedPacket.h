#pragma once

#include "Core/Expected.h"
#include "Network/Crypto/CryptoError.h"
#include "Network/Crypto/SimpleModulus.h"
#include "Network/IObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"

#include <cstddef>

// Un C1/C2 cifrado como C3/C4 con el SimpleModulus, y de vuelta.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "C3/C4", y el plan de la CL2 (CL2.3).
//
// Al cifrar, el serial pisa el tamaño (pkt[1] en un C1, pkt[2] en un C2) y se cifra desde ahí, de
// a 8 bytes: [serial][opcode][campos]. Detrás de lo útil va un byte de relleno, y el resto del
// último bloque, en 0. Un C1 sale como C3 si lo cifrado mide menos de 0x100 bytes (cmp eax,0x100,
// en 0x4e22a8), y si no, como C4. Un C2 sale siempre como C4: así lo arma la ventana de cartas
// (0x498820), el único emisor de un C2.
//
// Al descifrar, un C3 se rearma como C1 y un C4 como C2, sin el serial y con el tamaño que mide: un
// C1 de 186 bytes o más, que sale como C4, vuelve como C2, como en el original. El original declara
// uno menos en un C1 y dos menos en un C2 (Q-C10).
namespace Client::Network
{
// Cifra un C1/C2 (con el XOR32, si lo lleva) como C3/C4, y gasta un serial de envío solo si sale.
// El C3/C4 nuevo tiene WasDecrypted() false, incluso si el C1/C2 venía de un descifrado.
// `padding` es el byte que va detrás de lo útil: el original pone uno de rand(), y los tests lo
// fijan. WrongPacketType con un C3/C4 o con un Packet al que ya se le sacaron los bytes
// (TakeBytes), y TooLarge si el C4 pasaría de MaxPacketSize (un C2 de más de 5.954 bytes).
//
// Con `obfuscation`, lo que se va a cifrar (el serial, lo útil y el relleno) pasa antes por su
// BeforeEncrypt, como en el original pasa por FoxAnti antes de 0x685ac0. Sin ella, por
// IdentityObfuscation::Shared(), que no lo toca.
[[nodiscard]] Core::Expected<Packet, Crypto::CryptoError>
EncryptPacket(const Packet& plain, const Crypto::EncryptionKey& key, PacketSerials& serials,
              const IObfuscation& obfuscation, std::byte padding = {});
[[nodiscard]] Core::Expected<Packet, Crypto::CryptoError>
EncryptPacket(const Packet& plain, const Crypto::EncryptionKey& key, PacketSerials& serials, std::byte padding = {});

// Lo que sale de descifrar un C3/C4.
struct DecryptedPacket
{
    Packet packet;      // el C1/C2 rearmado, con el tamaño que mide
    SerialCheck serial; // cómo vino su serial
};

// Descifra un C3/C4 y lo rearma como C1/C2, con WasDecrypted() true tanto con InOrder como con
// Resynced. WrongPacketType con un C1/C2 o con un Packet al que ya se le sacaron los bytes, sin
// tocar los seriales. Con NotWholeBlocks, ChecksumMismatch,
// InvalidBlock o TooShort, el paquete no se despacha y se espera el serial siguiente
// (PacketSerials::SkipReceive). Si hay más de un problema gana el primero de esa lista: un checksum
// malo en cualquier bloque gana sobre un largo útil imposible, porque el original corta en el
// primer checksum malo sin mirar los largos, y es el error que lleva el aviso F1 03.
[[nodiscard]] Core::Expected<DecryptedPacket, Crypto::CryptoError>
DecryptPacket(const Packet& encrypted, const Crypto::DecryptionKey& key, PacketSerials& serials);
} // namespace Client::Network
