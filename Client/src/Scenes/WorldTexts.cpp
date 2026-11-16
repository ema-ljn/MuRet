#include "Scenes/WorldTexts.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/GameProjection.h"
#include "Scenes/HudText.h"
#include "Scenes/WorldRuntime.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// 0x438434: la z de la entrada más 370 (0x6b8b88).
constexpr double AboveThePosition = 370.0;
// 0x4384a3..0x4384b0: el fondo y el texto.
constexpr Render::Text::TextColors WorldTextColors{.text = 0xffc8e6ffu, .background = 0xff000064u};
constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// strlen sobre los 64 bytes de la entrada: 0x4381f0 copia los 64 que le pasan, con su '\0' adentro.
std::string_view TextOf(const WorldText& entry)
{
    const void* end = std::memchr(entry.text.data(), 0, entry.text.size());
    const std::size_t length = end != nullptr
                                   ? static_cast<std::size_t>(static_cast<const char*>(end) - entry.text.data())
                                   : entry.text.size();
    return {entry.text.data(), length};
}

Core::Expected<void, std::string> DrawOne(WorldTextDrawContext& context, const WorldText& entry)
{
    if (context.projection == nullptr)
        return Core::Unexpected(std::string("0x438455: un texto 3D sin la proyección del pase del mundo (0x6203c0)"));
    // 0x438434..0x43843d: fld de la z, fadd del float y fstp a float.
    const glm::vec3 point{entry.position.x, entry.position.y,
                          static_cast<float>(static_cast<double>(entry.position.z) + AboveThePosition)};
    const auto projected = ProjectGamePoint(*context.projection, point);
    if (!projected.HasValue())
        return Core::Unexpected(projected.Error());
    // 0x438460..0x438469: con signo, las dos.
    if (projected.Value().x < 0 || projected.Value().y < 0)
        return {};
    // 0x43846b..0x438494: SelectObject con la negrita; el GetTextExtentPointA de acá no se usa.
    context.hud.dcFont = Render::Text::FontKind::Bold;
    context.hud.textColors = WorldTextColors;
    DrawHudCentered(context.text, context.sprites.Screen(), context.hud, projected.Value().x, projected.Value().y,
                    TextOf(entry));
    return {};
}
} // namespace

void QueueWorldText(WorldTextQueue& queue, std::string_view text, const glm::vec3& position)
{
    WorldText entry;
    std::memcpy(entry.text.data(), text.data(), std::min(text.size(), entry.text.size()));
    entry.position = position;
    queue.push_back(entry);
}

Core::Expected<void, std::string> DrawTheWorldTexts(WorldTextDrawContext& context)
{
    // 0x4383f6..0x438405.
    context.sprites.SetColor(White);
    if (context.projecting)
    {
        for (const WorldText& entry : context.queue)
        {
            if (auto drawn = DrawOne(context, entry); !drawn.HasValue())
                return drawn;
        }
        return {};
    }
    // 0x43840b..0x438584: la de adelante y pop_front, hasta vaciarla (0x69eff0 suelta cada bloque al pasarlo).
    while (!context.queue.empty())
    {
        const WorldText entry = context.queue.front();
        context.queue.pop_front();
        if (auto drawn = DrawOne(context, entry); !drawn.HasValue())
            return drawn;
    }
    return {};
}
} // namespace Client::Scenes
