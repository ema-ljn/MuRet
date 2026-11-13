#include "App/ServerSession.h"

#include "App/CharacterEvent.h"
#include "App/Credentials.h"
#include "App/FaithfulPacer.h"
#include "App/ISessionEventSink.h"
#include "App/ServerAddress.h"
#include "App/SessionState.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Network/CloseReason.h"
#include "Network/Connection.h"
#include "Network/Crypto/CryptoError.h"
#include "Network/LinkState.h"
#include "Network/Packet.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Character/CharacterCreate.h"
#include "Protocol/Character/CharacterDelete.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Character/CharacterName.h"
#include "Protocol/Character/CharacterSelect.h"
#include "Protocol/Character/InventoryList.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Connection/Hello.h"
#include "Protocol/Connection/Keepalive.h"
#include "Protocol/Connection/Login.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/Connection/MainCheck.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Connection/ServerList.h"
#include "Protocol/Dispatcher.h"
#include "Protocol/Events.h"
#include "Protocol/Friends.h"
#include "Protocol/Inventory.h"
#include "Protocol/Messages.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Party.h"
#include "Protocol/Pet/PetInfo.h"
#include "Protocol/World.h"
#include "Protocol/Movement.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace Client::App
{
namespace
{
// Lo que dice el aviso de un paquete que el estado no espera, o que no se puede leer.
constexpr std::string_view OutOfTurn = "llegó fuera de turno";
constexpr std::string_view Unreadable = "no se puede leer";
// Lo que dice el aviso de una llamada de la UI que el estado no espera.
constexpr std::string_view NotAwaited = "la sesión no la espera";
constexpr std::string_view NotAwaitedPlural = "la sesión no las espera";
constexpr std::string_view NotAwaitedMasculine = "la sesión no lo espera";

// Un visitor de std::visit con un operator() por tipo: si Network::Event suma un tipo, Poll no
// compila hasta que la sesión diga qué hace con él.
template <typename... Handlers> struct Overloaded : Handlers...
{
    using Handlers::operator()...;
};
template <typename... Handlers> Overloaded(Handlers...) -> Overloaded<Handlers...>;

// La versión del saludo como texto para el log: viene del servidor, y un byte que no se ve (un \0,
// un salto de línea) sale como '?', así la línea sigue siendo una.
std::string VersionText(const std::array<char, Protocol::VersionSize>& version)
{
    std::string text(version.begin(), version.end());
    std::ranges::replace_if(text, [](char value) { return value < ' ' || value > '~'; }, '?');
    return text;
}

// Lo que espera cada estado Waiting*, para el log de ReplyTimeout; vacío en los que no esperan una
// respuesta de un servidor. Los que esperan al jugador no tienen tope.
std::string_view Awaited(SessionState state)
{
    switch (state)
    {
    case SessionState::WaitingConnectHello:
    case SessionState::WaitingGameHello:
        return "su saludo";
    case SessionState::WaitingServerList:
        return "la lista de servidores";
    case SessionState::WaitingServerAddress:
        return "la dirección del GameServer";
    case SessionState::WaitingLoginResult:
        return "el resultado del login";
    case SessionState::WaitingCharacterList:
        return "la lista de personajes";
    case SessionState::WaitingCreateResult:
        return "el resultado de crear un personaje";
    case SessionState::WaitingDeleteResult:
        return "el resultado de borrar un personaje";
    case SessionState::Idle:
    case SessionState::ConnectingConnectServer:
    case SessionState::ChoosingServer:
    case SessionState::ConnectingGameServer:
    case SessionState::WaitingCredentials:
    case SessionState::LoggedIn:
    case SessionState::ChoosingCharacter:
    case SessionState::EnteringWorld:
    case SessionState::InWorld:
    case SessionState::QuitOrdered:
    case SessionState::Failed:
        break;
    }
    return {};
}

// Los errores que se contestan con el F1 03: el checksum malo, como el original, y lo que no mide
// bloques enteros, como contesta probablemente el original (03, "C3/C4").
bool DeservesTheReport(const Network::Undecryptable& undecryptable)
{
    return undecryptable.error == Network::Crypto::CryptoError::ChecksumMismatch ||
           undecryptable.error == Network::Crypto::CryptoError::NotWholeBlocks;
}

// 0x4c20a0 construye en orden sobre pool[slot]: la última entrada reemplaza la anterior.
// La vista UI es única por slot; el evento conserva aparte todas las construcciones recibidas.
void KeepLastCharacterPerSlot(Protocol::Character::CharacterList& list)
{
    std::vector<Protocol::Character::CharacterEntry> unique;
    unique.reserve(list.characters.size());
    for (auto entry = list.characters.rbegin(); entry != list.characters.rend(); ++entry)
    {
        if (std::ranges::find(unique, entry->slot, &Protocol::Character::CharacterEntry::slot) == unique.end())
        {
            unique.push_back(*entry);
        }
    }
    std::ranges::reverse(unique);
    list.characters = std::move(unique);
}

// 0x4aec13..0x4aec8d: CreateFileA para leer, GetFileSize y un ReadFile del archivo entero. Vacío si no
// se abre (el CreateFileA que falla en 0x4aec26: tampoco abre una carpeta) o no se lee entero.
std::optional<std::vector<std::byte>> ReadWholeFile(const std::filesystem::path& path)
{
    std::error_code error;
    if (path.empty() || !std::filesystem::is_regular_file(path, error))
    {
        return std::nullopt;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad())
    {
        return std::nullopt;
    }
    std::vector<std::byte> image(bytes.size());
    std::ranges::transform(bytes, image.begin(), [](char byte) { return static_cast<std::byte>(byte); });
    return image;
}
} // namespace

ServerSession::ServerSession(Network::Connection& connection, Core::IClock& clock, Core::Log& log,
                             Network::ConnectionKeys keys, SessionSettings settings, ISessionEventSink& eventSink)
    : m_connection(connection), m_clock(clock), m_log(log), m_eventSink(eventSink), m_keys(keys),
      m_settings(std::move(settings)), m_connectServer(log), m_gameServer(log)
{
    RegisterConnectServerHandlers();
    RegisterGameServerHandlers();
    // El timer global no depende de abrir una conexión ni de autenticar (04 § El flujo de conexión).
    m_keepalive.Start(m_clock.Now());
}

void ServerSession::RegisterConnectServerHandlers()
{
    using Protocol::Connection::ConnectHelloOpcode;
    using Protocol::Connection::ServerBusySubcode;
    using Protocol::Connection::ServerListOpcode;
    using Protocol::Connection::ServerListSubcode;
    using Protocol::Connection::ServerSelectSubcode;
    // Un Dispatcher recién hecho no tiene nada: registrar no puede fallar.
    [[maybe_unused]] const bool registered =
        m_connectServer.On(ConnectHelloOpcode, [this](const Network::Packet& packet) { OnConnectHello(packet); }) &&
        m_connectServer.On(ServerListOpcode, ServerListSubcode,
                           [this](const Network::Packet& packet) { OnServerList(packet); }) &&
        m_connectServer.On(ServerListOpcode, ServerSelectSubcode,
                           [this](const Network::Packet& packet) { OnServerAddress(packet); }) &&
        m_connectServer.On(ServerListOpcode, ServerBusySubcode, [this](const Network::Packet&) { OnServerBusy(); });
    assert(registered && "los handlers del ConnectServer se registran una vez");
}

void ServerSession::RegisterGameServerHandlers()
{
    using Protocol::Character::CharacterOpcode;
    using Protocol::Character::CreateSubcode;
    using Protocol::Character::DeleteSubcode;
    using Protocol::Character::InventoryListSubcode;
    using Protocol::Character::ListSubcode;
    using Protocol::Character::SelectSubcode;
    using Protocol::Connection::HelloSubcode;
    using Protocol::Connection::JoinOpcode;
    using Protocol::Connection::LoginSubcode;
    using Protocol::Connection::LogoutSubcode;
    [[maybe_unused]] const bool registered =
        m_gameServer.On(JoinOpcode, HelloSubcode, [this](const Network::Packet& packet) { OnGameHello(packet); }) &&
        m_gameServer.On(JoinOpcode, LoginSubcode, [this](const Network::Packet& packet) { OnLoginResult(packet); }) &&
        m_gameServer.On(CharacterOpcode, ListSubcode,
                        [this](const Network::Packet& packet) { OnCharacterList(packet); }) &&
        m_gameServer.On(CharacterOpcode, CreateSubcode,
                        [this](const Network::Packet& packet) { OnCreateResult(packet); }) &&
        m_gameServer.On(CharacterOpcode, DeleteSubcode,
                        [this](const Network::Packet& packet) { OnDeleteResult(packet); }) &&
        m_gameServer.On(CharacterOpcode, SelectSubcode,
                        [this](const Network::Packet& packet) { OnWorldEntryReply(packet); }) &&
        m_gameServer.On(CharacterOpcode, InventoryListSubcode,
                        [this](const Network::Packet& packet) { OnInventoryReply(packet); }) &&
        m_gameServer.On(Protocol::Movement::WalkOpcode, [this](const Network::Packet& packet) { OnWalk(packet); }) &&
        m_gameServer.On(Protocol::Movement::DamageOpcode,
                        [this](const Network::Packet& packet) { OnDamage(packet); }) &&
        m_gameServer.On(Protocol::World::TeleportOpcode,
                        [this](const Network::Packet& packet) { OnTeleport(packet); }) &&
        m_gameServer.On(Protocol::World::WorldOpcode, Protocol::World::RegenSubcode,
                        [this](const Network::Packet& packet) { OnRegen(packet); }) &&
        m_gameServer.On(Protocol::World::WeatherOpcode, [this](const Network::Packet& packet) { OnWeather(packet); }) &&
        m_gameServer.On(Protocol::Messages::ChatOpcode, [this](const Network::Packet& packet) { OnChat(packet); }) &&
        m_gameServer.On(Protocol::Messages::KeyChatOpcode,
                        [this](const Network::Packet& packet) { OnKeyChat(packet); }) &&
        m_gameServer.On(Protocol::Messages::WhisperOpcode,
                        [this](const Network::Packet& packet) { OnWhisper(packet); }) &&
        m_gameServer.On(Protocol::Messages::WhisperResultOpcode,
                        [this](const Network::Packet& packet) { OnWhisperResult(packet); }) &&
        m_gameServer.On(Protocol::Messages::NoticeOpcode,
                        [this](const Network::Packet& packet) { OnNotice(packet); }) &&
        m_gameServer.On(CharacterOpcode, Protocol::OptionData::OptionDataSubcode,
                        [this](const Network::Packet& packet) { OnOptionData(packet); }) &&
        m_gameServer.On(Protocol::Inventory::ItemMoveOpcode,
                        [this](const Network::Packet& packet) { OnItemMove(packet); }) &&
        m_gameServer.On(Protocol::Inventory::LifeOpcode,
                        [this](const Network::Packet& packet) { OnLifeRefill(packet); }) &&
        m_gameServer.On(Protocol::Inventory::ManaOpcode,
                        [this](const Network::Packet& packet) { OnManaRefill(packet); }) &&
        m_gameServer.On(Protocol::Inventory::ItemDeleteOpcode,
                        [this](const Network::Packet& packet) { OnItemDelete(packet); }) &&
        m_gameServer.On(Protocol::Inventory::DurabilityOpcode,
                        [this](const Network::Packet& packet) { OnDurability(packet); }) &&
        m_gameServer.On(CharacterOpcode, Protocol::Inventory::EquipmentChangeSubcode,
                        [this](const Network::Packet& packet) { OnEquipmentChange(packet); }) &&
        m_gameServer.On(CharacterOpcode, Protocol::Inventory::ItemPutSubcode,
                        [this](const Network::Packet& packet) { OnItemPut(packet); }) &&
        m_gameServer.On(JoinOpcode, LogoutSubcode, [this](const Network::Packet& packet) { OnLogoutResult(packet); }) &&
        m_gameServer.On(Protocol::Connection::MainCheckOpcode,
                        [this](const Network::Packet& packet) { OnMainCheck(packet); }) &&
        // CL9 hito 9: los amigos.
        m_gameServer.On(Protocol::Friends::FriendListOpcode,
                        [this](const Network::Packet& packet) { OnFriendList(packet); }) &&
        m_gameServer.On(Protocol::Friends::FriendAddOpcode,
                        [this](const Network::Packet& packet) { OnFriendAddResult(packet); }) &&
        m_gameServer.On(Protocol::Friends::FriendRequestOpcode,
                        [this](const Network::Packet& packet) { OnFriendRequest(packet); }) &&
        m_gameServer.On(Protocol::Friends::FriendDeleteOpcode,
                        [this](const Network::Packet& packet) { OnFriendDeleteResult(packet); }) &&
        m_gameServer.On(Protocol::Friends::FriendStateOpcode,
                        [this](const Network::Packet& packet) { OnFriendState(packet); }) &&
        RegisterTheEventPackets();
    assert(registered && "los handlers del GameServer se registran una vez");
}

// CL9 hito 11: los paquetes de los eventos (04 § Los eventos), por opcode o por subcódigo como los despacha 0x4e2b10.
// El F3 va por subcódigo (CharacterOpcode es el mismo 0xf3), y el AF, el BC y el BD solo con los que el original
// atiende; los demás quedan sin handler, como antes.
bool ServerSession::RegisterTheEventPackets()
{
    namespace Events = Protocol::Events;
    const auto handler = [this](const Network::Packet& packet) { OnEventPacket(packet); };
    constexpr std::array<std::uint8_t, 15> Opcodes{
        Events::EventStateOpcode,       Events::MapAttributesOpcode, Events::ItemAcquiredOpcode,
        Events::DevilSquareEntryOpcode, Events::EventOpenTimeOpcode, Events::EventCountdownOpcode,
        Events::EventResultOpcode,      Events::EventChipInfoOpcode, Events::EventChipCountOpcode,
        Events::MutoNumbersOpcode,      Events::EventDialogOpcode,   Events::BloodCastleEntryOpcode,
        Events::EventPlayStateOpcode,   Events::SerialResultOpcode,  Events::EventSoundOpcode};
    struct WithSubcode
    {
        std::uint8_t opcode;
        std::uint8_t subcode;
    };
    constexpr std::array<WithSubcode, 15> Subcodes{{
        {Events::ChaosCastleOpcode, Events::ChaosCastleEntrySubcode},
        {Events::JewelMixOpcode, Events::JewelMixSubcode},
        {Events::JewelMixOpcode, Events::JewelUnmixSubcode},
        {Events::CrywolfOpcode, Events::CrywolfStateSubcode},
        {Events::CrywolfOpcode, Events::CrywolfAltarsSubcode},
        {Events::CrywolfOpcode, Events::CrywolfContractSubcode},
        {Events::CrywolfOpcode, Events::CrywolfTimerSubcode},
        {Events::CrywolfOpcode, Events::CrywolfBossSubcode},
        {Events::CrywolfOpcode, Events::CrywolfRankSubcode},
        {Events::CrywolfOpcode, Events::CrywolfRankingSubcode},
        {Events::CrywolfOpcode, Events::CrywolfChaosBoxSubcode},
        {Events::CrywolfOpcode, Events::CrywolfEffectSubcode},
        {Events::WorldOpcode, Events::EventTimeSubcode},
        {Events::WorldOpcode, Events::SoccerScoreSubcode},
        {Events::WorldOpcode, Events::ServerCommandSubcode},
    }};
    bool registered = true;
    for (const std::uint8_t opcode : Opcodes)
        registered = m_gameServer.On(opcode, handler) && registered;
    for (const WithSubcode& entry : Subcodes)
        registered = m_gameServer.On(entry.opcode, entry.subcode, handler) && registered;
    return registered;
}

void ServerSession::Start()
{
    if (m_state == SessionState::QuitOrdered)
    {
        m_log.Warning("no se vuelve a conectar: el GameServer ordenó cerrar el juego");
        return;
    }
    ++m_starts;
    m_failure.reset();
    m_serverList.clear();
    m_chosenServer.reset();
    m_gameServerAddress.reset();
    m_loginResult.reset();
    m_characters.reset();
    m_applicationFailure.reset();
    m_createResult.reset();
    m_deleteResult.reset();
    m_deletingSlot.reset();
    m_chosenSlot.reset();
    m_log.Info("se conecta al ConnectServer");
    Connect(m_settings.connectServer, std::nullopt);
    SetState(SessionState::ConnectingConnectServer);
}

Core::Expected<void, std::string> ServerSession::Poll()
{
    if (m_applicationFailure)
    {
        return Core::Unexpected(*m_applicationFailure);
    }
    HandleEvents();
    if (m_applicationFailure)
    {
        return Core::Unexpected(*m_applicationFailure);
    }
    const Core::Duration now = m_clock.Now();
    // También consume los vencimientos sin GS: no quedan pendientes para la próxima conexión.
    const bool keepaliveDue = m_keepalive.Due(now);
    const std::string_view awaited = Awaited(m_state);
    if (!awaited.empty() && now - m_stateSince >= ReplyTimeout)
    {
        m_log.Error("el {} no contestó en {} s: se esperaba {}", ServerName(),
                    std::chrono::duration_cast<std::chrono::seconds>(ReplyTimeout).count(), awaited);
        Fail(SessionFailure::NoReply);
        return {};
    }
    if (keepaliveDue && m_connection.State() == Network::LinkState::Open && !WithConnectServer())
    {
        SendKeepalive(now);
    }
    return {};
}

// Lo que queda del lote después de un Connect o un Fail es de la conexión de antes, y lo que queda
// después de que un Send de un handler cerró la conexión, de una que ya no está: ese cierre sale en
// el Poll siguiente (Connection.h). Si la cerró el Poll mismo (el otro lado, un corte perdido), el
// Closed es lo último del lote y lo que llegó antes se despacha.
void ServerSession::HandleEvents()
{
    const std::uint32_t generation = m_generation;
    const std::vector<Network::Event> events = m_connection.Poll();
    const bool closedByThePoll = m_connection.State() == Network::LinkState::Closed;
    for (const Network::Event& event : events)
    {
        std::visit(
            Overloaded{
                [this](const Network::Opened&) { OnOpened(); },
                [this](const Network::Packet& packet) { OnPacket(packet); },
                [this](const Network::Undecryptable& undecryptable) { OnUndecryptable(undecryptable); },
                [this](const Network::Closed& closed) { OnClosed(closed.reason); },
            },
            event);
        const bool closedByAHandler = !closedByThePoll && m_connection.State() == Network::LinkState::Closed;
        if (m_generation != generation || closedByAHandler)
        {
            return;
        }
    }
}

SessionState ServerSession::State() const
{
    return m_state;
}

// 0x4c4854 escribe el 0x3d sin mirar el estado anterior. Solo lo escribe 0x4c3940: ni 0x4c55a0 (el
// F3 10) ni los cambios de mapa (06 § Los cambios de mapa).
void ServerSession::PublishWorldEntry()
{
    if (!m_applyingWorldEntry)
    {
        m_log.Warning("se ignora la publicación de la entrada al mundo: solo la hace la respuesta del F3 03");
        return;
    }
    m_log.Info("se aplicó la entrada al mundo: la sesión está en el mundo");
    SetState(SessionState::InWorld);
}

bool ServerSession::JoinedWorld() const
{
    return m_state == SessionState::InWorld;
}

std::uint16_t ServerSession::PlayerIndex() const
{
    return m_playerIndex;
}

std::uint32_t ServerSession::StartCount() const
{
    return m_starts;
}

std::span<const Protocol::Connection::ServerEntry> ServerSession::ServerList() const
{
    return m_serverList;
}

// 0x645e10 manda el F4 03 con el código del botón y cierra la lista; el log no dice cuál.
void ServerSession::ChooseServer(std::uint16_t code)
{
    if (m_state != SessionState::ChoosingServer)
    {
        m_log.Warning("se ignora la elección de un servidor: {}", NotAwaited);
        return;
    }
    m_log.Info("se elige un servidor: se pide su dirección");
    m_chosenServer = code;
    Protocol::Outgoing choice = Protocol::Connection::WriteServerSelect(code);
    m_connection.Send(std::move(choice.packet), choice.delivery);
    SetState(SessionState::WaitingServerAddress);
}

std::optional<std::uint16_t> ServerSession::ChosenServer() const
{
    return m_chosenServer;
}

// El tick es el de GetTickCount en el original (0x641bf0): el del reloj del bucle en este momento.
// Ni la cuenta ni la clave van al log, tampoco cuando no entran.
void ServerSession::SubmitCredentials(const Credentials& credentials)
{
    if (m_state != SessionState::WaitingCredentials)
    {
        m_log.Warning("se ignoran la cuenta y la clave: {}", NotAwaitedPlural);
        return;
    }
    // Un envío periódico puede haber cerrado el GS en el Poll de este mismo frame.
    // Su Closed se despacha en el siguiente: hasta entonces se conservan estado y resultado.
    if (m_connection.State() != Network::LinkState::Open)
    {
        m_log.Warning("se ignoran la cuenta y la clave: el GameServer ya no está conectado");
        return;
    }
    const Protocol::Connection::LoginRequest request{credentials.account, credentials.password,
                                                     TickCountMs(m_clock.Now())};
    auto login = Protocol::Connection::WriteLoginRequest(request);
    if (!login.HasValue())
    {
        m_log.Error("no se puede escribir el login: la cuenta o la clave pasan de {} bytes",
                    Protocol::Connection::CredentialSize);
        Fail(SessionFailure::CredentialsTooLong);
        return;
    }
    m_log.Info("se manda el login");
    m_loginResult.reset();
    Protocol::Outgoing outgoing = std::move(login).Value();
    m_connection.Send(std::move(outgoing.packet), outgoing.delivery);
    SetState(SessionState::WaitingLoginResult);
}

std::optional<SessionFailure> ServerSession::Failure() const
{
    return m_failure;
}

std::optional<ServerAddress> ServerSession::GameServer() const
{
    return m_gameServerAddress;
}

std::optional<std::uint8_t> ServerSession::LoginResult() const
{
    return m_loginResult;
}

void ServerSession::SetKeepaliveSpeeds(const KeepaliveSpeeds& speeds)
{
    m_speeds = speeds;
}

// 0x634c60 pasa a la escena 4 y manda el F3 00 en el mismo paso (0x634dce, 0x634e0e): quien llama
// es la escena de la selección, al entrar.
void ServerSession::RequestCharacterList()
{
    if (m_state != SessionState::LoggedIn)
    {
        m_log.Warning("se ignora el pedido de la lista de personajes: {}", NotAwaitedMasculine);
        return;
    }
    m_log.Info("se pide la lista de personajes");
    Protocol::Outgoing request = Protocol::Character::WriteCharacterListRequest();
    m_connection.Send(std::move(request.packet), request.delivery);
    SetState(SessionState::WaitingCharacterList);
}

const std::optional<Protocol::Character::CharacterList>& ServerSession::Characters() const
{
    return m_characters;
}

void ServerSession::ChooseCharacter(std::optional<std::uint8_t> slot)
{
    if (m_state != SessionState::ChoosingCharacter)
    {
        m_log.Warning("se ignora la elección de un personaje: {}", NotAwaited);
        return;
    }
    m_chosenSlot = slot;
    if (ChosenCharacter() == nullptr)
    {
        m_chosenSlot.reset();
    }
}

const Protocol::Character::CharacterEntry* ServerSession::ChosenCharacter() const
{
    if (!m_characters || !m_chosenSlot)
    {
        return nullptr;
    }
    const auto& characters = m_characters->characters;
    const auto chosen = std::ranges::find(characters, *m_chosenSlot, &Protocol::Character::CharacterEntry::slot);
    return chosen == characters.end() ? nullptr : &*chosen;
}

// 0x636290 manda el F3 03 en el primer paso de la escena 5 y loguea el lugar y el nombre; acá, el
// lugar: el nombre es del jugador.
void ServerSession::EnterWorld()
{
    if (m_state != SessionState::ChoosingCharacter)
    {
        m_log.Warning("se ignora la entrada al mundo: {}", NotAwaited);
        return;
    }
    const Protocol::Character::CharacterEntry* chosen = ChosenCharacter();
    if (chosen == nullptr)
    {
        m_log.Warning("no se entra al mundo: no hay un personaje elegido");
        return;
    }
    m_log.Info("se entra al mundo con el personaje del lugar {}", chosen->slot);
    Protocol::Outgoing select = Protocol::Character::WriteCharacterSelect(chosen->name);
    m_connection.Send(std::move(select.packet), select.delivery);
    SetState(SessionState::EnteringWorld);
}

// 0x63d990 manda el F3 01 después de sus chequeos del nombre, que son de la ventana: acá solo se mira
// que entre en el paquete. El nombre es del jugador y no va al log.
void ServerSession::CreateCharacter(std::string_view name, std::uint8_t characterClass)
{
    if (m_state != SessionState::ChoosingCharacter)
    {
        m_log.Warning("se ignora el pedido de crear un personaje: {}", NotAwaitedMasculine);
        return;
    }
    const std::optional<std::array<std::byte, Protocol::Character::NameSize>> bytes =
        Protocol::Character::NameFromText(name);
    if (!bytes)
    {
        m_log.Warning("no se crea el personaje: el nombre no entra en 10 bytes");
        return;
    }
    m_log.Info("se pide crear un personaje de la clase {}", characterClass);
    Protocol::Outgoing create = Protocol::Character::WriteCharacterCreate(*bytes, characterClass);
    m_connection.Send(std::move(create.packet), create.delivery);
    m_createResult.reset();
    SetState(SessionState::WaitingCreateResult);
}

std::optional<std::uint8_t> ServerSession::CreateResult() const
{
    return m_createResult;
}

// 0x643740 manda el F3 02 con el nombre del elegido y el campo de la ventana. El código es como la
// clave: no va al log.
void ServerSession::DeleteCharacter(std::span<const std::byte, Protocol::Character::DeleteCodeSize> code)
{
    if (m_state != SessionState::ChoosingCharacter)
    {
        m_log.Warning("se ignora el pedido de borrar un personaje: {}", NotAwaitedMasculine);
        return;
    }
    const Protocol::Character::CharacterEntry* chosen = ChosenCharacter();
    if (chosen == nullptr)
    {
        m_log.Warning("no se borra un personaje: no hay uno elegido");
        return;
    }
    m_log.Info("se pide borrar el personaje del lugar {}", chosen->slot);
    Protocol::Outgoing request = Protocol::Character::WriteCharacterDelete(chosen->name, code);
    m_connection.Send(std::move(request.packet), request.delivery);
    m_deletingSlot = chosen->slot;
    m_deleteResult.reset();
    SetState(SessionState::WaitingDeleteResult);
}

void ServerSession::RequestLogout(std::uint8_t kind)
{
    if (m_state != SessionState::ChoosingCharacter && m_state != SessionState::InWorld)
    {
        m_log.Warning("se ignora el pedido de salida (F1 02 con {}): no hay un GameServer que lo espere", kind);
        return;
    }
    m_log.Info("se pide la salida al GameServer (F1 02 con {})", kind);
    Protocol::Outgoing request = Protocol::Connection::WriteLogoutRequest(kind);
    m_connection.Send(std::move(request.packet), request.delivery);
}

std::optional<std::uint8_t> ServerSession::DeleteResult() const
{
    return m_deleteResult;
}

// Al CS, lo primero es el A9, como lo manda 0x415be0 apenas la escena de login conecta (Q-C16). El
// GS saluda primero: al abrirlo no sale nada.
void ServerSession::OnOpened()
{
    if (m_state == SessionState::ConnectingConnectServer)
    {
        m_log.Info("conectó con el ConnectServer");
        Protocol::Outgoing request = Protocol::Pet::WritePetInfoRequest(Protocol::Pet::LoginScenePetInfoRequest);
        m_connection.Send(std::move(request.packet), request.delivery);
        SetState(SessionState::WaitingConnectHello);
        return;
    }
    m_log.Info("conectó con el GameServer: se espera su saludo");
    SetState(SessionState::WaitingGameHello);
}

void ServerSession::OnPacket(const Network::Packet& packet)
{
    Protocol::Dispatcher& dispatcher = WithConnectServer() ? m_connectServer : m_gameServer;
    if (dispatcher.Dispatch(packet) == Protocol::DispatchResult::Malformed)
    {
        m_log.Warning("se descarta un paquete mal formado del {}", ServerName());
    }
}

// El aviso ya lo dejó la conexión. Sin claves (el CS) no hay error, y no se contesta.
void ServerSession::OnUndecryptable(const Network::Undecryptable& undecryptable)
{
    if (!DeservesTheReport(undecryptable))
    {
        return;
    }
    m_log.Info("un C3/C4 del GameServer no se descifró: se le avisa al GameServer (F1 03)");
    Protocol::Outgoing report = Protocol::Connection::WriteDecryptErrorReport();
    m_connection.Send(std::move(report.packet), report.delivery);
}

void ServerSession::OnClosed(Network::CloseReason reason)
{
    m_log.Error("se perdió la conexión con el {}: {}", ServerName(), Network::CloseReasonText(reason));
    Fail(WithConnectServer() ? SessionFailure::ConnectServerLost : SessionFailure::GameServerLost);
}

// 0x4e33e4 pide la lista con cada saludo: en 8 de 64 conexiones sale dos veces (04 § El flujo de
// conexión). Acá, una vez.
void ServerSession::OnConnectHello([[maybe_unused]] const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingConnectHello)
    {
        m_log.Warning("se ignora el saludo del ConnectServer (00): {}", OutOfTurn);
        return;
    }
    m_log.Info("el ConnectServer saludó: se pide la lista de servidores");
    Protocol::Outgoing request = Protocol::Connection::WriteServerListRequest();
    m_connection.Send(std::move(request.packet), request.delivery);
    SetState(SessionState::WaitingServerList);
}

// La lista se guarda en el orden en que llega, y la elección es de la UI (CL5.5) o del AutoLogin,
// que elige el primero (pregunta 9 del plan de la CL4). 0x4c1860 la reparte en grupos para la
// ventana: eso es de UI::GroupServers.
void ServerSession::OnServerList(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingServerList)
    {
        m_log.Warning("se ignora la lista de servidores (F4 06): {}", OutOfTurn);
        return;
    }
    const auto list = Protocol::Connection::ReadServerList(packet);
    if (!list.HasValue())
    {
        m_log.Warning("se ignora la lista de servidores (F4 06): {}", Unreadable);
        return;
    }
    const std::vector<Protocol::Connection::ServerEntry>& entries = list.Value();
    if (entries.empty())
    {
        m_log.Error("la lista de servidores está vacía: no hay a qué GameServer conectarse");
        Fail(SessionFailure::EmptyServerList);
        return;
    }
    m_log.Info("la lista trae {} servidores: se espera la elección", entries.size());
    m_serverList = entries;
    SetState(SessionState::ChoosingServer);
}

// El original loguea la dirección, cierra el CS (0x4e8380) y conecta al GS (0x4c1340). Acá el log
// no la dice: es del servidor, como el host de la config.
void ServerSession::OnServerAddress(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingServerAddress)
    {
        m_log.Warning("se ignora la dirección del GameServer (F4 03): {}", OutOfTurn);
        return;
    }
    const auto address = Protocol::Connection::ReadServerAddress(packet);
    if (!address.HasValue())
    {
        m_log.Warning("se ignora la dirección del GameServer (F4 03): {}", Unreadable);
        return;
    }
    m_log.Info("el ConnectServer dio la dirección del GameServer: se cierra y se conecta a ese");
    m_gameServerAddress = ServerAddress{address.Value().host, address.Value().port};
    Connect(*m_gameServerAddress, m_keys);
    SetState(SessionState::ConnectingGameServer);
}

// El F4 05 contesta la elección de un servidor ocupado. Como el original (0x4e2b10 llama a
// 0x4c1bf0, que manda C1 04 F4 02), la sesión vuelve a pedir la lista al mismo CS, para que el
// jugador elija otro (Q-C71). El byte que sigue al subcódigo, el servidor, no se lee: el original
// tampoco lo mira.
void ServerSession::OnServerBusy()
{
    if (m_state != SessionState::WaitingServerAddress)
    {
        m_log.Warning("se ignora el aviso de servidor ocupado (F4 05): {}", OutOfTurn);
        return;
    }
    m_log.Warning("el servidor elegido está ocupado: se pide la lista otra vez");
    m_chosenServer.reset();
    Protocol::Outgoing request = Protocol::Connection::WriteServerListRequest();
    m_connection.Send(std::move(request.packet), request.delivery);
    SetState(SessionState::WaitingServerList);
}

// 0x4c1e20 muestra "Connectting error" con otro resultado y "Version dismatch" con otra versión,
// aunque el resultado tampoco sea 1. Acá el resultado se mira primero: un saludo rechazado no
// llega a comparar la versión.
void ServerSession::OnGameHello(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingGameHello)
    {
        m_log.Warning("se ignora el saludo del GameServer (F1 00): {}", OutOfTurn);
        return;
    }
    const auto hello = Protocol::Connection::ReadServerHello(packet);
    if (!hello.HasValue())
    {
        m_log.Warning("se ignora el saludo del GameServer (F1 00): {}", Unreadable);
        return;
    }
    if (hello.Value().result != Protocol::Connection::HelloAccepted)
    {
        m_log.Error("el GameServer rechazó la conexión (resultado {})", hello.Value().result);
        Fail(SessionFailure::HelloRejected);
        return;
    }
    // 0x4c1ec0 guarda la clave con el resultado 1, antes de comparar la versión.
    m_playerIndex = hello.Value().playerIndex;
    const std::string_view version(hello.Value().version.data(), hello.Value().version.size());
    if (version != Protocol::ClientVersion)
    {
        m_log.Error("el GameServer tiene la versión {} y este cliente la {}: no se manda el login",
                    VersionText(hello.Value().version), Protocol::ClientVersion);
        Fail(SessionFailure::VersionMismatch);
        return;
    }
    // 0x4c1e20 abre la ventana de la cuenta con el GS ya conectado (DAT_057678c0 en 2).
    m_log.Info("el GameServer saludó: se espera la cuenta y la clave");
    SetState(SessionState::WaitingCredentials);
}

void ServerSession::OnLoginResult(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingLoginResult)
    {
        m_log.Warning("se ignora el resultado del login (F1 01): {}", OutOfTurn);
        return;
    }
    const auto result = Protocol::Connection::ReadLoginResult(packet);
    if (!result.HasValue())
    {
        m_log.Warning("se ignora el resultado del login (F1 01): {}", Unreadable);
        return;
    }
    m_loginResult = result.Value();
    if (result.Value() != Protocol::Connection::LoginAccepted)
    {
        // El GS queda abierto: el aviso de la escena, y otra vez la cuenta (0x643560).
        m_log.Warning("el GameServer rechazó el login (resultado {}): se espera otra vez la cuenta y la clave",
                      result.Value());
        SetState(SessionState::WaitingCredentials);
        return;
    }
    m_log.Info("el GameServer aceptó el login");
    SetState(SessionState::LoggedIn);
    // 0x4ad100 manda uno inmediato, sin reiniciar el timer 1000 (04 § El flujo de conexión).
    SendKeepalive(m_clock.Now());
}

// La lista se guarda en el orden en que llega, aunque venga vacía: el original pasa igual al estado
// 0x33 (0x4c20a0). Elegir es del jugador.
void ServerSession::OnCharacterList(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingCharacterList)
    {
        m_log.Warning("se ignora la lista de personajes (F3 00): {}", OutOfTurn);
        return;
    }
    auto list = Protocol::Character::ReadCharacterList(packet);
    if (!list.HasValue())
    {
        m_log.Warning("se ignora la lista de personajes (F3 00): {}", Unreadable);
        return;
    }
    auto received = std::move(list).Value();
    Protocol::Character::CharacterList view = received;
    KeepLastCharacterPerSlot(view);
    m_characters = std::move(view);
    m_chosenSlot.reset();
    if (!CheckApplication(m_eventSink.ApplyCharacter(CharacterListReceived{std::move(received)}, *this)))
    {
        return;
    }
    m_log.Info("la lista trae {} personajes: se espera la elección", m_characters->characters.size());
    SetState(SessionState::ChoosingCharacter);
}

// 0x4c2290: con 1, el nuevo va a su lugar con su nombre, su nivel y su clase, sin equipo (0x517270,
// Q-C103), y pisa al que estuviera ahí; con 0 o 2, un aviso que vuelve a abrir la ventana. Con otro,
// el original no hace nada y se queda esperando con el aviso 0x5d abierto (Q-C114): acá, también, y
// el tope de la espera cierra la sesión. Un lugar de más de 4 no tiene posición en el original
// (0x4c2290 deja la de la pila): no se suma.
void ServerSession::OnCreateResult(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingCreateResult)
    {
        m_log.Warning("se ignora la respuesta de crear un personaje (F3 01): {}", OutOfTurn);
        return;
    }
    auto read = Protocol::Character::ReadCharacterCreateResult(packet);
    if (!read.HasValue())
    {
        m_log.Warning("se ignora la respuesta de crear un personaje (F3 01): {}", Unreadable);
        return;
    }
    const Protocol::Character::CharacterCreateResult& created = read.Value();
    if (created.result != Protocol::Character::CharacterCreated &&
        created.result != Protocol::Character::CreateRefused &&
        created.result != Protocol::Character::CreateRefusedOther)
    {
        m_log.Warning("se ignora la respuesta de crear un personaje (F3 01): el resultado {} no lo atiende el original",
                      created.result);
        return;
    }
    m_createResult = created.result;
    if (created.result == Protocol::Character::CharacterCreated)
    {
        if (created.slot > Protocol::Character::MaxCharacterSlot || !m_characters)
        {
            m_log.Warning("el personaje creado no se suma: el lugar {} no está en la selección", created.slot);
        }
        else
        {
            Protocol::Character::CharacterEntry entry;
            entry.slot = created.slot;
            entry.name = created.name;
            entry.level = created.level;
            entry.classByte = created.classByte;
            entry.withoutEquipment = true;
            entry.guildStatus = Protocol::Character::NoGuildStatus;
            std::vector<Protocol::Character::CharacterEntry>& characters = m_characters->characters;
            const auto same = std::ranges::find(characters, entry.slot, &Protocol::Character::CharacterEntry::slot);
            if (same != characters.end())
            {
                *same = entry;
            }
            else
            {
                characters.push_back(entry);
            }
            if (!CheckApplication(m_eventSink.ApplyCharacter(CharacterCreated{entry}, *this)))
            {
                return;
            }
            m_log.Info("se creó el personaje del lugar {}", entry.slot);
        }
    }
    else
    {
        m_log.Info("el GameServer no creó el personaje: {}", created.result);
    }
    SetState(SessionState::ChoosingCharacter);
}

// 0x4e2b10 lee la respuesta en línea y solo abre su aviso; el 0x60 (con 1) borra al elegido cuando
// llega (Q-C104). Acá se saca al que se mandó, y queda sin elegido.
void ServerSession::OnDeleteResult(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingDeleteResult)
    {
        m_log.Warning("se ignora la respuesta de borrar un personaje (F3 02): {}", OutOfTurn);
        return;
    }
    auto read = Protocol::Character::ReadCharacterDeleteResult(packet);
    if (!read.HasValue())
    {
        m_log.Warning("se ignora la respuesta de borrar un personaje (F3 02): {}", Unreadable);
        return;
    }
    m_deleteResult = read.Value();
    if (*m_deleteResult == Protocol::Character::CharacterDeleted && m_characters && m_deletingSlot)
    {
        std::erase_if(m_characters->characters, [this](const Protocol::Character::CharacterEntry& entry)
                      { return entry.slot == *m_deletingSlot; });
        if (!CheckApplication(m_eventSink.ApplyCharacter(CharacterDeleted{*m_deletingSlot}, *this)))
        {
            return;
        }
        m_chosenSlot.reset();
        m_log.Info("se borró el personaje del lugar {}", *m_deletingSlot);
    }
    else
    {
        m_log.Info("el GameServer no borró el personaje: {}", *m_deleteResult);
    }
    m_deletingSlot.reset();
    SetState(SessionState::ChoosingCharacter);
}

// 04 § La entrada al mundo: filtra los 23 campos wire y entrega al aplicador síncrono.
void ServerSession::OnWorldEntryReply(const Network::Packet& packet)
{
    if (!packet.WasDecrypted())
    {
        // 0x4c396a: sin cifrar no entra al mundo y le avisa al GameServer con el F1 03 00 F3.
        m_log.Warning("la respuesta de entrada al mundo (F3 03) no llegó cifrada: se le avisa al GameServer (F1 03)");
        Protocol::Outgoing report = Protocol::Connection::WriteClearWorldEntryReport();
        m_connection.Send(std::move(report.packet), report.delivery);
        return;
    }
    auto reply = Protocol::Character::ReadCharacterSelectResult(packet);
    if (!reply.HasValue())
    {
        m_log.Warning("se ignora la respuesta de entrada al mundo (F3 03): {}", Unreadable);
        return;
    }
    // El sink publica el 0x3d desde acá (PublishWorldEntry); un error después de publicarlo
    // igual termina en ApplicationFailed.
    m_applyingWorldEntry = true;
    Core::Expected<void, std::string> applied = m_eventSink.ApplyWorld(std::move(reply).Value(), *this);
    m_applyingWorldEntry = false;
    static_cast<void>(CheckApplication(std::move(applied)));
}

// 04 § El F3 10, el inventario: conserva el wire y entrega incluso la lista vacía al aplicador.
void ServerSession::OnInventoryReply(const Network::Packet& packet)
{
    if (!packet.WasDecrypted())
    {
        // 0x4c55a0 vacía el inventario antes de mirar el flag, y sin cifrar le avisa al GameServer con el
        // mismo F1 03 00 F3 que el F3 03 (0x4c56ed..0x4c5e3c), sin leer el paquete.
        m_log.Warning("el inventario (F3 10) no llegó cifrado: se vacía y se le avisa al GameServer (F1 03)");
        if (!CheckApplication(m_eventSink.ApplyWorld(ClearInventoryReceived{}, *this)))
        {
            return;
        }
        Protocol::Outgoing report = Protocol::Connection::WriteClearWorldEntryReport();
        m_connection.Send(std::move(report.packet), report.delivery);
        return;
    }
    auto reply = Protocol::Character::ReadInventoryList(packet);
    if (!reply.HasValue())
    {
        m_log.Warning("se ignora el inventario (F3 10): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(std::move(reply).Value(), *this)));
}

// 0x4e2b10 con los opcodes de los eventos: ninguno mira la escena, el estado de la sesión ni el flag de cifrado. El 0B
// guarda antes su copia en la cola de 0x77f27b4 (0x4e3594..0x4e361f), como el 1C, aunque llegue corto.
void ServerSession::OnEventPacket(const Network::Packet& packet)
{
    if (packet.Opcode() == Protocol::Events::EventStateOpcode)
        m_teleports.Record(packet.Bytes());
    auto event = Protocol::Events::ReadEventPacket(packet);
    if (!event.HasValue())
    {
        m_log.Warning("se ignora el paquete de evento {:02X}: {}", packet.Opcode().value_or(0), Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(std::move(event).Value(), *this)));
}

// 0x4e36b5: sin guarda de escena, estado de login ni procedencia cifrada. La Connection ya normalizó C3.
void ServerSession::OnWeather(const Network::Packet& packet)
{
    auto weather = Protocol::World::ReadWeather(packet);
    if (!weather.HasValue())
    {
        m_log.Warning("se ignora el clima (0F): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(weather.Value(), *this)));
}

// 04 § El chat que llega: ninguno mira el estado de la sesión, la escena (salvo el 00 y el 0D, que la mira el sink)
// ni el flag de cifrado. Los avisos del log no llevan el nombre ni el texto.
void ServerSession::OnChat(const Network::Packet& packet)
{
    const auto chat = Protocol::Messages::ReadChat(packet);
    if (!chat.HasValue())
    {
        m_log.Warning("se ignora el chat (00): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(chat.Value(), *this)));
}

void ServerSession::OnKeyChat(const Network::Packet& packet)
{
    const auto chat = Protocol::Messages::ReadKeyChat(packet);
    if (!chat.HasValue())
    {
        m_log.Warning("se ignora el chat por clave (01): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(chat.Value(), *this)));
}

void ServerSession::OnWhisper(const Network::Packet& packet)
{
    const auto whisper = Protocol::Messages::ReadWhisper(packet);
    if (!whisper.HasValue())
    {
        m_log.Warning("se ignora el susurro (02): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(whisper.Value(), *this)));
}

void ServerSession::OnWhisperResult(const Network::Packet& packet)
{
    const auto result = Protocol::Messages::ReadWhisperResult(packet);
    if (!result.HasValue())
    {
        m_log.Warning("se ignora el resultado del susurro (0C): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(result.Value(), *this)));
}

void ServerSession::OnFriendList(const Network::Packet& packet)
{
    const auto list = Protocol::Friends::ReadFriendList(packet);
    if (!list.HasValue())
    {
        m_log.Warning("se ignora la lista de amigos (C0): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(list.Value(), *this)));
}

void ServerSession::OnFriendAddResult(const Network::Packet& packet)
{
    const auto result = Protocol::Friends::ReadFriendAddResult(packet);
    if (!result.HasValue())
    {
        m_log.Warning("se ignora el resultado de agregar un amigo (C1): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(result.Value(), *this)));
}

void ServerSession::OnFriendRequest(const Network::Packet& packet)
{
    const auto request = Protocol::Friends::ReadFriendRequest(packet);
    if (!request.HasValue())
    {
        m_log.Warning("se ignora el pedido de amistad (C2): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(request.Value(), *this)));
}

void ServerSession::OnFriendDeleteResult(const Network::Packet& packet)
{
    const auto result = Protocol::Friends::ReadFriendDeleteResult(packet);
    if (!result.HasValue())
    {
        m_log.Warning("se ignora el resultado de borrar un amigo (C3): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(result.Value(), *this)));
}

void ServerSession::OnFriendState(const Network::Packet& packet)
{
    const auto state = Protocol::Friends::ReadFriendState(packet);
    if (!state.HasValue())
    {
        m_log.Warning("se ignora el estado de un amigo (C4): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(state.Value(), *this)));
}

void ServerSession::OnNotice(const Network::Packet& packet)
{
    const auto notice = Protocol::Messages::ReadNotice(packet);
    if (!notice.HasValue())
    {
        m_log.Warning("se ignora el aviso (0D): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(notice.Value(), *this)));
}

void ServerSession::OnOptionData(const Network::Packet& packet)
{
    const auto options = Protocol::OptionData::ReadOptionData(packet);
    if (!options.HasValue())
    {
        m_log.Warning("se ignoran las opciones (F3 30): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(options.Value(), *this)));
}

// 04 § Mover y equipar ítems. El 24 sin cifrar no se lee: 0x4d1400 manda el C1 06 F1 03 00 16 en C3 y no suelta la
// traba (0x4d1432..0x4d1970). Sus dos rand() (el relleno y el serial) no salen del de la CRT, como los del F3 10.
void ServerSession::OnItemMove(const Network::Packet& packet)
{
    if (!packet.WasDecrypted())
    {
        m_log.Warning("el movimiento de ítem (24) no llegó cifrado: se le avisa al GameServer (F1 03)");
        Protocol::Outgoing report = Protocol::Inventory::WriteClearItemMoveReport();
        m_connection.Send(std::move(report.packet), report.delivery);
        return;
    }
    const auto result = Protocol::Inventory::ReadItemMoveResult(packet);
    if (!result.HasValue())
    {
        m_log.Warning("se ignora el movimiento de ítem (24): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(result.Value(), *this)));
}

// 04 § La vida y el maná. Llegan en cualquier escena del GameServer, también antes de la respuesta del F3 03: 0x4d3510
// y 0x4d3820 no miran cuál es.
void ServerSession::OnLifeRefill(const Network::Packet& packet)
{
    const auto refill = Protocol::Inventory::ReadLifeRefill(packet);
    if (!refill.HasValue())
    {
        m_log.Warning("se ignora la vida (26): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(refill.Value(), *this)));
}

void ServerSession::OnManaRefill(const Network::Packet& packet)
{
    const auto refill = Protocol::Inventory::ReadManaRefill(packet);
    if (!refill.HasValue())
    {
        m_log.Warning("se ignora el maná (27): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(refill.Value(), *this)));
}

void ServerSession::OnItemDelete(const Network::Packet& packet)
{
    const auto deleted = Protocol::Inventory::ReadItemDelete(packet);
    if (!deleted.HasValue())
    {
        m_log.Warning("se ignora el borrado de ítem (28): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(deleted.Value(), *this)));
}

void ServerSession::OnDurability(const Network::Packet& packet)
{
    const auto durability = Protocol::Inventory::ReadDurability(packet);
    if (!durability.HasValue())
    {
        m_log.Warning("se ignora la durabilidad (2A): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(durability.Value(), *this)));
}

void ServerSession::OnEquipmentChange(const Network::Packet& packet)
{
    const auto change = Protocol::Inventory::ReadEquipmentChange(packet);
    if (!change.HasValue())
    {
        m_log.Warning("se ignora el equipo de otro (F3 13): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(change.Value(), *this)));
}

void ServerSession::OnItemPut(const Network::Packet& packet)
{
    const auto put = Protocol::Inventory::ReadItemPut(packet);
    if (!put.HasValue())
    {
        m_log.Warning("se ignora el ítem puesto (F3 14): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(put.Value(), *this)));
}

Core::Expected<void, std::string> ServerSession::SendItemMove(const Protocol::Inventory::ItemMove& move)
{
    return SendWorldPacket(Protocol::Inventory::WriteItemMove(move));
}

void ServerSession::ResendServerListRequest()
{
    m_log.Info("el 00 llegó en el login: se pide la lista de servidores por el GameServer");
    static_cast<void>(SendWorldPacket(Protocol::Connection::WriteServerListRequest()));
}

void ServerSession::SendCreditsNotice()
{
    if (m_connection.State() != Network::LinkState::Open)
    {
        m_log.Warning("no se manda el F4 06 de los créditos: no hay un socket abierto");
        return;
    }
    m_log.Info("los créditos mandan el F4 06 al {}", ServerName());
    Protocol::Outgoing notice = Protocol::Connection::WriteCreditsNotice();
    m_connection.Send(std::move(notice.packet), notice.delivery);
}

Core::Expected<void, std::string> ServerSession::SendWorldPacket(Protocol::Outgoing outgoing)
{
    if (WithConnectServer() || m_connection.State() != Network::LinkState::Open)
    {
        m_log.Warning("no se manda el paquete de mundo: el GameServer no está abierto");
        return Core::Unexpected(std::string("el GameServer no está abierto"));
    }
    m_connection.Send(std::move(outgoing.packet), outgoing.delivery);
    return {};
}

Core::Expected<void, std::string> ServerSession::SendWalk(std::span<const Protocol::Movement::Cell> path,
                                                          Protocol::Movement::Cell target, float angle)
{
    auto walk = Protocol::Movement::WriteWalk(path, target, angle);
    if (!walk.HasValue())
    {
        return Core::Unexpected(std::string("el camino o el ángulo del D3 no se puede escribir"));
    }
    return SendWorldPacket(std::move(walk).Value());
}

Core::Expected<void, std::string> ServerSession::SendFacing(std::uint8_t direction)
{
    return SendWorldPacket(Protocol::Movement::WriteFacing(direction));
}

Core::Expected<void, std::string> ServerSession::SendOptionData(const Protocol::OptionData::OptionBody& body)
{
    return SendWorldPacket(Protocol::OptionData::WriteOptionData(body));
}

void ServerSession::RequestGate(std::uint8_t gate)
{
    static_cast<void>(SendWorldPacket(Protocol::World::WriteGateRequest(gate)));
}

void ServerSession::ConfirmTeleport()
{
    static_cast<void>(SendWorldPacket(Protocol::World::WriteTeleportDone()));
}

void ServerSession::RequestCrywolfState()
{
    static_cast<void>(SendWorldPacket(Protocol::World::WriteCrywolfState()));
}

void ServerSession::SendChat(Protocol::Outgoing packet)
{
    static_cast<void>(SendWorldPacket(std::move(packet)));
}

void ServerSession::RequestCastleSiegeState()
{
    static_cast<void>(SendWorldPacket(Protocol::World::WriteCastleSiegeState()));
}

void ServerSession::RequestPartyList()
{
    static_cast<void>(SendWorldPacket(Protocol::Party::WritePartyListRequest()));
}

void ServerSession::SendWindowClose()
{
    static_cast<void>(SendWorldPacket(Protocol::Connection::WriteWindowClose()));
}

Core::Expected<void, std::string> ServerSession::SendEventWindowLeave()
{
    return SendWorldPacket(Protocol::Events::WriteEventWindowLeave());
}

Core::Expected<void, std::string> ServerSession::SendChaosCastleReposition(std::uint8_t x, std::uint8_t y)
{
    return SendWorldPacket(Protocol::Events::WriteChaosCastleReposition(x, y));
}

void ServerSession::SendFriendPacket(Protocol::Outgoing packet)
{
    static_cast<void>(SendWorldPacket(std::move(packet)));
}

std::span<const std::vector<std::byte>> ServerSession::Teleports() const
{
    return m_teleports.Entries();
}

// 06 § Caminar: los ecos y el daño se aplican sin guarda de escena ni del flag de cifrado.
void ServerSession::OnWalk(const Network::Packet& packet)
{
    const auto walk = Protocol::Movement::ReadWalk(packet);
    if (!walk.HasValue())
    {
        m_log.Warning("se ignora el movimiento (D3): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(walk.Value(), *this)));
}

void ServerSession::OnDamage(const Network::Packet& packet)
{
    const auto damage = Protocol::Movement::ReadDamage(packet);
    if (!damage.HasValue())
    {
        m_log.Warning("se ignora el daño (D7): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(damage.Value(), *this)));
}

// P-C33: 0x4c6e32 prueba el flag antes de leer los campos; cero manda el F1 03.
void ServerSession::OnTeleport(const Network::Packet& packet)
{
    // 0x4e3e98..0x4e3f1d, antes del flag y del lector, incluso si se rechaza o está corto.
    m_teleports.Record(packet.Bytes());
    if (!packet.WasDecrypted())
    {
        m_log.Warning("el warp (1C) no llegó cifrado: se le avisa al GameServer (F1 03)");
        Protocol::Outgoing report = Protocol::World::WriteRejectedTeleport();
        m_connection.Send(std::move(report.packet), report.delivery);
        return;
    }
    const auto teleport = Protocol::World::ReadTeleport(packet);
    if (!teleport.HasValue())
    {
        m_log.Warning("se ignora el warp (1C): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(teleport.Value(), *this)));
}

// 0x4c4b80 recibe sólo el paquete: no mira el flag de cifrado.
void ServerSession::OnRegen(const Network::Packet& packet)
{
    const auto regen = Protocol::World::ReadRegen(packet);
    if (!regen.HasValue())
    {
        m_log.Warning("se ignora la reaparición (F3 04): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(regen.Value(), *this)));
}

// 0x4c2710 no mira el estado ni la escena para el 0: SendMessageA(WM_DESTROY) en 0x4c391b, y el
// WM_DESTROY cierra el socket con un RST y vacía la cola de paquetes (0x4e8380), y hace
// PostQuitMessage. Acá se acepta en cualquier estado con el GS, como el original, aunque el GS
// solo la manda con el login aceptado (su cuenta corre con Connected >= 2). La conexión se cierra
// sin el Closed, y lo que quede del lote (otro paquete, o el cierre del GS si llegó en la misma
// lectura) no se recorre (Q-C106). El 1 tampoco mira el estado ni la escena (ReturnToSelection).
void ServerSession::OnLogoutResult(const Network::Packet& packet)
{
    const auto result = Protocol::Connection::ReadLogoutResult(packet);
    if (!result.HasValue())
    {
        m_log.Warning("se ignora la orden de salida del GameServer (F1 02): {}", Unreadable);
        return;
    }
    if (result.Value() == Protocol::Connection::LogoutReturnsToSelection)
    {
        ReturnToSelection(packet.WasDecrypted());
        return;
    }
    if (result.Value() != Protocol::Connection::LogoutQuitsTheGame)
    {
        // 2 es la lista, que cambia de escena (Q-C79): la CL9. Con otro, el original no hace nada.
        m_log.Warning("se ignora la orden de salida del GameServer (F1 02 con {}): MuRet sigue la de "
                      "cerrar el juego, con 0, y la de volver a la selección, con 1",
                      result.Value());
        return;
    }
    m_log.Info("el GameServer ordenó cerrar el juego (F1 02 con 0): se cierra la conexión y el juego, sin aviso");
    m_connection.Close();
    ++m_generation;
    SetState(SessionState::QuitOrdered);
}

// 0x4c2f5b: sin cifrar, el F1 03 00 F1 y nada más (Q-C211). Cifrado, 0x4c3626..0x4c390f: el sink limpia el
// juego y deja la escena 4; el F3 00 sale en 0x4c3653..0x4c38e4 con el estado 0x32, que 0x4c3900 baja
// a 0 (la escena espera la lista mientras esté antes de 0x33: 06, "La vuelta a la selección"); y 0x4c2480
// manda el C1 03 31 de 0x419900 y, con la caja de 0x723c80 vacía, el de 0x4499b0 (Q-C214). El orden en el
// cable es ese; lo del mundo no gasta nada que dependa de los paquetes. La lista vieja y la elección se
// olvidan: la nueva las reemplaza (0x4c20a0), y hasta que llegue no hay qué elegir.
void ServerSession::ReturnToSelection(bool encrypted)
{
    if (!encrypted)
    {
        m_log.Warning("la orden de volver a la selección (F1 02 con 1) no llegó cifrada: se le avisa al GameServer "
                      "(F1 03) y se sigue igual");
        Protocol::Outgoing report = Protocol::Connection::WriteClearLogoutReport();
        m_connection.Send(std::move(report.packet), report.delivery);
        return;
    }
    if (!CheckApplication(m_eventSink.ApplyWorld(ReturnToSelectionReceived{}, *this)))
    {
        return;
    }
    m_log.Info("el GameServer ordenó volver a la selección (F1 02 con 1): se pide la lista de personajes");
    m_characters.reset();
    m_chosenSlot.reset();
    Protocol::Outgoing request = Protocol::Character::WriteCharacterListRequest();
    m_connection.Send(std::move(request.packet), request.delivery);
    Protocol::Outgoing firstClose = Protocol::Connection::WriteWindowClose();
    m_connection.Send(std::move(firstClose.packet), firstClose.delivery);
    Protocol::Outgoing secondClose = Protocol::Connection::WriteWindowClose();
    m_connection.Send(std::move(secondClose.packet), secondClose.delivery);
    SetState(SessionState::WaitingCharacterList);
}

// 0x4e2b10 → 0x4e16c0, sin guarda de escena, de estado ni del cifrado: la clave, la imagen del
// ejecutable que nombra la línea de comandos (0x4ad4a0) y la suma, que vuelve como C3. El GS la
// espera tres segundos (gObjSecondProc) y si no llega cierra la conexión. El original primero prueba
// abrirlo para escribir (0x4ad51b) y, si puede, arma otro paquete (la protección, fuera de alcance):
// un ejecutable en uso no se abre así en Windows, y MuRet sólo lo lee (Q-C319).
void ServerSession::OnMainCheck(const Network::Packet& packet)
{
    const auto key = Protocol::Connection::ReadMainCheckKey(packet);
    if (!key.HasValue())
    {
        m_log.Warning("se ignora la clave de la suma del ejecutable (03): {}", Unreadable);
        return;
    }
    const std::uint16_t table = Protocol::Connection::MainCheckTable(key.Value());
    std::uint32_t sum = 0;
    const std::optional<std::vector<std::byte>> image = ReadWholeFile(m_settings.mainCheckFile);
    if (image)
    {
        const auto summed = Protocol::Connection::MainCheckSum(*image, table);
        if (!summed.HasValue())
        {
            static_cast<void>(CheckApplication(Core::Unexpected(summed.Error())));
            return;
        }
        sum = summed.Value();
    }
    else
    {
        m_log.Warning("la suma del ejecutable (03) va en 0: no se abre \"{}\" (0x4aec28)",
                      m_settings.mainCheckFile.string());
    }
    Protocol::Outgoing reply = Protocol::Connection::WriteMainCheckReply(sum);
    m_connection.Send(std::move(reply.packet), reply.delivery);
    m_log.Info("se contestó la suma del ejecutable (03)");
}

// El tick es el de GetTickCount en el original (0x4bee60), y las velocidades van con su ajuste.
void ServerSession::SendKeepalive(Core::Duration now)
{
    using Protocol::Connection::KeepaliveSpeed;
    const Protocol::Connection::Keepalive keepalive{TickCountMs(now),
                                                    KeepaliveSpeed(m_speeds.attackSpeed, m_speeds.characterFlags),
                                                    KeepaliveSpeed(m_speeds.magicSpeed, m_speeds.characterFlags)};
    Protocol::Outgoing outgoing = Protocol::Connection::WriteKeepalive(keepalive);
    m_connection.Send(std::move(outgoing.packet), outgoing.delivery);
    if (m_connection.State() == Network::LinkState::Open)
    {
        // La Connection pudo encolarlo: no afirma que el GS lo haya recibido.
        m_log.Info("se encoló el keepalive para el GameServer");
    }
}

// El CS va sin claves y el GS con las suyas: cada Open vuelve los seriales a 0.
void ServerSession::Connect(const ServerAddress& address, const std::optional<Network::ConnectionKeys>& keys)
{
    m_connection.Open(address.host, address.port, keys);
    ++m_generation;
}

bool ServerSession::CheckApplication(Core::Expected<void, std::string> applied)
{
    if (applied.HasValue())
    {
        return true;
    }
    if (!m_applicationFailure)
    {
        m_applicationFailure = applied.Error();
        Fail(SessionFailure::ApplicationFailed);
    }
    return false;
}

void ServerSession::Fail(SessionFailure failure)
{
    m_connection.Close();
    ++m_generation;
    m_failure = failure;
    SetState(SessionState::Failed);
}

void ServerSession::SetState(SessionState state)
{
    m_state = state;
    m_stateSince = m_clock.Now();
}

std::string_view ServerSession::ServerName() const
{
    return WithConnectServer() ? "ConnectServer" : "GameServer";
}

bool ServerSession::WithConnectServer() const
{
    switch (m_state)
    {
    case SessionState::ConnectingConnectServer:
    case SessionState::WaitingConnectHello:
    case SessionState::WaitingServerList:
    case SessionState::ChoosingServer:
    case SessionState::WaitingServerAddress:
        return true;
    case SessionState::Idle:
    case SessionState::ConnectingGameServer:
    case SessionState::WaitingGameHello:
    case SessionState::WaitingCredentials:
    case SessionState::WaitingLoginResult:
    case SessionState::LoggedIn:
    case SessionState::WaitingCharacterList:
    case SessionState::ChoosingCharacter:
    case SessionState::WaitingCreateResult:
    case SessionState::WaitingDeleteResult:
    case SessionState::EnteringWorld:
    case SessionState::InWorld:
    case SessionState::QuitOrdered:
    case SessionState::Failed:
        break;
    }
    return false;
}
} // namespace Client::App
