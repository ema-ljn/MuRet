#pragma once

#include "EngineApi.h"

#include <cstddef>
#include <span>

// Byte i is XORed with FC, CF or AB by i % 3; applying it twice restores the bytes, so one function encrypts and
// decrypts. Used by the .bmd tables and the terrain (Client/docs/research/05-formatos-de-datos.md, "Tablas .bmd";
// 0x5496c0 with the key at 0x6c8c74, and 0x5f8650 for the .att) and by the login's account and password
// (04-protocolo.md, "F1 01"; 0x4c1690 with the key at 0x6c82e4). The original keeps a copy per use, all with the same
// three bytes. i counts from the start of `bytes`: applying it in parts equals applying it at once only if each part
// starts at a multiple of 3, like Text.bmd's 300-byte records. The login applies it to account and password
// separately, so each field restarts at FC.
namespace Client::Crypto
{
ENGINE_API void Xor3(std::span<std::byte> bytes);
} // namespace Client::Crypto
