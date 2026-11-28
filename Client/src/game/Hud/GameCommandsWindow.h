#pragma once

#include "Core/Expected.h"
#include "Chat/ChatCommands.h"

#include <string>

namespace Client::Chat
{
struct GameChatContext;
} // namespace Client::Chat

// The commands window (D, CL8 milestone 6, 08 P-C92): the object [0x6e592c], with its step (0x40a450) and its drawing
// (0x40a650). Choosing a command, the notice it gives, closing it and the chosen character's card (0x6c8e30, which the
// picking leaves since CL9 milestone 1; 0x40aa80..0x40abd8) are ported, and what the chosen command does on them with
// the right click (0x406050 from 0x406097, CL9 milestone 7, phase 3): the trade, the personal shop, the party, the
// whisper, follow and the duel; the four guild ones stop toward milestone 8 (08 P-C209).
namespace Client::Hud
{

// 0x40a450, from 0x5ee140 with ECX = [0x6e592c]: with +4 at 1, the mouse over the window (from the corner the drawing
// left, 0xbe × 0x1b1) belongs to the UI (0x775857c); 0x406050 sets the twelve states to 1 and, with a chosen command
// and a chosen character, reads the four cells, chooses the target and the command's states and, with the right click,
// runs it (with Text.bmd and the `chat` cable; without them, the command stops); the click on buttons 1 to 11
// (108 × 20, every 28, edges excluded) spends it, releases the button (0x7d36874) and chooses the command (+0x20); the
// click on the close button closes it without Close: the two sounds, 0x77f3b10 at 0, 0x6c8dcc at 6 and
// ResetTheCommands. It draws no rand().
[[nodiscard]] Core::Expected<void, std::string> StepTheCommandsWindow(const Ui::GameUiContext& context);

// 0x40a650, from 0x5f7440 (0x5f75fa): with +4 at 1, the corner at (0x1c2, 0), the background (0x5ee220), the title
// (text 938), the eleven buttons (Message_box, the chosen one in gray), the close button with its help (text 927 with
// "D"), the commands' names centered with their checkbox (0x463090), the big font, the chosen player's card with their
// name (0x40aa80..0x40abd8), the chosen command's notice by its state and the blend without alpha at the end.
[[nodiscard]] Core::Expected<void, std::string> DrawTheCommandsWindow(Ui::GameHudContext& context);

// 0x573f85..0x574bd5 (/guild outside Chaos Castle, CL9 milestone 8): with a guild, notice 255; otherwise, the chosen
// one or the one in front (Social::TheOneToAsk) and, with the world loaded, 0x78aba40 with their key, the C1 05 50
// through the `chat` cable and notice 477 with their name. Without Text.bmd or without the cable when needed, it stops.
[[nodiscard]] Core::Expected<Chat::ChatCommandOutcome, std::string>
RequestAGuildJoin(const Chat::GameChatContext& chat);

// 0x570d7e..0x573f48 (/union, /rival and /rivaloff outside Chaos Castle, CL9 milestone 8): without a guild, notice
// 1355; otherwise, the chosen one or the one in front and `command`'s E5 (01 01, 02 01 or, after the 0xb7 animation and
// command 8's C1 05 18, 02 02) with its notice (1358, 1359 or 1360). Without Text.bmd, without the cable or without the
// models (/rivaloff), it stops.
[[nodiscard]] Core::Expected<Chat::ChatCommandOutcome, std::string>
RequestAGuildRelation(const Chat::GameChatContext& chat, Chat::GuildRelationCommand command);
} // namespace Client::Hud

namespace Client::Ui
{
struct GameHudContext;
struct GameUiContext;
} // namespace Client::Ui
