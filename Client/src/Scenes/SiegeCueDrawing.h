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
// Señales de Castle Siege y Crywolf con triángulos sin textura.
void DrawBattleCue(Render::ModelBatch& batch, const World::ModelEffect& effect);
} // namespace Client::Scenes
