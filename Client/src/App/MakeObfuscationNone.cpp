#include "App/MakeObfuscation.h"

#include "Network/IObfuscation.h"
#include "Network/IdentityObfuscation.h"

#include <memory>

namespace Client::App
{
std::unique_ptr<const Network::IObfuscation> MakeObfuscation()
{
    return std::make_unique<Network::IdentityObfuscation>();
}
} // namespace Client::App
