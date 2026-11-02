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
    if (m_state == State::Failed)
    {
        return std::nullopt;
    }
    m_frame.push_back(value);
    // Sin default: si se agrega un estado y no se lo atiende acá, -Wswitch avisa.
    switch (m_state)
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
    return m_error;
}

std::size_t FrameCutter::Pending() const
{
    return m_frame.size();
}

void FrameCutter::OnType(std::byte value)
{
    if (value == TypeC1 || value == TypeC3)
    {
        m_headerSize = OneByteSizeHeader;
    }
    else if (value == TypeC2 || value == TypeC4)
    {
        m_headerSize = TwoByteSizeHeader;
    }
    else
    {
        Fail(CutError::InvalidType);
        return;
    }
    m_state = State::Size;
}

// Se llama con cada byte del tamaño. Hasta tener la cabecera entera no hay tamaño que mirar: en un
// C2 el primer byte solo no dice nada.
void FrameCutter::OnSize()
{
    if (m_frame.size() < m_headerSize)
    {
        return;
    }
    m_declaredSize = std::to_integer<std::size_t>(m_frame[SizeOffset]);
    if (m_headerSize == TwoByteSizeHeader)
    {
        m_declaredSize = (m_declaredSize << BitsPerByte) | std::to_integer<std::size_t>(m_frame[SizeOffset + 1]);
    }
    if (m_declaredSize < m_headerSize + OpcodeSize)
    {
        Fail(CutError::SizeTooSmall);
        return;
    }
    m_state = State::Body;
}

// El tamaño cuenta el paquete entero, así que el cuerpo termina cuando frame llega a declaredSize.
// El paquete sale por movimiento y el corte queda vacío para el siguiente.
std::optional<Frame> FrameCutter::OnBody()
{
    if (m_frame.size() < m_declaredSize)
    {
        return std::nullopt;
    }
    m_state = State::Type;
    return std::exchange(m_frame, Frame{});
}

void FrameCutter::Fail(CutError reason)
{
    m_state = State::Failed;
    m_error = reason;
    m_frame.clear();
}
} // namespace Labs
