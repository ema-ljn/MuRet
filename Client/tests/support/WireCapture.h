// El cable del laboratorio: lee el muproxy.log, que el proxy del laboratorio escribió entre el
// cliente original y los servidores.
//
// Una línea por segmento TCP: hora, puerto, sentido (C->S o S->C), len=N y los N bytes en hex. Una
// línea ACC abre una conexión en ese puerto, y las siguientes de ese puerto son de ella hasta el
// próximo ACC; sus bytes son un texto del proxy, no del cable, y no se leen. Lo que viene después
// de los bytes se ignora: el log del laboratorio tiene una marca del proxy en una línea.
//
// El cable lleva el login del jugador: quien use esto no imprime un payload, ni al fallar
// (SameBytesWithoutValues, en SameBytes.h).

#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Client::Tests
{

enum class WireDirection : std::uint8_t
{
    ClientToServer, // C->S
    ServerToClient, // S->C
};

// Lo que trajo un segmento TCP, en un sentido.
struct WireSegment
{
    WireDirection direction = WireDirection::ClientToServer;
    std::vector<std::byte> bytes;
};

// Una conexión, desde su ACC: el puerto del proxy y los segmentos en el orden en que pasaron.
struct WireConnection
{
    std::uint16_t port = 0;
    std::vector<WireSegment> segments;

    // Los segmentos de un sentido, en orden: lo que devolvió cada recv del otro lado.
    [[nodiscard]] std::vector<std::span<const std::byte>> Reads(WireDirection direction) const;
};

enum class WireCaptureProblem : std::uint8_t
{
    MalformedLine,  // faltan campos, o el sentido o el puerto no se entienden
    BadHex,         // los bytes no son hex de a dos dígitos
    LengthMismatch, // len=N no es lo que miden los bytes
    NoConnection,   // un segmento de un puerto sin ACC antes
};

// Qué línea (desde 1) y qué problema: nunca su contenido.
struct WireCaptureError
{
    std::size_t line = 0;
    WireCaptureProblem problem = WireCaptureProblem::MalformedLine;
};

// Las conexiones del log, en el orden de sus ACC.
[[nodiscard]] Core::Expected<std::vector<WireConnection>, WireCaptureError> ParseWireCapture(std::string_view text);

} // namespace Client::Tests
