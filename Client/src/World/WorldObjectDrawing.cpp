#include "World/WorldObjectDrawing.h"
#include "Core/Expected.h"
#include "World/WorldObjectDrawingError.h"
#include <optional>
#include "World/WorldObjectDrawingOperations.h"
#include "World/WorldObjectDrawingContext.h"
#include "World/WorldObjectDrawingModel.h"
#include "World/WorldObjectDrawingPass.h"
#include "World/WorldObjectDrawingOwner.h"
#include "World/WorldObjectDrawingHook.h"
#include "World/IWorldObjectDrawingServices.h"
#include "World/WorldObjects.h"
#include "Render/Models/Cloth.h"
#include <glm/ext/vector_float3.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace Client::World
{
namespace
{
using Hook = WorldObjectDrawingHook;
constexpr std::array<Hook, 6> BodyHooks{Hook::Body438fe0, Hook::Body42cc10, Hook::Body4403b0,
                                        Hook::Body42f170, Hook::Body425d90, Hook::Body4306a0};
class Drawing
{
public:
    Drawing(WorldObjectDrawingOperations& op, std::uint32_t mode)
        : m_op(op), m_object(op.object), m_context(op.context), m_mode(mode)
    {
    }
    bool Execute();

private:
    bool Begin(float alpha)
    {
        return m_op.Run(m_context.services.Begin(*m_op.model, alpha));
    }
    bool End()
    {
        return m_op.Run(m_context.services.End(*m_op.model));
    }
    bool Shadow(std::int32_t tag = -1, std::int32_t mesh = -1, std::int32_t first = -1, std::int32_t end = -1)
    {
        return m_op.Run(m_context.services.Shadow(*m_op.model, tag, mesh, first, end));
    }
    void Light(float r, float g, float b)
    {
        m_op.model->light = glm::vec3(r, g, b);
    }
    void White()
    {
        Light(1, 1, 1);
    }
    bool D(std::uint32_t mask, std::int32_t texture = -1)
    {
        return m_op.Body(mask, texture);
    }
    bool P(std::int32_t mesh, std::uint32_t mask, std::int32_t texture = -1)
    {
        return m_op.Mesh(mesh, mask, texture);
    }
    bool GlowMeshes(int count);
    bool SuppressShadow() const
    {
        const auto map = m_context.map;
        return map == 10 || (map >= 24 && map <= 29) || map == 36;
    }
    bool PrepareShadow(float opacity);
    bool Ground();
    bool ModeTen();
    bool Early(bool& handled);
    bool Spirit();
    bool Map(bool& handled);
    bool Handlers(bool& handled);
    bool Types();
    bool ExtendedTypes();
    bool TransitionTypes();
    bool Continuation();
    bool FireOutputs();
    bool ObjectFire();
    bool Special();
    bool SpecialJoints(std::int32_t subtype);
    bool Death(std::int32_t target);
    WorldObjectDrawingOperations& m_op;
    WorldObject& m_object;
    WorldObjectDrawingContext& m_context;
    std::uint32_t m_mode;
};
bool Drawing::Execute()
{
    if (m_context.editorState == 0 && m_object.state.hiddenMesh == -2)
        return true;
    if (m_mode == 10)
        return ModeTen();
    if (m_context.map == 10 && m_object.type == 0x171)
        Light(.02f, .05f, .15f);
    if (m_context.map == 9 && m_object.type == 0x164)
    {
        m_op.model->light.y = .3f;
        m_op.model->light.x = 0;
        m_op.model->light.z = 1;
    }
    if (m_mode != 0 && m_object.type == 0x16d)
        Light(0, 0, 1);
    if (m_object.renderMask == 0x80)
        return D(0x82);
    bool handled = false;
    if (!Early(handled))
        return false;
    if (handled)
        return true;
    if (!Map(handled))
        return false;
    if (handled)
        return true;
    switch (m_object.renderFlags & 3u)
    {
    case 1:
        Light(.3f, 1, .5f);
        break;
    case 2:
        Light(.3f, .5f, 1);
        break;
    case 3:
        Light(.3f, 1, .8f);
        break;
    default:
        break;
    }
    const auto type = m_object.type;
    if ((type == 0x175 || type == 0x161 || type == 0x184 || type == 0x185) &&
        std::bit_cast<std::uint32_t>(m_object.alpha) == 0x3f800000u &&
        std::bit_cast<std::uint32_t>(m_object.state.light) == 0x3d4ccccdu)
        m_object.state.light = 1;
    if (!Handlers(handled))
        return false;
    if (!handled && !Types())
        return false;
    return Continuation();
}
bool Drawing::ModeTen()
{
    constexpr std::size_t OwnerCount = 400;
    for (std::size_t i = 0; i < OwnerCount; ++i)
    {
        auto entry = m_context.services.OwnerEntry(i);
        if (!entry.HasValue())
            return m_op.Run(Core::Unexpected(entry.Error()));
        auto& owner = entry.Value().get();
        if (owner.identity != m_object.owner || owner.identity == m_context.hero)
            continue;
        const auto query = m_context.services.OwnerQuery();
        if (!query.HasValue())
            return m_op.Run(Core::Unexpected(query.Error()));
        if ((query.Value() & 0xffu) == 1 && (owner.flags & 0x4000u) != 0 && owner.state != 1)
            return true;
    }
    White();
    std::int32_t mesh = 0;
    while (true)
    {
        auto current = m_context.services.Model(m_object.type);
        if (!current.HasValue())
            return m_op.Run(Core::Unexpected(current.Error()));
        if (mesh >= current.Value().get().meshCount)
            return true;
        auto pass = m_op.Pass(0x4040, 0x620);
        pass.alpha = .5f;
        pass.u = static_cast<float>(static_cast<double>(m_object.state.uv.x) * 8);
        pass.v = static_cast<float>(static_cast<double>(m_object.state.uv.y) * 2);
        if (!m_op.Mesh(mesh, pass))
            return false;
        ++mesh;
    }
}
bool Drawing::PrepareShadow(float opacity)
{
    return m_op.Run(m_context.services.AlphaBlend(1)) &&
           m_op.Run(m_context.services.Color(0, 0, 0, m_context.map == 7 ? .2f : opacity));
}
bool Drawing::Ground()
{
    const float x = m_object.transform.position.x, y = m_object.transform.position.y;
    const auto height = m_context.services.TerrainHeight(x, y);
    if (!height.HasValue())
        return m_op.Run(Core::Unexpected(height.Error()));
    m_op.model->shadowOrigin.z = height.Value();
    m_op.model->shadowOrigin.x = x;
    m_op.model->shadowOrigin.y = y;
    return true;
}
bool Drawing::GlowMeshes(int count)
{
    if (!Begin(m_object.alpha))
        return false;
    for (int mesh = 0; mesh < count; ++mesh)
        if (!P(mesh, 2) || !P(mesh, 0x44, 0x5c0))
            return false;
    return End();
}
bool Drawing::Early(bool& handled)
{
    handled = true;
    switch (m_object.type)
    {
    case 0x16c:
        Light(.4f, .6f, 1);
        m_op.model->step.flag88 = 0;
        if (!D(2, 0x5c0))
            return false;
        White();
        m_op.model->step.flag88 = 0xff;
        return D(2);
    case 0x110:
        return D(2);
    case 0x137:
        if (m_object.state4 == 0)
        {
            m_op.model->light = m_object.light;
            return D(2, 0x5c0);
        }
        if (m_object.state4 == 1)
        {
            Light(.1f, .5f, 1);
            return D(0x42, 0x5c0);
        }
        return true;
    case 0x136:
    {
        if (!D(2))
            return false;
        White();
        auto pass = m_op.Pass(4, 0x5c0);
        pass.alpha = .5f;
        pass.u = static_cast<float>(static_cast<double>(m_object.state.uv.x) * 5);
        pass.v = static_cast<float>(static_cast<double>(m_object.state.uv.y) * 2);
        pass.hiddenMesh = -1;
        return m_op.Body(pass);
    }
    case 0xbf:
        White();
        if (!D(2))
            return false;
        Light(1, .8f, .3f);
        for (int mesh = 12; mesh <= 15; ++mesh)
            if (!P(mesh, 0x44, m_object.state.hiddenMesh))
                return false;
        return SuppressShadow() || (PrepareShadow(.7f) && Shadow(-1, -1, 8, 9));
    case 0x14a:
    case 0x14b:
    case 0x14c:
        return Special();
    case 0xa8:
    case 0xa9:
    case 0xaa:
    case 0xab:
    case 0xac:
        Light(4.8f, 4.8f, 4.8f);
        if (!D(2))
            return false;
        White();
        return true;
    case 0x117:
        m_op.model->light = m_object.light;
        if (!D(2))
            return false;
        White();
        return true;
    case 0xc0:
        return Spirit();
    default:
        handled = false;
        return true;
    }
}
bool Drawing::Spirit()
{
    if (!m_op.Run(m_context.services.Color(1, 1, 1, std::nullopt)) || !Begin(m_object.alpha) ||
        !P(3, 2, m_object.state.hiddenMesh))
        return false;
    const auto level = m_object.level8e;
    if (level < 40)
    {
        if (!P(level < 20 ? 2 : 1, 2, m_object.state.hiddenMesh))
            return false;
    }
    else
    {
        if (!P(0, 2, m_object.state.hiddenMesh))
            return false;
        Light(.3f, .6f, 1);
        if (!P(0, 0x44, m_object.state.hiddenMesh))
            return false;
        White();
        auto pass = m_op.Pass(0x42, 0x6f7);
        pass.blendMesh = 3;
        pass.blendLight = static_cast<float>(std::sin(static_cast<double>(m_context.worldTime) * .001f));
        if (!m_op.Mesh(3, pass))
            return false;
    }
    if (!End())
        return false;
    return SuppressShadow() || (PrepareShadow(1) && Ground() && Shadow());
}
bool Drawing::Map(bool& handled)
{
    handled = true;
    const auto map = m_context.map;
    const auto type = m_object.type;
    if (map == 0 && type == 0x69)
    {
        if (!Begin(m_object.alpha))
            return false;
        for (int mesh = 0; mesh < 3; ++mesh)
        {
            auto pass = m_op.Pass(2);
            pass.u = 0;
            pass.v = 0;
            if (!m_op.Mesh(mesh, pass))
                return false;
        }
        auto pass = m_op.Pass(2);
        pass.blendMesh = 3;
        return m_op.Mesh(3, pass) && End();
    }
    if (map == 4 && (type == 0x17 || type == 0x13 || type == 0x14 || type == 3 || type == 4))
    {
        (void)m_context.random.Next();
        const auto live = m_object.type;
        if (live == 0x17)
            return D(2);
        if (live != 0x13 && live != 0x14 && live != 3 && live != 4)
            return true;
        const glm::vec3 previous = m_op.model->light;
        Light(1, .2f, .1f);
        m_op.model->step.flag88 = (live == 3 || live == 4) ? 1 : 2;
        if (!D(2, 0x5c0))
            return false;
        m_op.model->light = previous;
        m_op.model->step.flag88 = 0xff;
        return D(2);
    }
    if (map == 8 && type == 0x51)
        return Begin(m_object.alpha) && P(0, 2) && P(0, 0x44, 0x5c0) && End();
    if (map >= 11 && map <= 17 && (type == 0x1c || type == 0x1d))
    {
        if (!Begin(m_object.alpha) || !D(2) || !End() || !m_op.Run(m_context.services.AlphaBlend(1)) ||
            !m_op.Run(m_context.services.Color(0, 0, 0, 1)) || !Ground())
            return false;
        const auto blend = m_object.state.blendMesh;
        m_object.state.hiddenMesh = 2;
        if (!Shadow(blend, 2))
            return false;
        m_object.state.hiddenMesh = -1;
        return true;
    }
    if (map == 3 && type == 0x144)
    {
        m_op.model->step.flag88 = 0;
        Light(.8f, .8f, .8f);
        if (!P(0, 0x80))
            return false;
        m_op.model->step.flag88 = 0xff;
        return true;
    }
    if (map == 55 && type == 0x5a)
    {
        White();
        m_object.alpha = 1;
        m_object.state.light = 1;
        m_op.model->step.flag88 = 0;
        if (!P(0, 0x80))
            return false;
        m_op.model->step.flag88 = 0xff;
        return true;
    }
    handled = false;
    return true;
}
bool Drawing::Handlers(bool& handled)
{
    handled = false;
    for (std::size_t i = 0; i < BodyHooks.size(); ++i)
    {
        std::uint32_t argument = 0, known = 0;
        if (i == 2)
        {
            argument = (m_mode & 0xffffff00u) | (m_mode != 0 ? 1u : 0u);
            known = 0xffffffffu;
        }
        if (i == 4)
        {
            argument = m_mode != 0 ? 1u : 0u;
            known = 0xffu;
        }
        if (i == 5)
        {
            argument = m_mode;
            known = 0xffffffffu;
        }
        auto result =
            m_context.services.Handler(BodyHooks[i], m_object, *m_op.model, argument, known, m_context.random);
        if (!result.HasValue())
            return m_op.Run(Core::Unexpected(result.Error()));
        if ((result.Value() & 0xffu) != 0)
        {
            handled = true;
            return true;
        }
    }
    return true;
}
bool Drawing::Types()
{
    const auto type = m_object.type;
    switch (type)
    {
    case 0x17f:
        if (m_mode != 0)
        {
            White();
            return GlowMeshes(3);
        }
        return D(2);
    case 0x17c:
        if (m_object.state4 == 1)
        {
            if (!Begin(m_object.alpha))
                return false;
            const float f = static_cast<float>(std::sin(static_cast<double>(m_context.worldTime) * .002f) * .01f + 1);
            auto pass = m_op.Pass(0x42, 0x6f6);
            pass.blendMesh = 0;
            pass.blendLight = f;
            if (!m_op.Mesh(0, pass))
                return false;
            pass = m_op.Pass(0x42, 0x6f5);
            pass.blendMesh = 1;
            pass.blendLight = f;
            if (!m_op.Mesh(1, pass))
                return false;
            for (int i = 0; i < 2; ++i)
            {
                pass = m_op.Pass(0x42);
                pass.blendMesh = 1;
                pass.blendLight = f;
                if (!m_op.Mesh(2, pass))
                    return false;
            }
            return End();
        }
        return D(2);
    case 0x190:
    {
        if (!D(2))
            return false;
        auto pass = m_op.Pass(2);
        pass.blendMesh = 2;
        pass.blendLight = 1;
        return m_op.Mesh(2, pass);
    }
    case 0x183:
        return Begin(m_object.alpha) && P(0, 2) && End() && m_op.Run(m_context.services.Type183(*m_op.model, m_object));
    case 0x188:
        if (m_mode != 0)
        {
            Light(.1f, .1f, .1f);
            auto pass = m_op.Pass(2);
            pass.hiddenMesh = 1;
            if (!m_op.Body(pass))
                return false;
            Light(1, .1f, .1f);
            return P(1, 2);
        }
        return D(2);
    case 0x180:
        if (m_mode != 0)
        {
            White();
            return P(0, 2, 0x62f) && P(2, 2, 0x631);
        }
        return D(2);
    case 0x181:
        if (m_mode != 0)
        {
            White();
            return P(0, 2, 0x630) && P(1, 2, 0x62f);
        }
        return D(2);
    case 0x182:
        if (m_mode != 0)
        {
            White();
            auto pass = m_op.Pass(2, 0x62e);
            pass.hiddenMesh = 1;
            return m_op.Body(pass);
        }
        return D(2);
    case 0x186:
        if (m_mode == 0)
        {
            m_op.model->step.flag88 = 0;
            Light(.4f, .3f, .5f);
            if (!D(2, 0x624))
                return false;
            m_op.model->step.flag88 = 0xff;
            Light(.5f, .5f, .5f);
            return D(2);
        }
        if (m_mode == 0x12d)
        {
            White();
            return GlowMeshes(2);
        }
        Light(.1f, 1, .8f);
        return D(2);
    case 0x187:
        Light(.9f, .8f, 1);
        if (!D(4, 0x5c1))
            return false;
        White();
        return D(2);
    case 0x185:
    {
        auto pass = m_op.Pass(2);
        pass.hiddenMesh = 1;
        if (!m_op.Body(pass))
            return false;
        pass = m_op.Pass(0x402, m_object.state.hiddenMesh);
        pass.alpha = .5f;
        if (!m_op.Mesh(1, pass))
            return false;
        return P(1, 0x402, m_object.state.hiddenMesh);
    }
    case 0x18a:
        m_op.model->step.flag88 = 0;
        White();
        if (!D(0x42, 0x5c0))
            return false;
        m_op.model->step.flag88 = 0xff;
        Light(.6f, .6f, .6f);
        return D(2);
    case 0x18e:
    {
        if (m_object.animation.action == 6)
            return Death(0x10a);
        const auto value = m_context.services.ProtectedValue(m_object);
        if (!value.HasValue())
            return m_op.Run(Core::Unexpected(value.Error()));
        const auto difference =
            std::bit_cast<std::int32_t>(value.Value() - static_cast<std::uint32_t>(m_object.reference188));
        if (difference < 25)
        {
            if (m_object.animation.action == 6)
                m_object.alive = false;
            return m_op.Run(m_context.services.Sound(0x87, 0, 0)) &&
                   m_op.Run(m_context.services.Transition(*m_op.model, 0x10a));
        }
        if (!D(2))
            return false;
        White();
        if (!D(0x44, 0x5c0))
            return false;
        m_op.model->light.z = 1;
        m_op.model->light.x = .3f;
        m_op.model->light.y = .3f;
        return D(0x48, 0x5c0);
    }
    case 0x18f:
        if (m_object.animation.action == 6)
            return Death(0x10c);
        return D(2);
    default:
        return ExtendedTypes();
    }
}
bool Drawing::Death(std::int32_t target)
{
    m_object.alive = false;
    return m_op.Run(m_context.services.Sound(0x87, 0, 0)) &&
           m_op.Run(m_context.services.Transition(*m_op.model, target));
}
bool Drawing::TransitionTypes()
{
    switch (m_object.type)
    {
    case 0x10a:
    case 0x10b:
        if (m_object.state4 == 2)
        {
            Light(.1f, .3f, .6f);
            return D(0x44, 0x5c0);
        }
        if (m_object.state4 == 3)
        {
            Light(.1f, .6f, .3f);
            return D(0x44, 0x5c0);
        }
        if (!D(2))
            return false;
        White();
        if (!D(0x44, 0x5c0))
            return false;
        m_op.model->light.z = 1;
        m_op.model->light.x = .3f;
        m_op.model->light.y = .3f;
        return D(0x48, 0x5c0);
    case 0x12b:
        if (m_object.state.hiddenMesh == 99 && m_object.seen)
        {
            m_object.state.hiddenMesh = -2;
            return m_op.Run(m_context.services.Transition(*m_op.model, 0x12d));
        }
        White();
        if (!D(2))
            return false;
        if (m_object.state4 > 1)
            return true;
        Light(1, .2f, .1f);
        {
            auto pass = m_op.Pass(0x44);
            pass.blendLight = 1;
            return m_op.Body(pass);
        }
    case 0x12c:
        if (m_object.state.hiddenMesh == 99 && m_object.seen)
        {
            m_object.state.hiddenMesh = -2;
            return m_op.Run(m_context.services.Transition(*m_op.model, 0x12e));
        }
        return D(2);
    case 0x12d:
    case 0x12e:
        if (m_object.state4 == 1)
        {
            White();
            if (!D(2))
                return false;
            Light(1, .2f, .1f);
            auto pass = m_op.Pass(0x44);
            pass.blendLight = 1;
            if (!m_op.Body(pass))
                return false;
            White();
            return true;
        }
        if (m_object.state4 == 2 || m_object.state4 == 3)
        {
            Light(.5f, 1, .3f);
            if (!D(2))
                return false;
            White();
            return true;
        }
        return D(2);
    default:
        return D(2);
    }
}
bool Drawing::ExtendedTypes()
{
    switch (m_object.type)
    {
    case 0x112:
        if (m_object.state4 == 1)
            return D(m_object.renderMask | 2u);
        return D(2);
    case 0xcc:
        if (m_object.state4 == 3 || m_object.state4 == 4)
        {
            m_op.model->light.z = 10;
            m_op.model->light.x = .1f;
            m_op.model->light.y = .1f;
            if (!D(m_object.renderMask | 2u))
                return false;
            White();
            return true;
        }
        return D(2);
    case 0xcb:
        if (m_object.state4 == 2 || m_object.state4 == 3)
        {
            m_op.model->light.z = 1;
            m_op.model->light.x = .5f;
            m_op.model->light.y = .5f;
            if (!D(2, 0x6e9))
                return false;
            White();
            return true;
        }
        return D(2);
    case 0xf9:
    {
        auto pass = m_op.Pass(0x4042);
        pass.blendLight = 1;
        return m_op.Body(pass);
    }
    case 0x11b:
        return FireOutputs();
    case 0x114:
        if (m_object.state4 == 3 || m_object.state4 == 4 || m_object.state4 == 5)
        {
            auto pass = m_op.Pass(2, 0x6ef);
            pass.hiddenMesh = -1;
            return m_op.Body(pass);
        }
        return D(2);
    case 0x140:
        Light(.4f, .6f, 1);
        return D(0x44) && D(0x44);
    case 0x12a:
        White();
        if (!D(2))
            return false;
        Light(1, .5f, 0);
        return D(0x44);
    case 0x138:
        if (m_object.state4 == 0)
        {
            Light(.3f, .6f, 1);
            return D(2) && D(0x44);
        }
        return D(2);
    case 0x139:
    {
        Light(1, .6f, .3f);
        if (!D(2))
            return false;
        auto pass = m_op.Pass(0x44);
        pass.hiddenMesh = 1;
        return m_op.Body(pass);
    }
    case 0x109:
        if (m_object.state4 == 1)
        {
            auto pass = m_op.Pass(0x1040);
            pass.hiddenMesh = 1;
            if (!m_op.Body(pass))
                return false;
        }
        return D(2);
    default:
        return TransitionTypes();
    }
}
bool Drawing::FireOutputs()
{
    Light(1, .6f, .3f);
    if (!D(2))
        return false;
    glm::vec3 input(0);
    if (!m_op.Transform(WorldObjectPoseBone(0x068b187c), input, m_object.output40) ||
        !m_op.Transform(WorldObjectPoseBone(0x068b18ac), input, m_object.output4c))
        return false;
    glm::vec3 light(1, 0, 0);
    if (!m_op.Sprite(0x5ac, m_object.output40, 1, light) || !m_op.Sprite(0x5ac, m_object.output4c, 1, light))
        return false;
    light = glm::vec3(.5f);
    const float first = static_cast<float>(m_context.random.Next() % 360);
    if (!m_op.Sprite(0x5fd, m_object.output40, .5f, light, first))
        return false;
    const float second = static_cast<float>(m_context.random.Next() % 360);
    return m_op.Sprite(0x5fd, m_object.output4c, .5f, light, second);
}
bool Drawing::Continuation()
{
    if ((m_object.renderFlags & 2u) != 0)
    {
        auto pass = m_op.Pass(2);
        pass.blendMesh = -2;
        pass.blendLight = 1;
        if (!m_op.Body(pass))
            return false;
    }
    if (m_object.type == 0x177)
    {
        if (!Begin(m_object.alpha))
            return false;
        const float f = static_cast<float>(std::sin(static_cast<double>(m_context.worldTime) * .002f) * .3f + .5f);
        auto pass = m_op.Pass(2, m_op.model->meshMaterials[0]);
        pass.blendMesh = 0;
        pass.blendLight = f;
        if (!m_op.Mesh(0, pass))
            return false;
        std::int32_t time = 0;
        if (!m_op.Ftol(m_context.worldTime, time))
            return false;
        pass = m_op.Pass(0x44);
        pass.blendMesh = 3;
        pass.blendLight = f;
        pass.v = static_cast<float>(-static_cast<double>(time % 100) * .01f);
        if (!m_op.Mesh(3, pass) || !End())
            return false;
    }
    const auto type = m_object.type;
    if (type == 0x1f4 || type == 0x1f3)
    {
        auto pass = m_op.Pass(0x42);
        pass.u = 0;
        pass.v = 0;
        if (!m_op.Mesh(0, pass))
            return false;
    }
    if (m_object.type == 0x111)
    {
        glm::vec3 light(1, .6f, .4f), input(0), pos;
        if (!m_op.Transform(WorldObjectPoseBone(0x068b184c), input, pos))
            return false;
        float wave = static_cast<float>(std::sin(static_cast<double>(m_context.worldTime) * .002f));
        float angle = static_cast<float>(m_context.random.Next() % 360);
        if (!m_op.Sprite(0x5fd, pos, static_cast<float>(static_cast<double>(wave) * .3f + 1.3f), light, angle) ||
            !m_op.Sprite(0x5ac, pos, 1.5f, light, 90))
            return false;
        wave = static_cast<float>(std::sin(static_cast<double>(m_context.worldTime) * .002f));
        angle = static_cast<float>(m_context.random.Next() % 360);
        if (!m_op.Sprite(0x5fd, pos, static_cast<float>(static_cast<double>(wave) * .3f + 1.3f), light, angle) ||
            !m_op.Particle(0x5c5, pos, light, 0, 1))
            return false;
    }
    const auto last = m_object.type;
    if (last == 0xed || last == 0xee)
        return ObjectFire();
    return true;
}
bool Drawing::ObjectFire()
{
    White();
    auto pass = m_op.Pass(0x42);
    pass.blendMesh = -2;
    pass.blendLight = 1;
    if (!m_op.Body(pass))
        return false;
    auto current = m_context.services.Model(m_object.type);
    if (!current.HasValue())
        return m_op.Run(Core::Unexpected(current.Error()));
    m_op.model = &current.Value().get();
    m_object.light = glm::vec3(1);
    glm::vec3 input(0, 1.5f, 0), pos;
    if (!m_op.Transform(WorldObjectPoseBone(0x068b187c), input, pos))
        return false;
    for (int i = 0; i < 2; ++i)
        if (!m_op.Particle(0x5da, m_object.transform.position, m_object.light, 8,
                           static_cast<float>(static_cast<double>(m_object.transform.scale) - .4f), m_object.identity))
            return false;
    if (!m_op.Particle(0x5f2, m_object.transform.position, m_object.light, 0x26, m_object.transform.scale,
                       m_object.identity))
        return false;
    m_object.light = glm::vec3(1, .4f, 0);
    if (!m_op.Particle(0x5da, m_object.transform.position, m_object.light, 9,
                       static_cast<float>(static_cast<double>(m_object.transform.scale) - .4f), m_object.identity))
        return false;
    for (int k = 0; k < 10; ++k)
    {
        const float f = static_cast<float>(k);
        m_object.light.z = 0;
        m_object.light.x = static_cast<float>(1 - static_cast<double>(f) * .1f);
        m_object.light.y = static_cast<float>(static_cast<double>(.4f) - static_cast<double>(f) * .04f);
        input = glm::vec3(static_cast<float>(static_cast<double>(f) * 15), 1.5f, 0);
        if (!m_op.Transform(WorldObjectPoseBone(0x068b184c), input, pos) ||
            !m_op.Sprite(0x5c6, pos, static_cast<float>(static_cast<double>(f) * .9f + 3), m_object.light))
            return false;
    }
    return true;
}
bool Drawing::Special()
{
    const float q = static_cast<float>(std::sin(static_cast<double>(m_context.worldTime) * .002f) * .2f);
    if (!Begin(1))
        return false;
    White();
    m_op.model->step.flag88 = 0;
    if (!P(0, 2) || !P(1, 2))
        return false;
    glm::vec3 input, pos, light;
    if (m_object.animation.action == 4)
    {
        White();
        if (!P(1, 0x46, 0x68f))
            return false;
        light = glm::vec3(1, 0, 0);
        input.x = static_cast<float>(static_cast<double>(m_context.random.Next() % 10 - 10) * .5);
        input.y = 0;
        input.z = static_cast<float>(static_cast<double>(m_context.random.Next() % 40 - 20) * .5);
        if (!m_op.Transform(WorldObjectPoseBone(0x068b1abc), input, pos) ||
            !m_op.Particle(0x5c6, pos, light, 15, static_cast<float>(static_cast<double>(q) * .05f + .7f)))
            return false;
    }
    m_op.model->step.flag88 = 0xff;
    if (!End())
        return false;
    if (!SuppressShadow() && (!PrepareShadow(.7f) || !Shadow()))
        return false;
    light = glm::vec3(static_cast<float>(static_cast<double>(q) + .9f),
                      static_cast<float>(static_cast<double>(q) * .5 + .2f),
                      static_cast<float>(static_cast<double>(q) * .5 + .1f));
    const float scale = static_cast<float>(static_cast<double>(q) * .1f + .5f);
    input = glm::vec3(50, 2, 11);
    if (!m_op.Transform(WorldObjectPoseBone(0x068b1a2c), input, pos) || !m_op.Sprite(0x5ac, pos, scale, light) ||
        !m_op.Sprite(0x5ac, pos, scale, light))
        return false;
    input = glm::vec3(50, 2, -11);
    if (!m_op.Transform(WorldObjectPoseBone(0x068b1a2c), input, pos) || !m_op.Sprite(0x5ac, pos, scale, light) ||
        !m_op.Sprite(0x5ac, pos, scale, light))
        return false;
    light = glm::vec3(1, .3f, .2f);
    input = glm::vec3(40, 15, 0);
    if (!m_op.Transform(WorldObjectPoseBone(0x068b1a8c), input, pos) || !m_op.Sprite(0x5ac, pos, 1.5f, light) ||
        !m_op.Sprite(0x5ac, pos, 1, light))
        return false;
    const auto liveType = m_object.type;
    std::int32_t subtype = 3;
    if (liveType == 0x14b)
    {
        light = glm::vec3(.8f, 0, 0);
        subtype = 1;
    }
    else if (liveType == 0x14c)
    {
        light = glm::vec3(.1f, .1f, .8f);
        subtype = 2;
    }
    else
        light = glm::vec3(1, 1, .2f);
    if (!m_op.Effect(0x14d, m_object.transform.position, light, 0, m_object.identity) ||
        !m_op.Effect(0x14d, m_object.transform.position, light, 0, m_object.identity))
        return false;
    return SpecialJoints(subtype);
}
bool Drawing::SpecialJoints(std::int32_t subtype)
{
    std::size_t first = 0, count = 0;
    if (m_object.animation.action == 1)
    {
        if (!(m_object.animation.frame >= 0))
            return true;
        if (m_object.animation.frame > 1.5f)
            return true;
        count = 4;
    }
    else if (m_object.animation.action == 2)
    {
        if (m_object.animation.frame > 1.0f && !(m_object.animation.frame > 1.4f))
        {
            first = 0;
            count = 2;
        }
        else if (m_object.animation.frame > 4.8f && !(m_object.animation.frame > 5.2f))
        {
            first = 2;
            count = 2;
        }
    }
    glm::vec3 light(1), pos;
    constexpr std::array<std::uint32_t, 4> Addresses{0x068b1c3c, 0x068b1d5c, 0x068b1edc, 0x068b205c};
    for (std::size_t i = first; i < first + count; ++i)
    {
        glm::vec3 input(0);
        if (!m_op.Transform(WorldObjectPoseBone(Addresses[i]), input, pos) ||
            !m_op.Effect(0x14e, pos, light, subtype, m_object.identity))
            return false;
    }
    return true;
}
} // namespace
Core::Expected<void, WorldObjectDrawingError> DrawWorldObject(WorldObject& object, WorldObjectDrawingContext& context,
                                                              std::uint32_t mode)
{
    auto model = context.services.Model(object.type);
    if (!model.HasValue())
        return Core::Unexpected(model.Error());
    WorldObjectDrawingOperations operations(object, context, model.Value().get());
    Drawing drawing(operations, mode);
    (void)drawing.Execute();
    return operations.ResultOf();
}
} // namespace Client::World
