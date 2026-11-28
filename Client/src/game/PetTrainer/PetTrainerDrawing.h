#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui

// The pet trainer's drawing (0x44b510, CL9 milestone 6, phase 4b; 06 § HUD y ventanas, "El entrenador"), which
// 0x5f7440 always calls after the chaos box (0x5f75b7). It dispatches by 0x724580's mode like the step
// (PetTrainer/PetTrainer.h): 0x44b610 the menu, 0x44c850 recovering the pet and 0x44d680 the box. Only mode 2 takes
// rand(): per button, 0x44b190's own-record closes (one, or two with a price other than 0) and its own access's to
// compare the zen.
namespace Client::PetTrainer
{

// 0x44b510: with 0x724580 at 0, at 4 or outside 1..3, nothing. A missing Text.bmd text, one that overflows its stack
// buffer or 0x724584's mix outside 0..2 (which reads past 0x6b8c68) fails.
//   - The menu (0x44b610): the background of 0xfb and 0xfc without blending, title 1227 in a box of 203 · W / 640 at
//     (0xda, 0x64), texts 1202 and 1203 (with the hero's name) centered on 0x13f, the two buttons (1204 and 1205, in
//     bold with the mouse over them) and the close one with its help (1002) in [0x18a, 0x1a2) × [0x109, 0x121), one
//     further right than the one that closes in the step (Q-C958).
//   - Recovery (0x44c850): the same with 1228, without drawing the 1203 it builds (Q-C959), and per button (1187 the
//     horse, 1214 the raven) 0x44b190's price (1229 to 1232) 0x1e lower, in white on red if the zen falls short.
//   - The box (0x44d680): the window at (0x104, 0) with 0x723c80's 8 × 4 grid, the mix button and its help (1212),
//     title 1205 with the tax (1623), 0x724584's mix (1213 without a mix, "%s %s" with 1187 or 1214), the rate (584, 60
//     with a mix), the taxed price (1622) and warnings 586 and 587.
[[nodiscard]] Core::Expected<void, std::string> DrawTheTrainer(Ui::GameHudContext& context);
} // namespace Client::PetTrainer
