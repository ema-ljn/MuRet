#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/CastleWindows.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/StderrSink.h"
#include "Protocol/CastleSiege.h"
#include "Scenes/CastleSeniorAnswers.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/PanelDialog.h"

#include <cstdint>
#include <string>

namespace Client::Scenes
{
Core::Expected<bool, std::string> AnswerTheCastleWindowDialog(WorldState& world, const CastleSender& send,
                                                              UI::PanelDialogOwner owner, std::uint32_t reason)
{
    if (owner == UI::PanelDialogOwner::CastleGiveUp)
    {
        if (reason == UI::PanelAnswerConfirm)
        {
            if (!send)
                return Core::Unexpected(std::string("0x64ccd0: renunciar al asedio sin el cable"));
            send(Protocol::CastleSiege::WriteGiveUp());
        }
        return true;
    }
    using Answer = void (*)(std::int32_t, const CastleAnswerContext&);
    Answer answer = nullptr;
    switch (owner)
    {
    case UI::PanelDialogOwner::CastleGate:
        answer = AnswerTheGateDialog;
        break;
    case UI::PanelDialogOwner::CastleGateLife:
        answer = AnswerTheGateLifeDialog;
        break;
    case UI::PanelDialogOwner::CastleGateDefense:
        answer = AnswerTheGateDefenseDialog;
        break;
    case UI::PanelDialogOwner::CastleStatue:
        answer = AnswerTheStatueDialog;
        break;
    case UI::PanelDialogOwner::CastleStatueLife:
        answer = AnswerTheStatueLifeDialog;
        break;
    case UI::PanelDialogOwner::CastleStatueDefense:
        answer = AnswerTheStatueDefenseDialog;
        break;
    case UI::PanelDialogOwner::CastleStatueRecovery:
        answer = AnswerTheStatueRecoveryDialog;
        break;
    case UI::PanelDialogOwner::CastleTaxes:
        answer = AnswerTheTaxDialog;
        break;
    case UI::PanelDialogOwner::CastleMoney:
        break;
    default:
        return Core::Unexpected(std::string("0x6600d0: el diálogo no tiene dueño del castillo"));
    }
    const auto wanted = owner == UI::PanelDialogOwner::CastleMoney ? UI::PanelAnswerOk : UI::PanelAnswerConfirm;
    if (reason != wanted)
        return true;
    if (!send)
        return Core::Unexpected(std::string("la respuesta del castillo sin el cable"));
    Core::StderrSink sink;
    Core::Log log(sink, Core::LogLevel::Warning);
    const CastleAnswerContext context{.data = world.Runtime().castleSiege.data, .log = log, .send = send};
    if (owner == UI::PanelDialogOwner::CastleMoney)
        AnswerTheMoneyDialog(static_cast<std::int32_t>(reason), world.Runtime().chat.input.dialogEdit.text.Value(),
                             context);
    else
        answer(static_cast<std::int32_t>(reason), context);
    return true;
}
} // namespace Client::Scenes
