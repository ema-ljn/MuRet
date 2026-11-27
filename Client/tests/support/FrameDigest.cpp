#include "support/FrameDigest.h"

#include "Render/ModelBatch.h"
#include "Render/ModelFog.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "UI/ChatLog.h"
#include "UI/PanelRegistry.h"
#include "support/Fnv1a.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingTextDrawer.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int4.hpp>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace Client::Tests
{
namespace
{
// An FNV-1a fed one value at a time. Integers go in little-endian at their own width and floats as their bits, so
// the hash does not depend on the host or on padding.
class Fnv1aWriter
{
public:
    void Add(std::span<const std::byte> bytes)
    {
        for (const std::byte value : bytes)
        {
            AddByte(std::to_integer<std::uint8_t>(value));
        }
    }

    void Add(bool value)
    {
        AddByte(value ? 1 : 0);
    }

    template <std::integral Integer>
        requires(!std::same_as<Integer, bool>)
    void Add(Integer value)
    {
        const auto bits = static_cast<std::make_unsigned_t<Integer>>(value);
        for (std::size_t index = 0; index < sizeof(Integer); ++index)
        {
            AddByte(static_cast<std::uint8_t>(bits >> (8 * index)));
        }
    }

    template <typename Enum>
        requires std::is_enum_v<Enum>
    void Add(Enum value)
    {
        Add(static_cast<std::underlying_type_t<Enum>>(value));
    }

    void Add(float value)
    {
        Add(std::bit_cast<std::uint32_t>(value));
    }

    void Add(std::string_view text)
    {
        Add(static_cast<std::uint64_t>(text.size()));
        Add(std::as_bytes(std::span(text)));
    }

    void Add(const glm::vec2& value)
    {
        Add(value.x);
        Add(value.y);
    }

    void Add(const glm::vec3& value)
    {
        Add(value.x);
        Add(value.y);
        Add(value.z);
    }

    void Add(const glm::ivec4& value)
    {
        for (int index = 0; index < 4; ++index)
        {
            Add(value[index]);
        }
    }

    void Add(const glm::mat4& value)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                Add(value[column][row]);
            }
        }
    }

    template <typename Value> void Add(const std::optional<Value>& value)
    {
        Add(value.has_value());
        if (value)
        {
            Add(*value);
        }
    }

    void Add(Render::TextureSlot slot)
    {
        Add(slot.value);
    }

    void Add(const Render::Color& color)
    {
        Add(color.red);
        Add(color.green);
        Add(color.blue);
        Add(color.alpha);
    }

    void Add(const Render::SpriteVertex& vertex)
    {
        Add(vertex.x);
        Add(vertex.y);
        Add(vertex.u);
        Add(vertex.v);
        Add(vertex.color);
    }

    void Add(const Render::ModelVertex& vertex)
    {
        Add(vertex.position);
        Add(vertex.uv);
        Add(vertex.color);
    }

    void Add(const Render::Text::BoxShape& box)
    {
        Add(box.width);
        Add(box.rows);
        Add(box.prefixColumns);
        Add(box.prefixColor);
    }

    void Add(const RecordedText& text)
    {
        Add(text.x);
        Add(text.y);
        Add(std::string_view{text.text});
        Add(text.style.font);
        Add(text.style.colors.text);
        Add(text.style.colors.background);
        Add(text.clippedAtLeft);
        Add(text.box);
        Add(text.clampY);
        Add(text.windowed);
        Add(text.pixelPosition);
        Add(text.firstColumn);
        Add(text.columns);
    }

    void Add(const Render::Pass3D& pass)
    {
        Add(pass.screen.width);
        Add(pass.screen.height);
        Add(pass.viewport);
        Add(pass.projection);
        Add(pass.view);
        Add(pass.farPlane);
        Add(pass.clearDepth);
    }

    void Add(const Render::ModelFog& fog)
    {
        Add(fog.color);
        Add(fog.start);
        Add(fog.end);
    }

    void Add(const Render::ModelDrawState& state)
    {
        Add(state.texture);
        Add(state.blend);
        Add(state.depthTest);
        Add(state.space);
        Add(state.rawTextureName);
        Add(state.cullEnabled);
        Add(state.depthWrite);
        Add(state.alphaTest);
        Add(state.texture2d);
    }

    void Add(const Render::ModelDrawCommand& command)
    {
        Add(command.state);
        Add(static_cast<std::uint64_t>(command.firstVertex));
        Add(static_cast<std::uint64_t>(command.vertexCount));
        Add(command.operation);
        Add(command.fog);
        Add(command.textureEnv);
    }

    template <typename Value> void AddAll(std::span<const Value> values)
    {
        Add(static_cast<std::uint64_t>(values.size()));
        for (const Value& value : values)
        {
            Add(value);
        }
    }

    [[nodiscard]] std::uint64_t Hash() const
    {
        return m_hash;
    }

private:
    void AddByte(std::uint8_t value)
    {
        m_hash = (m_hash ^ value) * Fnv1aPrime;
    }

    std::uint64_t m_hash = Fnv1aOffsetBasis;
};

void AddEvent(Fnv1aWriter& writer, const FrameEvent& event)
{
    writer.Add(event.kind);
    switch (event.kind)
    {
    case FrameEvent::Kind::Quad:
    case FrameEvent::Kind::Text:
        writer.Add(event.texture);
        writer.Add(event.blend);
        writer.Add(event.alphaTest);
        writer.AddAll(std::span<const Render::SpriteVertex>(event.vertices));
        if (event.kind == FrameEvent::Kind::Text)
        {
            writer.Add(event.text);
        }
        break;
    case FrameEvent::Kind::Flush:
        break;
    case FrameEvent::Kind::World:
        writer.Add(event.world.map);
        writer.Add(event.pass);
        writer.Add(event.alpha);
        break;
    case FrameEvent::Kind::Models:
        writer.Add(event.pass);
        writer.AddAll(std::span<const Render::ModelDrawCommand>(event.modelCommands));
        writer.AddAll(std::span<const Render::ModelVertex>(event.modelVertices));
        break;
    }
}

std::uint64_t DrawOf(const RecordedFrame& frame)
{
    Fnv1aWriter writer;
    writer.Add(static_cast<std::int64_t>(frame.clock.count()));
    writer.Add(frame.presented);
    writer.Add(static_cast<std::uint64_t>(frame.events.size()));
    for (const FrameEvent& event : frame.events)
    {
        AddEvent(writer, event);
    }
    return writer.Hash();
}

std::uint64_t StateOf(const Kernel::WorldState& world)
{
    Fnv1aWriter writer;
    const Kernel::WorldRuntime& runtime = world.Runtime();
    writer.Add(runtime.map);
    const auto actors = world.Actors();
    for (std::size_t index = 0; index < actors.size(); ++index)
    {
        const Kernel::ActorState& actor = actors[index];
        if (!actor.alive)
        {
            continue;
        }
        writer.Add(static_cast<std::uint64_t>(index));
        writer.Add(actor.key);
        writer.Add(actor.type);
        writer.Add(actor.position);
        writer.Add(actor.animation.action);
    }
    const Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    writer.Add(own.life);
    writer.Add(own.mana);
    writer.Add(own.zen);
    const UI::PanelState& panels = runtime.panels;
    for (const std::uint8_t open : {panels.inventory, panels.character, panels.party, panels.warehouse, panels.chaosBox,
                                    panels.personalShop, panels.npcShop, panels.trade, panels.eventNpc})
    {
        writer.Add(open);
    }
    writer.Add(static_cast<std::uint64_t>(runtime.chat.log.entries.size()));
    for (const UI::ChatEntry& entry : runtime.chat.log.entries)
    {
        writer.Add(std::string_view{entry.name});
        writer.Add(std::string_view{entry.text});
        writer.Add(entry.type);
    }
    return writer.Hash();
}
} // namespace

FrameDigest DigestOf(const RecordedFrame& frame, std::span<const std::byte> sentThisFrame, std::uint32_t randState,
                     const Kernel::WorldState& world)
{
    return FrameDigest{
        .draw = DrawOf(frame), .sent = Fnv1a64(sentThisFrame), .randState = randState, .state = StateOf(world)};
}
} // namespace Client::Tests
