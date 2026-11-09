#pragma once

namespace Client::Render::Models
{
struct ClothWorld;
// El dispatch de la escena queda dentro del scope: snapshot/cierre/refresco antes; viento y
// +1 del valor reabierto después, también si una rama de UI retorna temprano.
class ClothWorldStep
{
public:
    explicit ClothWorldStep(ClothWorld* world);
    ClothWorldStep(const ClothWorldStep&) = delete;
    ClothWorldStep& operator=(const ClothWorldStep&) = delete;
    ~ClothWorldStep();

private:
    ClothWorld* m_world;
};
} // namespace Client::Render::Models
