#include "App/MakeObfuscation.h"

#include "App/LabXorRemapTables.h"
#include "Network/IObfuscation.h"
#include "Network/XorRemapObfuscation.h"

#include <memory>

namespace Client::App
{
std::unique_ptr<const Network::IObfuscation> MakeObfuscation()
{
    return std::make_unique<Network::XorRemapObfuscation>(LabXorRemapTables());
}
} // namespace Client::App
