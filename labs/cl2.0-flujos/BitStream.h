#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Misión de la CL2.0 (docs/learning/ola-cl2.0-flujos-y-bloques.md, actividad 10): un flujo de bits
// MSB primero, como el que arma el bloque C3/C4.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "C3/C4: cifrado por bloques con Enc1.dat y
// Dec2.dat".
//
// MSB primero: el primer bit del flujo es el 0x80 del byte 0, el segundo el 0x40, y un campo puede
// empezar en la mitad de un byte y terminar en el siguiente. Los dos tipos van en un mismo archivo
// porque uno es el espejo del otro y se prueban juntos.
namespace Labs
{
class BitWriter
{
public:
    // Agrega los bitCount bits de abajo de value, el más alto primero. Los bits de value por encima
    // de bitCount no se escriben; si bitCount pasa de 32, los de más arriba salen en 0, como si
    // value tuviera ceros a la izquierda. Con bitCount 0 no escribe nada.
    void Write(std::uint32_t value, std::size_t bitCount);

    // Los bytes escritos. El último puede estar a medio llenar: sus bits de abajo quedan en 0.
    [[nodiscard]] std::span<const std::byte> Bytes() const;

    // Cuántos bits se escribieron.
    [[nodiscard]] std::size_t BitCount() const;

private:
    void WriteBit(bool bit);

    std::vector<std::byte> m_bytes;
    std::size_t m_writtenBits = 0;
};

class BitReader
{
public:
    // Lee de bytes, que tienen que seguir vivos mientras se lee.
    explicit BitReader(std::span<const std::byte> m_bytes);

    // Lee bitCount bits en el mismo orden en que los escribe BitWriter, el primero como el más alto
    // del resultado. Si no quedan tantos, o si bitCount pasa de 32 (no entran en el resultado),
    // devuelve std::nullopt y no avanza. Read(0) devuelve 0.
    [[nodiscard]] std::optional<std::uint32_t> Read(std::size_t bitCount);

    // Cuántos bits quedan por leer.
    [[nodiscard]] std::size_t RemainingBits() const;

private:
    [[nodiscard]] bool BitAt(std::size_t index) const;

    std::span<const std::byte> m_buffer;
    std::size_t m_position = 0; // en bits, desde el 0x80 del byte 0
};
} // namespace Labs
