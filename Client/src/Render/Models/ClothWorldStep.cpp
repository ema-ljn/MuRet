#include "Render/Models/ClothWorldStep.h"

#include "Render/Models/Cloth.h"

namespace Client::Render::Models
{
ClothWorldStep::ClothWorldStep(ClothWorld* world) : m_world(world)
{
    if (m_world != nullptr)
        BeginTheClothWorldStep(*m_world);
}
ClothWorldStep::~ClothWorldStep()
{
    if (m_world != nullptr)
        StepTheClothWorld(*m_world);
}
} // namespace Client::Render::Models
