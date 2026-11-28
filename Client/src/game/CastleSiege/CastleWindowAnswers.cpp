#include "CastleSiege/CastleSiegeReceive.h"
#include "CastleSiege/CastleWindows.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/StderrSink.h"
#include "Protocol/CastleSiege/CastleSiege.h"
#include "CastleSiege/CastleSeniorAnswers.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/PanelDialog.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Chat/ChatInput.h"

#include <cstdint>
#include <string>

namespace Client::CastleSiege
{
Core::Expected<bool, std::string> AnswerTheCastleWindowDialog(Kernel::WorldState& world, const CastleSender& send,
                                                              Ui::PanelDialogOwner owner, std::uint32_t reason)
{
    if (owner == Ui::PanelDialogOwner::CastleGiveUp)
    {
        if (reason == Ui::PanelAnswerConfirm)
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
    case Ui::PanelDialogOwner::CastleGate:
        answer = AnswerTheGateDialog;
        break;
    case Ui::PanelDialogOwner::CastleGateLife:
        answer = AnswerTheGateLifeDialog;
        break;
    case Ui::PanelDialogOwner::CastleGateDefense:
        answer = AnswerTheGateDefenseDialog;
        break;
    case Ui::PanelDialogOwner::CastleStatue:
        answer = AnswerTheStatueDialog;
        break;
    case Ui::PanelDialogOwner::CastleStatueLife:
        answer = AnswerTheStatueLifeDialog;
        break;
    case Ui::PanelDialogOwner::CastleStatueDefense:
        answer = AnswerTheStatueDefenseDialog;
        break;
    case Ui::PanelDialogOwner::CastleStatueRecovery:
        answer = AnswerTheStatueRecoveryDialog;
        break;
    case Ui::PanelDialogOwner::CastleTaxes:
        answer = AnswerTheTaxDialog;
        break;
    case Ui::PanelDialogOwner::CastleMoney:
        break;
    default:
        return Core::Unexpected(std::string("0x6600d0: el diálogo no tiene dueño del castillo"));
    }
    const auto wanted = owner == Ui::PanelDialogOwner::CastleMoney ? Ui::PanelAnswerOk : Ui::PanelAnswerConfirm;
    if (reason != wanted)
        return true;
    if (!send)
        return Core::Unexpected(std::string("la respuesta del castillo sin el cable"));
    Core::StderrSink sink;
    Core::Log log(sink, Core::LogLevel::Warning);
    const CastleAnswerContext context{.data = world.Runtime().castleSiege.data, .log = log, .send = send};
    if (owner == Ui::PanelDialogOwner::CastleMoney)
        AnswerTheMoneyDialog(static_cast<std::int32_t>(reason), world.Runtime().chatInput.dialogEdit.text.Value(),
                             context);
    else
        answer(static_cast<std::int32_t>(reason), context);
    return true;
}
} // namespace Client::CastleSiege
