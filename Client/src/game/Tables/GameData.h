#pragma once

#include "Core/Expected.h"
#include "Tables/AddedEmotes.h"
#include "Tables/GateTable.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/MonsterSkillTable.h"
#include "Tables/QuestTable.h"
#include "Tables/SkillTable.h"
#include "Tables/Credits.h"
#include "Tables/DialogTable.h"
#include "Tables/MoveRequirements.h"
#include "Tables/NpcNames.h"
#include "Tables/SlideTable.h"
#include "Tables/TextTable.h"
#include "UI/MacroFile.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Read once, before the first scene, and used by the ones after. Lives in main.cpp, like the SceneManager.
namespace Client::Tables
{
// As the original asks for it ("Data\Local\Text.bmd", 0x6d4370).
inline constexpr std::string_view TextTableFile = "Local\\Text.bmd";
// As 0x61ccb0 asks for them (0x6d2a7c and 0x6d2ab8).
inline constexpr std::string_view FilterNameFile = "Local\\FilterName.bmd";
inline constexpr std::string_view FilterFile = "Local\\Filter.bmd";
// 0x54a360's table, asked by 0x61ccb0 during the load (05 § Gate.bmd).
inline constexpr std::string_view GateTableFile = "Gate.bmd";
// Item metadata used by 0x54bf40, loaded once by 0x54b9e0.
inline constexpr std::string_view ItemTableFile = "Local\\Item.bmd";
// 0x54acf0: 256 records of 0x3c and a required checksum (05 § Tablas .bmd).
inline constexpr std::string_view SkillTableFile = "Local\\Skill.bmd";
// As 0x40c600 asks for them from 0x61ccb0 (0x6c63e8 and 0x6c63c8).
inline constexpr std::string_view ItemSetTypeFile = "Local\\ItemSetType.bmd";
inline constexpr std::string_view ItemSetOptionFile = "Local\\ItemSetOption.bmd";
// As 0x63ff30 asks for it ("Data\Local\credit.bmd", 0x6d66e0).
inline constexpr std::string_view CreditsFile = "Local\\credit.bmd";
// As 0x61ee30 asks for them (0x6c649c), after Text.bmd.
inline constexpr std::string_view MacroFile = "Macro.txt";
// The banner's texts, as 0x61ccb0 asks for them (0x61ed18, with 0x6d29c8).
inline constexpr std::string_view SlideTableFile = "Local\\Slide.bmd";
// The /move window's destinations, as 0x61ccb0 asks for them (0x61ec8f, with 0x6d2a18).
inline constexpr std::string_view MoveRequirementsFile = "Local\\movereq.bmd";
// As 0x61ccb0 asks for them (0x61eb69 with 0x6d2a48 and 0x61eb7c with 0x6d2a30).
inline constexpr std::string_view DialogTableFile = "Local\\Dialog.bmd";
inline constexpr std::string_view QuestTableFile = "Local\\quest.bmd";
// As 0x61ccb0 asks for them (0x61ecdf, with 0x6d29fc).
inline constexpr std::string_view MonsterSkillTableFile = "Local\\MonsterSkill.bmd";
// As 0x61ccb0 asks for them (0x61ed05, with 0x6d29e0 and [0x5763350]'s language). The language is *inferred*
// (P-C352): Eng, byte for byte equal to the lab's Chs.
inline constexpr std::string_view NpcNameFile = "Local\\NpcName(Eng).txt";

struct GameData
{
    // The font's face is text 0.
    std::optional<TextTable> text;
    // The words a new character's name cannot have (UI::NameFilters).
    std::vector<std::string> filterName;
    std::vector<std::string> filter;
    std::optional<GateTable> gates;
    // Q-C01: checksumMatches stays available for the integrator's warning.
    std::optional<LoadedItemTable> items;
    // [0x6eed3c]'s tables (05, "Las tablas de los conjuntos").
    std::optional<ItemSetTables> itemSets;
    // The plain table, without the original's guard container (Q-C08).
    std::optional<SkillTable> skills;
    // Or none if missing. The original reads it each time it builds the login UI (0x63ff30) and, if missing, shows a
    // MessageBox and closes the game (WM_DESTROY); MuRet reads it once, and without it the credits close when opened
    // (P-C78).
    std::vector<CreditEntry> credits;
    // Data\Macro.txt (0x61c1c0): main.cpp copies them to ChatInputState::macros when it builds the world.
    UI::Macros macros{};
    // [0x5763528]'s banner period, speed and texts. Empty if missing.
    std::optional<SlideTable> slides;
    // main.cpp copies them to WorldRuntime::moves (0x723c68's list). Empty if missing, like when fopen fails in
    // 0x448440.
    std::vector<MoveRequirement> moves;
    // 0x7688ac0's 200 dialogs, which main.cpp copies to WorldRuntime::dialogTable. Without the file, 200 empty ones
    // (the original warns and closes the window, P-C170).
    DialogTable dialogs;
    // The 200 records of [0x710148] + 0xc, which main.cpp copies to WorldRuntime::quest. Without the file, 200 zeros,
    // as 0x418d10 leaves the .bss ones.
    QuestTable quests;
    // [0x7437670]'s skills, which main.cpp copies to WorldRuntime::monsterSkills. Without the file, the .bss (the
    // original warns and closes the window, 0x54a500..0x54a549).
    MonsterSkillTable monsterSkills;
    // NpcName(%s).txt: the records 0x54e7f0 leaves at 0x7681eb8, which main.cpp copies to the start of
    // WorldRuntime::npcNames. Empty if missing, like when fopen fails.
    std::vector<NpcNameRecord> npcNames;
    // MuRet's Data\MuRet\Emotes.txt, which GameClient copies to WorldRuntime::addedEmotes. Empty if missing.
    std::vector<AddedEmote> addedEmotes;
};

// WinMain's work before scene 1: Text.bmd at 0x4b9b9f (0x61ee30 and 0x5495e0), before the fonts (the face is its
// text 0), then what 0x61ccb0 loads, each found in `dataRoot` ignoring case (ResolveDataPath). Required, an error
// naming the file if missing, unreadable or malformed: Text.bmd (the original reads a short one anyway and keeps what
// was in memory, 05, Tablas .bmd), Gate.bmd, Item.bmd (the non-fatal checksum, Q-C01, goes to
// items->checksumMatches), the two set tables (0x40c660 and 0x40c850 show their notice and close) and Skill.bmd (a
// different checksum is rejected, 0x54acf0; the table stays plain, without the guard container, Q-C08). Optional,
// empty if missing (where the original warns and closes, or does not start), an error naming the file if unreadable
// or malformed: the two filters (a checksum that does not match too), Macro.txt after Text.bmd like 0x61ee30 (empty
// like when fopen fails in 0x61c1c0; a word that overflows its place is the error, Q-C531), Slide.bmd (0x46b3b0; the
// banner keeps its constructor's, the original says "File not exist." and closes, Q-C517), credit.bmd, Dialog.bmd and
// quest.bmd before movereq.bmd (0x61eb69 and 0x61eb7c; 200 empty records, a short one with what its fread leaves,
// Q-C1127, an error below one record or with over 10 answers in a Dialog.bmd record), movereq.bmd before the set
// tables like 0x61ccb0 (0x448440 returns 0 and nobody checks; a name without its end is an error), MonsterSkill.bmd
// after them (0x61ecdf; the .bss table; a type outside [0, 0xffff] is an error) and NpcName(Eng).txt (0x61ed05; over
// 1 MiB, or what the original would not load without overwriting something, Tables::LoadNpcNames, is an error). Last,
// MuRet's own emotes (AddedEmotesFile), empty if missing and an error naming the file if unreadable or malformed.
[[nodiscard]] Core::Expected<GameData, std::string> LoadGameData(const std::filesystem::path& dataRoot);

// 0x61c170: fopen("wt") of Data\Macro.txt, which creates or empties it, and `content` (UI::WrittenMacros). The macros'
// content does not go to the log. Leaving the game (GameSceneUi) and changing server (0x412b6d) share it.
[[nodiscard]] Core::Expected<void, std::string> WriteMacroFile(const std::filesystem::path& dataRoot,
                                                               std::string_view content);
} // namespace Client::Tables
