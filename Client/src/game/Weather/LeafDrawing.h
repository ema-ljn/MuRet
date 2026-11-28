#pragma once

namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render

// The leaves' draw in the world pass (0x5306a0).
namespace Client::Weather
{
class Leaves;

// 0x5306a0, in the world pass, after the model effects: the live leaves, additive (on map 0x37, 0x620a90) and with
// the white of a single glColor3f(1, 1, 1), each one the square of LeafQuadOf translated to its position, with its
// type's texture.
void DrawLeaves(Render::ModelBatch& batch, const Leaves& leaves);
} // namespace Client::Weather
