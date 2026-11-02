#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Misión de la CL2.0 (docs/learning/ola-cl2.0-flujos-y-bloques.md, actividad 4): cortar un flujo TCP
// en paquetes C1/C2/C3/C4, alimentado de a un byte.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "Cadena de recepción" (el corte).
//
// TCP entrega bytes, no mensajes: una lectura puede traer medio paquete o tres. El corte es una
// máquina de estados que solo mira un byte por vez, así que nunca lee uno que no llegó (el
// original sí, Q-C03 en 07-quirks-y-bugs.md: ver jefe/OldCutTest.cpp).
namespace Labs
{
// Un paquete entero, con su cabecera, tal como vino en el flujo.
using Frame = std::vector<std::byte>;

// Los cuatro tipos (03, "Cadena de recepción"): C1/C3 llevan el tamaño en un byte, pkt[1]; C2/C4
// en dos, pkt[1..2], big-endian. C3/C4 son los cifrados, pero el corte no los distingue.
inline constexpr std::byte TypeC1{0xC1};
inline constexpr std::byte TypeC2{0xC2};
inline constexpr std::byte TypeC3{0xC3};
inline constexpr std::byte TypeC4{0xC4};

// La cabecera de cada tipo, del mismo 03: el byte del tipo y el tamaño, en uno o en dos bytes.
inline constexpr std::size_t OneByteSizeHeader = 2;
inline constexpr std::size_t TwoByteSizeHeader = 3;

// Después de la cabecera viene el opcode (03, "Cadena de recepción"): es lo mínimo que lleva un
// paquete, así que un C1 mide al menos 3 bytes y un C2, 4.
inline constexpr std::size_t OpcodeSize = 1;

// Lo que frena el corte. Las dos son de la cabecera: el cuerpo no se mira.
enum class CutError : std::uint8_t
{
    InvalidType,  // el primer byte no es C1, C2, C3 ni C4
    SizeTooSmall, // el tamaño no llega a la cabecera más el opcode (incluye el 0)
};

class FrameCutter
{
public:
    // Agrega un byte. Devuelve el paquete cuando ese byte lo completa; si no, std::nullopt.
    //
    // Un error deja el corte parado: TCP no tiene separadores, así que después de una cabecera
    // mala no hay forma de saber dónde empieza el próximo paquete. Desde ahí Feed no hace nada y
    // Error() dice qué pasó; quien lo usa cierra la conexión.
    [[nodiscard]] std::optional<Frame> Feed(std::byte value);

    // El error que paró el corte, o std::nullopt si sigue andando.
    [[nodiscard]] std::optional<CutError> Error() const;

    // Cuántos bytes del paquete en curso ya llegaron (0 entre paquetes).
    [[nodiscard]] std::size_t Pending() const;

private:
    // Esperando el tipo, el tamaño (uno o dos bytes) o el resto del cuerpo; o parado por un error.
    enum class State : std::uint8_t
    {
        Type,
        Size,
        Body,
        Failed,
    };

    void OnType(std::byte value);
    void OnSize();
    std::optional<Frame> OnBody();
    void Fail(CutError reason);

    State m_state = State::Type;
    std::size_t m_headerSize = 0;   // 2 en un C1/C3, 3 en un C2/C4
    std::size_t m_declaredSize = 0; // el tamaño que dice la cabecera, ella incluida
    Frame m_frame;                  // los bytes del paquete en curso; salen por movimiento
    std::optional<CutError> m_error;
};
} // namespace Labs
