#pragma once

namespace Client::Actors::Drawing
{
struct ItemDrawContext;
struct ItemDrawResources;
} // namespace Client::Actors::Drawing

namespace Client::Session
{
// What the items create in other modules, on `resources`: of the joints (game/Joints), only Helper07's 0x613/4 and
// 0x614/14 with their opaque owner, and of the model effects, only the lightning 0xc2/3 (game/Fauna) and the dragon,
// the arrival and the teleport with subtype 0. The rest stops with 0x530860 or 0x518cf0.
void FillItemCreators(Actors::Drawing::ItemDrawContext& item, const Actors::Drawing::ItemDrawResources& resources);
} // namespace Client::Session
