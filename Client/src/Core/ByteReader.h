#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>

// Lee enteros de un buffer de bytes, en orden, sin pasarse del final.
//
// Los archivos de Data\ guardan los enteros en little-endian: el byte menos significativo primero,
// como los dejaba el x86 del original. El tamaño de un C2/C4 va al revés, en big-endian
// (Client/docs/research/03-red-y-cifrado.md, "Cadena de recepción"). Acá se arman byte por byte con
// desplazamientos, y no casteando el buffer a std::uint32_t*: un cast no respeta la alineación ni
// las reglas de alias, y su resultado dependería del orden de bytes de la máquina (guía de la
// CL1.0, actividades 4 y 9). Lo usan Data, Network y Protocol.
namespace Client::Core
{
class ByteReader
{
public:
    // No copia los bytes: `buffer` tiene que seguir vivo mientras se lea.
    explicit ByteReader(std::span<const std::byte> buffer);

    // Un std::span<const T> acepta un temporal, y `ByteReader reader(LoadFile());` quedaría
    // colgando en la primera lectura. Este constructor lo rechaza: toma los rangos temporales que
    // son dueños de sus bytes (un std::vector, un std::array), y no los que solo los miran (un
    // std::span) ni las variables, que siguen usando el de arriba.
    template <typename Range>
        requires std::ranges::range<Range> && (!std::ranges::borrowed_range<Range>)
    explicit ByteReader(Range&& temporary) = delete;

    // Cada lectura avanza lo que leyó. Si no quedan bytes suficientes devuelve std::nullopt, sin
    // leer nada y sin avanzar.
    [[nodiscard]] std::optional<std::uint8_t> ReadU8();
    [[nodiscard]] std::optional<std::uint16_t> ReadU16Le();
    [[nodiscard]] std::optional<std::uint32_t> ReadU32Le();
    [[nodiscard]] std::optional<std::uint16_t> ReadU16Be();

    // Los `count` bytes que siguen, sin copiarlos: miran el mismo buffer. Es para los campos de
    // largo fijo, como un texto de 16 bytes del protocolo.
    [[nodiscard]] std::optional<std::span<const std::byte>> ReadBytes(std::size_t count);

    [[nodiscard]] std::size_t Remaining() const;

private:
    std::optional<std::uint32_t> ReadUnsigned(std::size_t size, std::endian order);

    std::span<const std::byte> m_buffer;
    std::size_t m_position = 0;
};
} // namespace Client::Core
