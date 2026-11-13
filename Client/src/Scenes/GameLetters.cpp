#include "Scenes/GameLetters.h"

#include "App/IChatRoomLinks.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/FriendChat.h"
#include "Protocol/Letters.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GameFriends.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameUi.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/FriendModel.h"
#include "UI/GameText.h"
#include "UI/LetterModel.h"
#include "UI/LetterWindows.h"
#include "UI/MessageArea.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowSteps.h"
#include "UI/OldWindows.h"
#include "UI/SprintfFormat.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
// Text.bmd.
constexpr std::size_t LetterSentText = 1046;
constexpr std::size_t SendFailedText = 1053;
constexpr std::size_t ReadTitleText = 1054;
constexpr std::size_t DeleteFailedText = 1055;
constexpr std::size_t InviteFailedText = 1056;
constexpr std::size_t InvitedText = 1057;
constexpr std::size_t JoinFailedText = 1058;
constexpr std::size_t CameInText = 1059;
constexpr std::size_t WentOutText = 1060;
constexpr std::size_t NoSuchReceiverText = 1061;
constexpr std::size_t NewLetterText = 1062;
constexpr std::size_t ChatWaitingText = 1063;
constexpr std::size_t ReceiverFullText = 1064;
constexpr std::size_t NoMoneyText = 1065;
constexpr std::size_t OtherLevelText = 1068;
constexpr std::size_t NotThereText = 1069;
constexpr std::size_t OfflineText = 1070;
constexpr std::size_t LettersFullText = 1073;
constexpr std::size_t InviteFullText = 1074;
constexpr std::size_t SeventhSendText = 423;
// 0x49e480 con FD_CLOSE: se cortó la conexión de la sala.
constexpr std::size_t RoomClosedText = 402;
// 0x4dc840 con el 1 y 0x49d0b0: el costo de una carta.
constexpr std::int32_t LetterCost = 1000;
// 0x41ed10(0x29, 0, 0): llegó una carta. Queda en lastSoundId (CL10).
constexpr std::int32_t NewLetterSound = 0x29;
// 0x4dcb70: la ventana de leer, en (100, 100) sin el lugar de la anterior.
constexpr std::int32_t ReadPlace = 100;
// 0x4dce10: el chat, en (100, 100).
constexpr std::int32_t ChatPlace = 100;
// 0x4dcd16..0x4dcd69: la clase, la acción, el ángulo y el zoom del retrato.
constexpr std::int32_t ActionBase = 0x78;
constexpr std::int32_t AngleUnit = 6;
constexpr float ZoomUnit = 0.01f;
// strncmp(nombre, "webzen", 10) (0x6c8610).
constexpr std::string_view WebzenName = "webzen";
// 0x49e280: 0x555190(0x6e57a4, "max chat count over", 1).
constexpr std::string_view MaxChatCountOver = "max chat count over";
// 0x4dd3d0: la ventana del chat minimizada (5) u oculta (3).
constexpr std::int32_t MinimizedState = 5;
constexpr std::int32_t HiddenState = 3;
// 0x4dd1e0 y 0x4dd3d0: el renglón del sistema (1) y el de un miembro (3), y el que habla sin número (0xff).
constexpr std::int32_t SystemLine = 1;
constexpr std::int32_t MemberLine = 3;
constexpr std::uint8_t NoSpeaker = 0xff;
// El lugar de la marca del modelo: 0x4a0270 deja la de 0x4dcb70 en 1.
constexpr std::int32_t ReadType = 1;

std::string CString(std::string_view text)
{
    return std::string(text.substr(0, text.find('\0')));
}

Core::Expected<std::string, std::string> TextOf(const ChatReceiveContext& context, std::size_t index,
                                                std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::string(where) + ": el texto " + std::to_string(index) + " de Text.bmd no está");
    return CString(*text);
}

// 0x555190(vacío, texto, 1).
Core::Expected<void, std::string> SystemMessage(const ChatReceiveContext& context, std::string_view text,
                                                std::string_view where)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::string(where) + ": no hay con qué medir el mensaje");
    return AddGameMessage(context.world.Runtime(), *context.measure, context.screen, "", text, UI::SystemMessage);
}

// Lo que la fábrica y los pasos de las ventanas viejas leen del juego desde un paquete: los textos, la medida, los
// globales y la vista sin el mouse ni las teclas (el cable no está: lo que manda, corta).
class OldWindowsFromAPacket
{
public:
    explicit OldWindowsFromAPacket(const ChatReceiveContext& context)
        : m_texts(OldWindowTextsOf(context.texts)), m_globals(OldWindowGlobalsOf(context.world.Runtime()))
    {
        WorldRuntime& runtime = context.world.Runtime();
        if (context.measure != nullptr)
            m_measure.emplace(UI::OldWindowMeasure{
                .measure = *context.measure, .dcFont = runtime.hud.dcFont, .screen = context.screen});
        m_view.texts = &m_texts;
        m_view.measure = m_measure ? &*m_measure : nullptr;
        m_view.heroName = std::string(HeroNameOf(context.world.Actors()[runtime.hero]));
    }
    OldWindowsFromAPacket(const OldWindowsFromAPacket&) = delete;
    OldWindowsFromAPacket& operator=(const OldWindowsFromAPacket&) = delete;
    OldWindowsFromAPacket(OldWindowsFromAPacket&&) = delete;
    OldWindowsFromAPacket& operator=(OldWindowsFromAPacket&&) = delete;
    ~OldWindowsFromAPacket() = default;

    [[nodiscard]] const UI::OldWindowTexts& Texts() const
    {
        return m_texts;
    }
    [[nodiscard]] const UI::OldWindowGlobals& Globals() const
    {
        return m_globals;
    }
    [[nodiscard]] const UI::OldWindowStepView& View() const
    {
        return m_view;
    }

    // 0x48ee60(caso, x, y, texto, 0, banderas).
    [[nodiscard]] Core::Expected<std::int32_t, std::string> Create(UI::OldWindowManager& manager, std::int32_t kind,
                                                                   std::int32_t x, std::int32_t y, std::string text,
                                                                   std::int32_t flags, std::string_view where) const
    {
        if (!m_measure)
            return Core::Unexpected(std::string(where) + ": la ventana sin con qué medir su texto");
        return UI::CreateTheOldWindow(
            manager,
            UI::OldWindowRequest{.kind = kind, .x = x, .y = y, .data = std::move(text), .aux = 0, .flags = flags},
            m_texts, *m_measure, &m_globals, &m_view);
    }

private:
    UI::OldWindowTexts m_texts;
    std::optional<UI::OldWindowMeasure> m_measure;
    UI::OldWindowGlobals m_globals;
    UI::OldWindowStepView m_view{};
};

// 0x48ee60(10, −1, −1, texto, 0, 0).
Core::Expected<void, std::string> Notice(const ChatReceiveContext& context, std::size_t index, std::string_view where)
{
    auto text = TextOf(context, index, where);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    const OldWindowsFromAPacket windows(context);
    auto created =
        windows.Create(context.world.Runtime().oldWindows, UI::NoticeCase, -1, -1, std::move(text).Value(), 0, where);
    if (!created.HasValue())
        return Core::Unexpected(std::move(created).Error());
    return {};
}

// 0x4a0960 sobre la pestaña de las cartas, con la de amigos.
void RefreshTheTab(UI::OldWindowManager& manager)
{
    UI::RefreshTheLettersTab(manager);
}

// 0x493530(ip, sala, ticket) sobre el chat: +0x770 = sala y 0x49de50: un lugar (0x49e280; sin ninguno, "max chat
// count over" y nada más), la conexión al puerto 55980 con el mensaje 0x500 + lugar y la sala anotada con la ventana; y
// por ella, el C1 10 00 con la sala y el ticket. El connect del original espera (0x4e8570): si falla, la sala no se
// anota, no sale nada y el lugar queda tomado (Q-C1005). En MuRet conecta sin esperar: la sala se anota al empezar,
// el C1 10 00 espera en la cola y, si el connect falla, ApplyChatRoomClosed la olvida.
Core::Expected<void, std::string> JoinTheRoom(const ChatReceiveContext& context, UI::OldWindow& chat,
                                              const Protocol::FriendChat::RoomAnswer& answer)
{
    UI::OldWindowManager& manager = context.world.Runtime().oldWindows;
    chat.chat.room = answer.room;
    if (!UI::TakeAChatSlot(manager.chatRooms, answer.room))
        return SystemMessage(context, MaxChatCountOver, "0x49e280");
    if (context.rooms == nullptr)
        return Core::Unexpected(std::string("0x49de50: la conexión de la sala del chat con un amigo, sin la sesión"));
    context.rooms->OpenChatRoom(answer.room, chat.base.id, answer.address);
    manager.chatRooms.rooms.try_emplace(answer.room, UI::ChatRoom{.room = answer.room, .window = chat.base.id});
    context.rooms->SendToChatRoom(answer.room, Protocol::FriendChat::WriteJoin(answer.room, answer.ticket));
    return {};
}

// El chat de la ventana `id`, si está y es un chat (0x490a00).
UI::OldWindow* ChatOf(UI::OldWindowManager& manager, std::int32_t id)
{
    UI::OldWindow* window = UI::FindTheOldWindow(manager, id);
    if (window == nullptr || window->kind != UI::OldWindowKind::ChatWindow)
        return nullptr;
    return window;
}
} // namespace

Core::Expected<void, std::string> ApplyLetterSendResult(const Protocol::Letters::LetterSendResult& result,
                                                        const ChatReceiveContext& context)
{
    UI::OldWindowManager& manager = context.world.Runtime().oldWindows;
    const auto window = static_cast<std::int32_t>(result.window);
    std::size_t text = 0;
    switch (result.result)
    {
    case 0:
        text = SendFailedText;
        break;
    case 1:
    {
        // 0x4dc87a: el 4 a la ventana y sprintf(1046, 1000) al chat.
        if (window != 0)
            UI::PostToTheManager(manager, UI::CloseMessage, window, 0);
        auto sent = TextOf(context, LetterSentText, "0x4dc840(1)");
        if (!sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        const std::array<UI::SprintfArgument, 1> cost{UI::SprintfArgument(LetterCost)};
        return SystemMessage(context, UI::FormatLikeSprintf(sent.Value(), cost), "0x4dc840(1)");
    }
    case 2:
        text = NoSuchReceiverText;
        break;
    case 3:
        text = ReceiverFullText;
        break;
    case 4:
        text = NoMoneyText;
        break;
    case 6:
        text = OtherLevelText;
        break;
    case 7:
        text = SeventhSendText;
        break;
    default:
        return {};
    }
    // +0x778 de la ventana de 0x490a00, sin mirar que esté (Q-C1001): MuRet no escribe en una que no está.
    if (window != 0)
    {
        if (UI::OldWindow* write = UI::FindTheOldWindow(manager, window); write != nullptr)
            write->letter.sending = 0;
    }
    return Notice(context, text, "0x4dc840");
}

Core::Expected<void, std::string> ApplyLetterRow(const Protocol::Letters::LetterRow& row,
                                                 const ChatReceiveContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::OldWindowManager& manager = runtime.oldWindows;
    if (row.type < 2)
    {
        UI::AddALetter(manager.letterModel, row.index, row.name, row.subject, row.date, row.time, row.type);
        UI::SortTheLetters(manager.letterModel, UI::ByDateLetters);
    }
    else if (row.type == 2)
    {
        // (CL10) El sonido queda pedido.
        runtime.lastSoundId = NewLetterSound;
        manager.windowList.list.newLetter = 1;
        auto text = TextOf(context, NewLetterText, "0x4dcaa4");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (auto added = SystemMessage(context, text.Value(), "0x4dcaa4"); !added.HasValue())
            return added;
        UI::AddALetter(manager.letterModel, row.index, row.name, row.subject, row.date, row.time, 0);
        UI::SortTheLetters(manager.letterModel, UI::SameLetters);
    }
    RefreshTheTab(manager);
    if (static_cast<std::size_t>(manager.letterLimit) <= manager.letterModel.letters.size())
    {
        auto full = TextOf(context, LettersFullText, "0x4dcb35");
        if (!full.HasValue())
            return Core::Unexpected(std::move(full).Error());
        return SystemMessage(context, full.Value(), "0x4dcb35");
    }
    return {};
}

Core::Expected<void, std::string> ApplyLetterRead(const Protocol::Letters::LetterRead& letter,
                                                  const ChatReceiveContext& context)
{
    UI::OldWindowManager& manager = context.world.Runtime().oldWindows;
    UI::KeepTheLetter(manager.letterModel, letter.index, letter.copy);
    UI::LetterEntry* entry = UI::FindALetter(manager.letterModel, letter.index);
    if (entry == nullptr)
        return {};
    entry->type = ReadType;
    RefreshTheTab(manager);
    auto format = TextOf(context, ReadTitleText, "0x4dcb70");
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    const std::array<UI::SprintfArgument, 1> subject{UI::SprintfArgument(std::string_view(entry->subject))};
    std::string title = UI::FormatLikeSprintf(format.Value(), subject);
    const UI::LetterEntry shown = *entry;
    const OldWindowsFromAPacket windows(context);
    Core::Expected<std::int32_t, std::string> created = 0;
    if (manager.nextReadX == -1)
        created = windows.Create(manager, UI::LetterReadCase, ReadPlace, ReadPlace, std::move(title), 0, "0x4dcb70");
    else
    {
        created = windows.Create(manager, UI::LetterReadCase, manager.nextReadX, manager.nextReadY, std::move(title),
                                 UI::KeepPlaceFlag, "0x4dcb70");
        manager.nextReadX = -1;
    }
    if (!created.HasValue())
        return Core::Unexpected(std::move(created).Error());
    const std::int32_t id = created.Value();
    UI::OldWindow* read = UI::FindTheOldWindow(manager, id);
    if (read == nullptr)
        return Core::Unexpected(std::string("0x4dcb70: la ventana de leer no se armó y el original sigue con NULL"));
    UI::SetTheLetter(manager, *read, shown, letter.body);
    // 0x491720: la ventana de la carta, si 0x491570 la había anotado.
    if (const auto found = manager.letterWindows.find(shown.index); found != manager.letterWindows.end())
        found->second = id;
    if (shown.name.substr(0, WebzenName.size() + 1) == WebzenName)
        read->letter.fromWebzen = 1;
    else
    {
        // La clase (los bits 4..6 y el 4 de pkt[8], 0x4977d0), el equipo (0x497810), la acción (0x4978f0), el ángulo
        // (0x497890, con el personaje armado) y el zoom (0x4978d0).
        UI::OldPortraitPart& portrait = read->letter.portrait;
        const std::uint8_t packed = letter.portraitClass;
        portrait.portraitClass = static_cast<std::uint8_t>(
            (static_cast<std::uint8_t>(packed & 0x10U) | static_cast<std::uint8_t>(packed >> 4U)) >> 1U);
        const UI::OldWindowStepView& view = windows.View();
        if (!view.portrait)
            return Core::Unexpected(std::string("0x4dcd0e: el personaje del retrato de las cartas es de Scenes "
                                                "(P-C212)"));
        if (auto set = view.portrait(portrait, UI::PortraitCallKind::SetClass, 0); !set.HasValue())
            return set;
        portrait.equipment = letter.equipment;
        if (auto set = view.portrait(portrait, UI::PortraitCallKind::SetEquipment, 0); !set.HasValue())
            return set;
        portrait.action = letter.action + ActionBase;
        if (auto set = view.portrait(portrait, UI::PortraitCallKind::SetAction, 0); !set.HasValue())
            return set;
        if (portrait.built != 0)
            portrait.angle = static_cast<float>((letter.direction & 0x3f) * AngleUnit);
        portrait.zoom = static_cast<float>(((letter.direction >> 6) * 5 + 0x28) * 2) * ZoomUnit;
    }
    return UI::Steps::PostAndStep(manager, *read, UI::ToTheEndMessage, windows.Globals(), windows.View());
}

Core::Expected<void, std::string> ApplyLetterDeleteResult(const Protocol::Letters::LetterDeleteResult& result,
                                                          const ChatReceiveContext& context)
{
    UI::OldWindowManager& manager = context.world.Runtime().oldWindows;
    if (result.result == 0)
    {
        if (auto shown = Notice(context, DeleteFailedText, "0x4dcd90"); !shown.HasValue())
            return shown;
    }
    else if (result.result == 1)
    {
        UI::RemoveALetter(manager.letterModel, result.index);
        UI::ForgetTheLetter(manager.letterModel, result.index);
    }
    RefreshTheTab(manager);
    return {};
}

Core::Expected<void, std::string> ApplyRoomAnswer(const Protocol::FriendChat::RoomAnswer& answer,
                                                  const ChatReceiveContext& context)
{
    UI::OldWindowManager& manager = context.world.Runtime().oldWindows;
    if (answer.result == 0 || answer.result == 2)
    {
        UI::ForgetAPendingChat(manager, answer.name);
        return Notice(context, answer.result == 0 ? NotThereText : OfflineText, "0x4dce10");
    }
    if (answer.result != 1)
        return {};
    const OldWindowsFromAPacket windows(context);
    auto title = TextOf(context, UI::ChatTitleText, "0x4dce10");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    if (answer.type == 0)
    {
        UI::ForgetAPendingChat(manager, answer.name);
        auto created = windows.Create(manager, UI::ChatCase, ChatPlace, ChatPlace, title.Value(), 0, "0x4dce10(1, 0)");
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        UI::OldWindow* chat = ChatOf(manager, created.Value());
        if (chat == nullptr)
            return Core::Unexpected(std::string("0x4dcfcf: el chat no se armó y el original sigue con NULL"));
        return JoinTheRoom(context, *chat, answer);
    }
    if (answer.type == 1)
    {
        // 0x4a6430: el chat que ya está con el amigo sale de su sala y entra a la nueva.
        const std::int32_t open = UI::FindTheChatWith(manager, answer.name, windows.View().heroName);
        if (open == -1)
            return {};
        if (open != 0)
        {
            UI::OldWindow* chat = ChatOf(manager, open);
            if (chat == nullptr)
                return Core::Unexpected(std::string("0x4dcf58: el chat de 0x4a6430 no está"));
            if (auto left = UI::LeaveTheChatRoom(manager, *chat); !left.HasValue())
                return left;
            return JoinTheRoom(context, *chat, answer);
        }
    }
    else if (answer.type != 2)
        return {};
    // 0x4dcf7e: el 2 (minimizado), la sala, el estado 5, al fondo (el 5 al administrador) y fuera de la pestaña de
    // las ventanas.
    auto created =
        windows.Create(manager, UI::CalledChatCase, ChatPlace, ChatPlace, title.Value(), 0, "0x4dce10(1, 2)");
    if (!created.HasValue())
        return Core::Unexpected(std::move(created).Error());
    const std::int32_t id = created.Value();
    UI::OldWindow* chat = ChatOf(manager, id);
    if (chat == nullptr)
        return Core::Unexpected(std::string("0x4dcf9c: el chat no se armó y el original sigue con NULL"));
    if (auto joined = JoinTheRoom(context, *chat, answer); !joined.HasValue())
        return joined;
    chat->base.state = MinimizedState;
    UI::PostToTheManager(manager, UI::SendBackMessage, id, 0);
    UI::Steps::ForgetTheWindowRow(manager, id);
    return {};
}

Core::Expected<void, std::string> ApplyInviteResult(const Protocol::FriendChat::InviteResult& result,
                                                    const ChatReceiveContext& context)
{
    UI::OldWindowManager& manager = context.world.Runtime().oldWindows;
    UI::OldWindow* chat = ChatOf(manager, static_cast<std::int32_t>(result.window));
    if (chat == nullptr)
        return {};
    std::size_t index = 0;
    std::string text;
    switch (result.result)
    {
    case 0:
        index = InviteFailedText;
        break;
    case 1:
    {
        // 0x4dd04f: el elegido de la lista de invitar (la vtable + 0x64), si hay, con el 1057 detrás.
        UI::OldChatPart& part = chat->chat;
        const std::optional<std::size_t> at = UI::SelectedInTheList(part.invite, part.invitable.size());
        if (!at || *at >= part.invitable.size())
            return {};
        text = UI::FriendName(part.invitable[*at].name);
        index = InvitedText;
        break;
    }
    case 3:
        index = InviteFullText;
        break;
    default:
        return {};
    }
    auto line = TextOf(context, index, "0x4dd020");
    if (!line.HasValue())
        return Core::Unexpected(std::move(line).Error());
    UI::AddAChatLine(*chat, NoSpeaker, text + line.Value(), SystemLine, 0);
    return {};
}

Core::Expected<void, std::string> ApplyRoomPacket(std::int32_t window, const Network::Packet& packet,
                                                  const ChatReceiveContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::OldWindowManager& manager = runtime.oldWindows;
    namespace Chat = Protocol::FriendChat;
    const std::optional<std::uint8_t> opcode = packet.Opcode();
    if (!opcode)
        return {};
    const OldWindowsFromAPacket windows(context);
    switch (*opcode)
    {
    case Chat::JoinOpcode:
    {
        auto join = Chat::ReadJoinResult(packet);
        if (!join.HasValue())
            return Core::Unexpected(std::string("0x4e6240(0): el paquete no se lee"));
        if (join.Value().result != 0)
            return {};
        return Notice(context, JoinFailedText, "0x4e6240(0)");
    }
    case Chat::MemberOpcode:
    {
        // 0x4dd1e0.
        auto member = Chat::ReadMember(packet);
        if (!member.HasValue())
            return Core::Unexpected(std::string("0x4dd1e0: el paquete no se lee"));
        UI::OldWindow* chat = ChatOf(manager, window);
        if (chat == nullptr)
            return {};
        const Chat::Member& got = member.Value();
        const std::string name = UI::FriendName(got.name);
        if (got.kind == 0)
        {
            const std::int32_t count =
                UI::AddAChatMember(manager, *chat, name, got.index, 0, windows.Texts(), windows.View().heroName);
            if (count > 2)
            {
                auto text = TextOf(context, CameInText, "0x4dd1e0");
                if (!text.HasValue())
                    return Core::Unexpected(std::move(text).Error());
                UI::AddAChatLine(*chat, NoSpeaker, name + text.Value(), SystemLine, 0);
            }
        }
        else if (got.kind == 1)
        {
            if (UI::ListCount(chat->chat.users, chat->chat.members.size()) > 2)
            {
                auto text = TextOf(context, WentOutText, "0x4dd1e0");
                if (!text.HasValue())
                    return Core::Unexpected(std::move(text).Error());
                UI::AddAChatLine(*chat, NoSpeaker, name + text.Value(), SystemLine, 0);
            }
            UI::RemoveAChatMember(manager, *chat, name, windows.Texts(), windows.View().heroName);
        }
        else
            return {};
        if (chat->chat.mode == 2)
            UI::FillTheInvites(manager, *chat, windows.View().heroName);
        return {};
    }
    case Chat::MembersOpcode:
    {
        // 0x4dd350 (en línea en 0x4e6240): sin mirar que la ventana esté.
        auto members = Chat::ReadMembers(packet);
        if (!members.HasValue())
            return Core::Unexpected(std::string("0x4e6240(2): el paquete no se lee"));
        for (const Chat::MemberRow& row : members.Value().members)
        {
            UI::OldWindow* chat = ChatOf(manager, window);
            if (chat == nullptr)
                return Core::Unexpected(std::string("0x4e6240(2): el chat no está y el original sigue con NULL"));
            static_cast<void>(
                UI::AddAChatMember(manager, *chat, row.name, row.index, 0, windows.Texts(), windows.View().heroName));
        }
        return {};
    }
    case Chat::MessageOpcode:
    {
        // 0x4dd3d0.
        auto message = Chat::ReadMessage(packet);
        if (!message.HasValue())
            return Core::Unexpected(std::string("0x4dd3d0: el paquete no se lee"));
        UI::OldWindow* chat = ChatOf(manager, window);
        if (chat == nullptr || message.Value().length > Chat::MessageLimit)
            return {};
        if (chat->base.state == MinimizedState)
        {
            UI::MarkTheChatWaiting(manager, window, runtime.lastSoundId);
            auto text = TextOf(context, ChatWaitingText, "0x4dd47c");
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            if (auto added = SystemMessage(context, text.Value(), "0x4dd47c"); !added.HasValue())
                return added;
            chat->base.state = HiddenState;
            UI::Steps::AddTheWindowRow(manager, window, chat->frame.title.value_or(std::string()));
        }
        else if (chat->base.state == HiddenState || UI::LastOldWindow(manager) != window)
            UI::MarkTheChatWaiting(manager, window, runtime.lastSoundId);
        UI::AddAChatLine(*chat, message.Value().index, message.Value().text, MemberLine, 0);
        return {};
    }
    case Chat::NoticeOpcode:
    {
        // 0x4e6299: pkt[0x68] = 0 y, si hay texto, al chat.
        auto notice = Chat::ReadNotice(packet);
        if (!notice.HasValue())
            return Core::Unexpected(std::string("0x4e6240(0xd): el paquete no se lee"));
        if (notice.Value().text.empty())
            return {};
        return SystemMessage(context, notice.Value().text, "0x4e6240(0xd)");
    }
    default:
        return {};
    }
}

Core::Expected<void, std::string> ApplyChatRoomClosed(const App::ChatRoomClosed& closed,
                                                      const ChatReceiveContext& context)
{
    UI::OldWindowManager& manager = context.world.Runtime().oldWindows;
    if (!closed.opened)
    {
        UI::ForgetTheChatRoom(manager.chatRooms, closed.room);
        return {};
    }
    // 0x49e480 con FD_CLOSE: el chat de la sala, si está, con el 402 de nadie (0x493f80 con 0xff y 1).
    UI::OldWindow* chat = ChatOf(manager, closed.window);
    if (chat == nullptr)
        return {};
    auto text = TextOf(context, RoomClosedText, "0x49e480");
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    UI::AddAChatLine(*chat, NoSpeaker, text.Value(), SystemLine, 0);
    return {};
}

void FlushTheChatRoomOrders(UI::ChatRooms& rooms, App::IChatRoomLinks* links)
{
    while (!rooms.orders.empty())
    {
        const UI::ChatRoomOrder order = std::move(rooms.orders.front());
        rooms.orders.pop_front();
        if (links == nullptr)
            continue;
        switch (order.kind)
        {
        case UI::ChatRoomOrder::Kind::Message:
            links->SendToChatRoom(order.room, Protocol::FriendChat::WriteMessage(order.text));
            break;
        case UI::ChatRoomOrder::Kind::Leave:
            links->SendToChatRoom(order.room, Protocol::FriendChat::WriteLeave());
            links->CloseChatRoom(order.room);
            break;
        case UI::ChatRoomOrder::Kind::Ping:
            links->SendToChatRoom(order.room, Protocol::FriendChat::WritePing());
            break;
        }
    }
}
} // namespace Client::Scenes
