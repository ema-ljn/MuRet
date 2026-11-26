#include "Protocol/Weather/Weather.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"

#include <cstdint>
#include <optional>

namespace Client::Protocol::Weather
{
namespace
{
using Code = Network::Field<std::uint8_t, 3>;
// 0x4e36b5: the dispatcher reads it itself.
constexpr Network::PacketLayout WeatherLayout{
    {Network::PacketType::C1, WeatherOpcode, std::nullopt}, Code::End, 0x4e36b5};
} // namespace

Core::Expected<Weather, Network::ProtocolError> ReadWeather(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, WeatherLayout,
                               [](Network::LayoutReader& reader) { return Weather{reader.Read<Code>()}; });
}
} // namespace Client::Protocol::Weather
