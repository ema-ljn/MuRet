#include "Render/Models/LogicalAccess.h"

#include "Render/Models/Cloth.h"

#include <cstdint>

// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La tabla de protección". La cuenta de
// accesos es el byte que sigue a la copia cifrada de la entrada, así que suma y resta en 8 bits.
namespace Client::Render::Models
{
LogicalAccess::LogicalAccess(std::uint8_t& depth, CrtRandom& random, std::uint16_t closeRandoms)
    : m_depth(depth), m_random(random), m_closeRandoms(closeRandoms)
{
    // Abrir no gasta: lo que el research cuenta por cada abrir y cerrar es el cierre (3.848 en
    // MoveHero, 0x57d051..0x57d10d; 8 en la cámara, 0x635ae0, por las dos celdas). El descifrado del
    // primer acceso (0x40b440, 0x412810) no se porta: el valor ya está en claro.
    m_depth = static_cast<std::uint8_t>(m_depth + 1u);
}

LogicalAccess::~LogicalAccess()
{
    m_depth = static_cast<std::uint8_t>(m_depth - 1u);
    if (m_depth != 0)
    {
        return;
    }
    // El cifrado del último cierre: un rand() por byte (0x40b4d0 llama a 0x69fd70 en 0x40b52e).
    m_random.Discard(m_closeRandoms);
}
} // namespace Client::Render::Models
