#pragma once
// Lo que trae se usa sin nombrarlo: GoogleTest busca PrintTo por ADL, e include-cleaner no lo
// ve. Por eso cada include de este header se queda.
// IWYU pragma: always_keep

#include "Protocol/FrontEnd/CharacterList.h"
#include "Session/Dispatcher.h"
#include "Network/PacketFields.h"
#include "support/NetworkPrinters.h"

#include <ostream>

// Cómo imprime GoogleTest los enums de Client::Protocol en un mensaje: sin esto, un
// std::optional<DispatchResult> sale como "(1-byte object <01>)". Como NetworkPrinters.h. Y una
// entrada de la lista de personajes, con el largo del nombre y nunca el nombre, como las
// Credentials de AppPrinters.h: es del jugador.
namespace Client::Protocol
{
inline void PrintTo(DispatchResult result, std::ostream* out)
{
    switch (result)
    {
    case DispatchResult::Handled:
        *out << "Handled";
        return;
    case DispatchResult::Unknown:
        *out << "Unknown";
        return;
    case DispatchResult::Malformed:
        *out << "Malformed";
        return;
    }
    *out << "DispatchResult fuera del enum";
}
} // namespace Client::Protocol

namespace Client::Protocol::FrontEnd
{
inline void PrintTo(const CharacterEntry& entry, std::ostream* out)
{
    *out << "CharacterEntry{lugar " << int{entry.slot} << ", nombre de " << Network::TextField(entry.name).size()
         << " bytes}";
}
} // namespace Client::Protocol::FrontEnd
