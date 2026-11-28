#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui

// CL9 milestone 11, phase 2: the event drawings in the game's 2D pass (06 § Los eventos), *static*: there is no GL
// trace of any of them; what the VM has to confirm is in 08 (P-C294).
namespace Client::Events
{
struct EventMapObject;

// 0x5a7eb0, after the HUD (0x637668): with the F3 22 word (0x5767816) other than 0 and at most 10 minutes, the time in
// font 0x576355c, centered on half the world's width (0x5bb8d0, with its rand()): in color at (x, 350) and in white at
// (x + 1, 351). The other half (0x5a800e, the guild war with 0x57678ed other than 0x77f3cc0) belongs to milestone 8:
// MuRet does not write 0x57678ed and does not reach it.
[[nodiscard]] Core::Expected<void, std::string> DrawTheEventTime(Ui::GameHudContext& context);

// The event object's method +4: 0x40c2d0 (Blood Castle, maps 11 to 17, +0x10 at 1, 2 or 5) and 0x405090 (Chaos Castle,
// maps 18 to 23, +0x10 from 6 to 8), with +0x18 (the time) at 1 or more: the points of +0x20 and +0x1c, 865 and the
// time (0x40bbf0). Chaos Castle then picks the trap area (0x6c634c) by +0x20, only if `write` (an intermediate MuRet
// drawing does not write). Devil Square (0x40bdb0) is a ret.
[[nodiscard]] Core::Expected<void, std::string> DrawTheCastleTimeAndPoints(Ui::GameHudContext& context,
                                                                           EventMapObject& event, bool write);

// 0x632a2b: the event object's method +0x10 with box 0x8b, 0x97 or 0x9b, by its type and not by the box: the results
// of Devil Square (0x40bdd0, the ranking of +0x24 records of +0x2c), Blood Castle (0x40c440) and Chaos Castle
// (0x405220), with the record of +0x2c (+0x38, +0x3c and +0x40) and +0x24.
[[nodiscard]] Core::Expected<void, std::string> DrawTheEventBoxText(Ui::GameHudContext& context,
                                                                    const EventMapObject& event);
} // namespace Client::Events
