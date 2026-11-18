#pragma once

namespace Client::Render
{
class ModelBatch;
}

namespace Client::World
{
struct ModelEffect;
}

namespace Client::Scenes
{
// Sólo para las señales que reconoce World::IsSiegeCue. Usa triángulos sin textura.
void DrawSiegeCue(Render::ModelBatch& batch, const World::ModelEffect& effect);
} // namespace Client::Scenes
