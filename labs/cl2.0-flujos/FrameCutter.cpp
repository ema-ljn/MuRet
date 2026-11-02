#include "FrameCutter.h"

#include <utility>

namespace Labs
{

namespace
{

// Los cuatro tipos (03, "Cadena de recepción"): C1/C3 llevan el tamaño en un byte, pkt[1]; C2/C4 en
// dos, pkt[1..2], big-endian. C3/C4 son los cifrados, pero el corte no los distingue.
constexpr std::byte TypeC1{0xC1};
constexpr std::byte TypeC2{0xC2};
constexpr std::byte TypeC3{0xC3};
constexpr std::byte TypeC4{0xC4};

// Cabecera de cada tipo: el byte del tipo y el tamaño.
constexpr std::size_t OneByteSizeHeader = 2;
constexpr std::size_t TwoByteSizeHeader = 3;

// Después de la cabecera viene el opcode: es lo mínimo que lleva un paquete.
constexpr std::size_t OpcodeSize = 1;

} // namespace

std::optional<Frame> FrameCutter::Feed(std::byte value)
{
    if (state == State::Failed)
    {
        return std::nullopt;
    }
    frame.push_back(value);
    if (state == State::Type)
    {
        OnType(value);
        return std::nullopt;
    }
    if (state == State::Size)
    {
        OnSize();
        return std::nullopt;
    }
    return OnBody();
}

std::optional<CutError> FrameCutter::Error() const
{
    return error;
}

std::size_t FrameCutter::Pending() const
{
    return frame.size();
}

void FrameCutter::OnType(std::byte value)
{
    if (value == TypeC1 || value == TypeC3)
    {
        headerSize = OneByteSizeHeader;
    }
    else if (value == TypeC2 || value == TypeC4)
    {
        headerSize = TwoByteSizeHeader;
    }
    else
    {
        Fail(CutError::InvalidType);
        return;
    }
    state = State::Size;
}

// Se llama con cada byte del tamaño. Hasta tener la cabecera entera no hay tamaño que mirar: en un
// C2 el primer byte solo no dice nada.
void FrameCutter::OnSize()
{
    if (frame.size() < headerSize)
    {
        return;
    }
    declaredSize = std::to_integer<std::size_t>(frame[1]);
    if (headerSize == TwoByteSizeHeader)
    {
        declaredSize = (declaredSize << 8) | std::to_integer<std::size_t>(frame[2]);
    }
    if (declaredSize < headerSize + OpcodeSize)
    {
        Fail(CutError::SizeTooSmall);
        return;
    }
    state = State::Body;
}

// El tamaño cuenta el paquete entero, así que el cuerpo termina cuando frame llega a declaredSize.
// El paquete sale por movimiento y el corte queda vacío para el siguiente.
std::optional<Frame> FrameCutter::OnBody()
{
    if (frame.size() < declaredSize)
    {
        return std::nullopt;
    }
    state = State::Type;
    return std::exchange(frame, Frame{});
}

void FrameCutter::Fail(CutError reason)
{
    state = State::Failed;
    error = reason;
    frame.clear();
}

} // namespace Labs
