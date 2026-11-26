#include "Session/OpcodeTable.h"

#include "Protocol/Session/Opcodes.h"
#include "Session/ServerSession.h"

#include <array>
#include <optional>
#include <span>

namespace Client::Session
{
std::span<const OpcodeRow> ConnectServerOpcodes()
{
    namespace Codes = Protocol::Session;
    using App::ServerSession;
    static constexpr std::array<OpcodeRow, 5> Rows{
        Row(Codes::ConnectHelloOpcode, std::nullopt, 0x4e33e4, "ConnectHello", &Call<&ServerSession::OnConnectHello>),
        Row(Codes::ServerListOpcode, Codes::ServerListRequestSubcode, 0, "ServerList ServerListRequest", SentOnly{}),
        Row(Codes::ServerListOpcode, Codes::ServerSelectSubcode, 0x4e32f2, "ServerList ServerSelect",
            &Call<&ServerSession::OnServerAddress>),
        Row(Codes::ServerListOpcode, Codes::ServerBusySubcode, 0x4e32f2, "ServerList ServerBusy",
            &Call<&ServerSession::OnServerBusy>),
        Row(Codes::ServerListOpcode, Codes::ServerListSubcode, 0x4e32f2, "ServerList ServerList",
            &Call<&ServerSession::OnServerList>),
    };
    static_assert(EachCodeHasOneRow(Rows));
    return Rows;
}
} // namespace Client::Session
