#pragma once

#include "Data/Models/Model.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "EngineApi.h"
#include "Render/TextureSlot.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Model texture slots as 0x60d530, 0x60d550 and 0x60d5f0 hand them out
// (Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario"): as data, without GL, to compare with
// the traces and with lor_slots.bin, which name slots (question 13 of the CL6 plan). Which scene starts at which slot
// belongs to whoever builds it (CL6.3).
namespace Client::Models
{
struct TextureRequestNames
{
    std::string requested;
    std::string previous;
};

struct ModelTextureRequests
{
    std::vector<TextureRequestNames> names;
    // The last request of this load for each slot, even if the same name is used again.
    std::map<std::uint16_t, std::size_t> lastBySlot;
};

class ENGINE_API ModelTextureSlots
{
public:
    // 0x60d530: the next free slot in `next`; the name search starts at 0.
    explicit ModelTextureSlots(Render::TextureSlot next);

    // ToLoad's paths point to the ones it stores: a copy would point to the other's. Moved, they stay valid, because a
    // moved std::deque does not move its elements.
    ModelTextureSlots(const ModelTextureSlots&) = delete;
    ModelTextureSlots& operator=(const ModelTextureSlots&) = delete;
    ModelTextureSlots(ModelTextureSlots&&) = default;
    ModelTextureSlots& operator=(ModelTextureSlots&&) = default;
    ~ModelTextureSlots() = default;

    // A table with the same names and the next free slot at `next`, with nothing to upload: the one of a later loader
    // that continues with the global table (0x61ca30 with 0x60d530(0x6f8)). This one does not change.
    [[nodiscard]] ModelTextureSlots ContinuedAt(Render::TextureSlot next) const;

    // 0x60d530 again, like each loader of the original: moves the next free slot and keeps what already has a name. It
    // can go backwards (0x615700 goes back to 0x138, Q-C89).
    void SetNext(Render::TextureSlot next);

    // 0x60d5f0: the slot of each mesh of `model`, in order. Each texture name is looked up, without the folder and
    // case-sensitively (Q-C90), in slots 0 to the next free one: if found, the mesh uses it; otherwise `folder` + the
    // name is requested at the next one, which advances even if the file is missing. Then skin and hair go to their
    // fixed slot (Data::Models::SkinSlotOf), and the requested one stays taken. `sampling` is the OZJ one: OZTs always
    // use GL_NEAREST.
    std::vector<Render::TextureSlot> Assign(const Data::Models::Model& model, std::string_view folder,
                                            Data::Textures::Sampling sampling);
    // Keeps the link between each mesh and its request. Skin and already resident names have no request.
    std::vector<Render::TextureSlot> Assign(const Data::Models::Model& model, std::string_view folder,
                                            Data::Textures::Sampling sampling, ModelTextureRequests* requests,
                                            std::vector<std::optional<std::size_t>>* bindings);

    // What Assign requested and must be uploaded, in order, with each one's slot and sampling
    // (Data::Textures::LoadTextureSet): the folder, the name up to the first dot and the extension of the original's
    // loader (Data::Models::ModelTextureLoaderOf), so it opens the same file.
    [[nodiscard]] std::span<const Data::Textures::TextureSetEntry> ToLoad() const;

    [[nodiscard]] Render::TextureSlot Next() const;

    // The bare name `slot` holds, like the table at 0x7d3a198: the one of the last mesh that requested it, or empty if
    // nobody did or if it is outside the table. For comparing with a dump.
    [[nodiscard]] std::string_view NameOf(Render::TextureSlot slot) const;
    // 63c150 clears the name when the last physical reference is released.
    void SetName(Render::TextureSlot slot, std::string_view name);

private:
    std::optional<Render::TextureSlot> Find(std::string_view name) const;
    Render::TextureSlot Request(std::string_view folder, std::string_view name, Data::Textures::Sampling sampling);

    // The bare name each slot holds, like the table at 0x7d3a198; empty, no name.
    std::vector<std::string> m_names;
    std::size_t m_next = 0;
    // ToLoad's paths: a std::deque does not move what it already stored, so the string_views of m_toLoad stay valid.
    std::deque<std::string> m_paths;
    std::vector<Data::Textures::TextureSetEntry> m_toLoad;
};
} // namespace Client::Models
