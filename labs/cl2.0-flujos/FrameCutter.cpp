#include "FrameCutter.h"

#include <utility>

namespace Labs
{

namespace
{

// Dónde empieza el tamaño, después del byte del tipo, y cuánto se corre el byte alto del de un
// C2/C4, que es big-endian.
constexpr std::size_t SizeOffset = 1;
constexpr unsigned BitsPerByte = 8;

} // namespace

std::optional<Frame> FrameCutter::Feed(std::byte value)
{
    if (state == State::Failed)
    {
        return std::nullopt;
    }
    frame.push_back(value);
    // Sin default: si se agrega un estado y no se lo atiende acá, -Wswitch avisa.
    switch (state)
    {
    case State::Type:
        OnType(value);
        return std::nullopt;
    case State::Size:
        OnSize();
        return std::nullopt;
    case State::Body:
        return OnBody();
    case State::Failed:
        break;
    }
    return std::nullopt;
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
    declaredSize = std::to_integer<std::size_t>(frame[SizeOffset]);
    if (headerSize == TwoByteSizeHeader)
    {
        declaredSize = (declaredSize << BitsPerByte) | std::to_integer<std::size_t>(frame[SizeOffset + 1]);
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
