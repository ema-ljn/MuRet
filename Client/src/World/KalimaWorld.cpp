#include "World/KalimaWorld.h"

#include "Core/Expected.h"
#include "World/KalimaWaves.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "World/FaceNormal.h"
#include "World/OriginalFtol.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Client::World
{
namespace
{
constexpr std::size_t MeshSide = 42;
constexpr std::size_t MeshVertexCount = MeshSide * MeshSide;
constexpr std::size_t MeshIndexCount = (MeshSide - 1) * (MeshSide - 1) * 6;
constexpr std::int32_t WaveSide = static_cast<std::int32_t>(KalimaWaveSide);
constexpr float BaseHeight = 350;
constexpr float CellScale = 50;
constexpr std::int32_t DisturbInterval = 40;
constexpr std::int32_t PeriodicRadius = 50;
// Constantes de41d2d0/41d700/41d880/41cf00, verificadas en la imagen estática.
constexpr int MeshOffsetX = 25;
constexpr int MeshOffsetY = 15;
constexpr double DisturbanceScale = 1024;
constexpr unsigned DisturbanceShift = 19;
constexpr int DisturbanceRandomSpan = 30;
constexpr int DisturbanceRadiusX = 20;
constexpr int DisturbanceRadiusY = 2;
constexpr int DisturbanceAmplitude = 2000;
constexpr float FirstTimeRate = 0.005f;
constexpr float FirstCounterRate = 0.003f;
constexpr float SecondTimeRate = 0.001f;
constexpr float SecondCounterRate = 0.002f;
constexpr float FirstSpatialRate = 0.1f;
constexpr float SecondSpatialRate = 0.5f;
constexpr float SecondRowRate = 0.3f;
constexpr double FirstPeriodicAmplitude = 50;
constexpr double SecondPeriodicAmplitude = 25;
constexpr float UvOffset = 0.1f;
constexpr double GreenLightMultiplier = 2.5;
constexpr double BlueLightMultiplier = 3;
constexpr Render::Color WaterColor{0.2f, 0.5f, 0.65f, 1};
constexpr std::uint16_t WaterTexture = 0x23;
constexpr std::uint16_t HighlightTexture = 0x24;

std::int32_t TwicePlus(std::int32_t coordinate, std::int32_t offset)
{
    const auto bits = std::bit_cast<std::uint32_t>(coordinate) * 2u + std::bit_cast<std::uint32_t>(offset);
    return std::bit_cast<std::int32_t>(bits);
}

Core::Expected<std::int32_t, std::string>
ReadHeroCoordinate(const std::function<Core::Expected<std::int32_t, std::string>()>& read, std::uint8_t legacy)
{
    if (read)
        return read();
    return static_cast<std::int32_t>(legacy);
}

float Spill(double value)
{
    return static_cast<float>(value);
}

std::int32_t Truncate(double value)
{
    // Los productores internos sólo pasan valores finitos acotados por los senos/cosenos.
    return OriginalFtolLow(value).value();
}

float MeshHeight(std::int32_t wave, std::int32_t first, std::int32_t second)
{
    // 41d361..41d398: no redondea entre las dos medias.
    return Spill((((static_cast<double>(first) + BaseHeight) + second + BaseHeight) * 0.5 + wave + BaseHeight) * 0.5);
}

glm::vec3 Normalize(const glm::vec3& value)
{
    const double x = value.x;
    const double y = value.y;
    const double z = value.z;
    const double length = std::sqrt((x * x + y * y) + z * z);
    return {Spill(x / length), Spill(y / length), Spill(z / length)};
}
} // namespace

struct KalimaWorld::Storage
{
    std::int32_t map = 0;
    KalimaWaveState waves{};
    std::array<std::array<std::int32_t, KalimaWaveCellCount>, 2> periodic{};
    std::array<glm::vec3, MeshVertexCount> vertices{};
    std::array<glm::vec3, MeshVertexCount> normals{};
    std::array<std::uint32_t, MeshVertexCount> normalCounts{};
    std::array<std::size_t, MeshIndexCount> indices{};

    void Perturb(std::int32_t centerX, std::int32_t centerY, std::int32_t radiusX, std::int32_t radiusY,
                 std::int32_t amplitude, Render::Models::CrtRandom& random)
    {
        if (centerX < 0)
            centerX = random.Next() % (WaveSide - 1 - radiusX * 2) + radiusX + 1;
        if (centerY < 0)
            centerY = random.Next() % (WaveSide - 1 - radiusY * 2) + radiusY + 1;
        const int startX = std::max(-radiusX, 1 - centerX);
        const int startY = std::max(-radiusY, 1 - centerY);
        const int endX = std::min(radiusX, WaveSide - 1 - centerX);
        const int endY = std::min(radiusY, WaveSide - 1 - centerY);
        const double scale = DisturbanceScale / radiusX;
        const float squaredScale = Spill(scale * scale);
        auto& destination = waves.planes[waves.selected ? 1u : 0u];
        for (int y = startY; y < endY; ++y)
        {
            for (int x = startX; x < endX; ++x)
            {
                const int distance = x * x + y * y;
                if (distance >= radiusX * radiusY)
                    continue;
                const double phase = std::sqrt(static_cast<double>(distance) * squaredScale);
                const auto increment = Truncate((std::cos(phase) + 1.0) * amplitude) >> DisturbanceShift;
                const auto cell = static_cast<std::size_t>((centerY + y) * WaveSide + centerX + x);
                destination[cell] += std::bit_cast<std::uint32_t>(increment);
            }
        }
    }

    void UpdatePeriodic(const KalimaFrameContext& context, std::int32_t heroX, std::int32_t heroY)
    {
        const int startX = std::max(0, TwicePlus(heroX, -PeriodicRadius));
        const int startY = std::max(0, TwicePlus(heroY, -PeriodicRadius));
        const int endX = std::min(WaveSide, TwicePlus(heroX, PeriodicRadius));
        const int endY = std::min(WaveSide, TwicePlus(heroY, PeriodicRadius));
        for (int y = startY; y < endY; ++y)
        {
            const float yFirst = Spill(static_cast<double>(y) * FirstSpatialRate);
            const float ySecond = Spill(static_cast<double>(y) * SecondSpatialRate);
            const double yThird = static_cast<double>(y) * SecondRowRate;
            for (int x = startX; x < endX; ++x)
            {
                const double xFirst = static_cast<double>(x) * FirstSpatialRate;
                const auto first =
                    Truncate(std::sin((static_cast<double>(context.worldTime) * FirstTimeRate + yFirst) + xFirst) *
                             FirstPeriodicAmplitude);
                const auto cell = static_cast<std::size_t>(y * WaveSide + x);
                periodic[0][cell] =
                    Truncate(static_cast<double>(first) -
                             std::sin((static_cast<double>(context.worldTime) * FirstCounterRate + ySecond) + xFirst) *
                                 FirstPeriodicAmplitude);
                const auto second = Truncate(std::sin((static_cast<double>(x) * SecondSpatialRate +
                                                       static_cast<double>(context.worldTime) * SecondTimeRate) +
                                                      ySecond) *
                                             SecondPeriodicAmplitude);
                periodic[1][cell] =
                    Truncate(static_cast<double>(second) -
                             std::sin((static_cast<double>(context.worldTime) * SecondCounterRate + yThird) + x) *
                                 SecondPeriodicAmplitude);
            }
        }
    }

    void BuildVertices(std::uint8_t heroX, std::uint8_t heroY)
    {
        const auto& plane = waves.planes[waves.selected ? 1u : 0u];
        for (std::size_t row = 0; row < MeshSide; ++row)
        {
            const int y = heroY * 2 - MeshOffsetY + static_cast<int>(row);
            for (std::size_t column = 0; column < MeshSide; ++column)
            {
                const int x = heroX * 2 - MeshOffsetX + static_cast<int>(column);
                float height = BaseHeight;
                if (x >= 0 && y >= 0 && x < WaveSide && y < WaveSide)
                {
                    const auto cell = static_cast<std::size_t>(y * WaveSide + x);
                    height = MeshHeight(std::bit_cast<std::int32_t>(plane[cell]), periodic[0][cell], periodic[1][cell]);
                }
                vertices[row * MeshSide + column] = {static_cast<float>(x) * CellScale,
                                                     static_cast<float>(y) * CellScale, height};
            }
        }
    }

    void BuildNormals()
    {
        normals.fill(glm::vec3{});
        normalCounts.fill(0);
        for (std::size_t triangle = 0; triangle < MeshIndexCount; triangle += 3)
        {
            const glm::vec3 normal = FaceNormal(vertices[indices[triangle]], vertices[indices[triangle + 1]],
                                                vertices[indices[triangle + 2]]);
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const auto index = indices[triangle + corner];
                normals[index].x = Spill(static_cast<double>(normals[index].x) + normal.x);
                normals[index].y = Spill(static_cast<double>(normals[index].y) + normal.y);
                normals[index].z = Spill(static_cast<double>(normals[index].z) + normal.z);
                ++normalCounts[index];
            }
        }
        for (std::size_t index = 0; index < MeshVertexCount; ++index)
        {
            const double count = normalCounts[index];
            normals[index] = Normalize(
                {Spill(normals[index].x / count), Spill(normals[index].y / count), Spill(normals[index].z / count)});
        }
    }

    void Emit(Render::ModelBatch& batch, bool highlight) const
    {
        Render::ModelDrawState state = batch.State();
        state.texture = Render::TextureSlot{highlight ? HighlightTexture : WaterTexture};
        state.rawTextureName.reset();
        state.blend = highlight ? Render::ModelBlend::Additive : Render::ModelBlend::Alpha;
        state.cullEnabled = false;
        state.depthWrite = !highlight;
        state.alphaTest = !highlight;
        state.texture2d = true;
        batch.ApplyState(state);
        for (std::size_t triangle = 0; triangle < MeshIndexCount; triangle += 3)
        {
            std::array<Render::ModelVertex, 3> corners;
            for (std::size_t corner = 0; corner < corners.size(); ++corner)
            {
                const auto index = indices[triangle + corner];
                const auto& normal = normals[index];
                const glm::vec2 uv{Spill(static_cast<double>(normal.y) * 0.5f + UvOffset),
                                   Spill((static_cast<double>(normal.z) + 1.0f) * 0.5f)};
                const double retainedLight = 1.0 - ((static_cast<double>(normal.x) + normal.z) - normal.y);
                // 41d257: FST guarda una copia float sin sacar la luz retenida para el azul.
                const float light = Spill(retainedLight);
                const Render::Color color = highlight ? Render::Color{light, Spill(light * GreenLightMultiplier),
                                                                      Spill(retainedLight * BlueLightMultiplier), 1}
                                                      : WaterColor;
                corners[corner] = {vertices[index], uv, color};
            }
            batch.Triangle(corners);
        }
    }
};

bool IsKalimaMap(std::int32_t map)
{
    constexpr int FirstMap = 24;
    constexpr int LastMap = 29;
    constexpr int SeventhMap = 36;
    return (map >= FirstMap && map <= LastMap) || map == SeventhMap;
}

KalimaWorld::KalimaWorld(std::int32_t map) : m_storage(std::make_unique<Storage>())
{
    m_storage->map = map;
    std::size_t cursor = 0;
    for (std::size_t row = 0; row < MeshSide - 1; ++row)
    {
        for (std::size_t column = 0; column < MeshSide - 1; ++column)
        {
            const std::size_t topLeft = row * MeshSide + column;
            const std::size_t bottomRight = topLeft + MeshSide + 1;
            const std::array<std::size_t, 6> quad =
                row % 2 == column % 2 ? std::array<std::size_t, 6>{topLeft, topLeft + 1, bottomRight,
                                                                   topLeft, bottomRight, bottomRight - 1}
                                      : std::array<std::size_t, 6>{topLeft,     topLeft + 1, bottomRight - 1,
                                                                   topLeft + 1, bottomRight, bottomRight - 1};
            for (const auto index : quad)
                m_storage->indices[cursor++] = index;
        }
    }
}

KalimaWorld::~KalimaWorld() = default;

std::unique_ptr<KalimaWorld> KalimaWorld::CloneForFrame() const
{
    auto clone = std::make_unique<KalimaWorld>(m_storage->map);
    *clone->m_storage = *m_storage;
    return clone;
}

Core::Expected<void, std::string> KalimaWorld::Update(const KalimaFrameContext& context)
{
    if (!IsKalimaMap(m_storage->map) || !std::isfinite(context.worldTime))
        return Core::Unexpected(std::string("0x41cca0: mapa o tiempo fuera del contrato portable"));
    if (context.logicSteps % DisturbInterval == 0)
    {
        const auto heroX = ReadHeroCoordinate(context.readHeroX, context.heroTileX);
        if (!heroX.HasValue())
            return Core::Unexpected(heroX.Error());
        const auto offset = context.random.Next() % DisturbanceRandomSpan - MeshOffsetY;
        const auto heroY = ReadHeroCoordinate(context.readHeroY, context.heroTileY);
        if (!heroY.HasValue())
            return Core::Unexpected(heroY.Error());
        m_storage->Perturb(TwicePlus(heroX.Value(), offset), TwicePlus(heroY.Value(), MeshOffsetX), DisturbanceRadiusX,
                           DisturbanceRadiusY, DisturbanceAmplitude, context.random);
    }
    m_storage->waves.selected = !m_storage->waves.selected;
    UpdateKalimaWaves(m_storage->waves);
    const auto heroX = ReadHeroCoordinate(context.readHeroX, context.heroTileX);
    if (!heroX.HasValue())
        return Core::Unexpected(heroX.Error());
    const auto heroY = ReadHeroCoordinate(context.readHeroY, context.heroTileY);
    if (!heroY.HasValue())
        return Core::Unexpected(heroY.Error());
    m_storage->UpdatePeriodic(context, heroX.Value(), heroY.Value());
    return {};
}

Core::Expected<void, std::string> KalimaWorld::Draw(Render::ModelBatch& batch, std::uint8_t heroTileX,
                                                    std::uint8_t heroTileY)
{
    if (!IsKalimaMap(m_storage->map))
        return Core::Unexpected(std::string("0x41cf00: el owner no corresponde a Kalima"));
    m_storage->BuildVertices(heroTileX, heroTileY);
    m_storage->BuildNormals();
    m_storage->Emit(batch, false);
    m_storage->Emit(batch, true);
    return {};
}

Core::Expected<void, std::string> KalimaWorld::Disturb(std::int32_t tileX, std::int32_t tileY, std::int32_t radius,
                                                       std::int32_t amplitude, Render::Models::CrtRandom& random)
{
    if (radius <= 0 || radius > (WaveSide - 2) / 2 || tileX < -WaveSide || tileY < -WaveSide || tileX >= WaveSide ||
        tileY >= WaveSide)
        return Core::Unexpected(std::string("0x41d700: radio o coordenadas fuera del contrato portable"));
    m_storage->Perturb(tileX * 2, tileY * 2, radius, radius, amplitude, random);
    return {};
}

std::optional<float> KalimaWorld::Height(float x, float y) const
{
    if (!std::isfinite(x) || !std::isfinite(y))
        return std::nullopt;
    constexpr float CoordinateScale = 0.02f;
    const double columnValue = static_cast<double>(std::max(x, 0.0f)) * CoordinateScale;
    const double rowValue = static_cast<double>(std::max(y, 0.0f)) * CoordinateScale;
    if (columnValue >= WaveSide || rowValue >= WaveSide)
        return std::nullopt;
    const auto column = OriginalFtolLow(columnValue);
    const auto row = OriginalFtolLow(rowValue);
    if (!column || !row)
        return std::nullopt;
    const auto cell = static_cast<std::size_t>(*row * WaveSide + *column);
    const auto& plane = m_storage->waves.planes[m_storage->waves.selected ? 1u : 0u];
    const double wave = std::bit_cast<std::int32_t>(plane[cell]);
    const double first = m_storage->periodic[0][cell];
    const double second = m_storage->periodic[1][cell];
    // 41dcc6..41dcf0: ningún spill intermedio de la altura geométrica.
    return Spill((wave + BaseHeight + ((first + BaseHeight) + second + BaseHeight) * 0.5) * 0.25);
}

const KalimaWaveState& KalimaWorld::Waves() const
{
    return m_storage->waves;
}

std::int32_t KalimaWorld::Map() const
{
    return m_storage->map;
}
} // namespace Client::World
