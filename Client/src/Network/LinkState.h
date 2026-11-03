#pragma once

#include <cstdint>

// En qué está una Connection (Network/Connection.h). En su propio header, como CloseReason.h, para
// quien solo necesita el estado y no la conexión entera.
// Contrato: el plan de la CL4 (CL4.3), "Interfaces".
namespace Client::Network
{
enum class LinkState : std::uint8_t
{
    Closed,
    Connecting, // desde Open hasta el Poll que da el Opened
    Open,
};
} // namespace Client::Network
