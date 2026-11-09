#include "World/TerrainPicking.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "World/FaceNormal.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainPickingSegment.h"
#include "World/TerrainPickingState.h"
#include "World/TerrainPickingTarget.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace Client::World
{
namespace
{
using PickResult = Core::Expected<bool, std::string>;
using PickStatus = Core::Expected<void, std::string>;

// 06 § El rayo contra el terreno: constantes de 0x622430, 0x5fc190, 0x5fc0e0 y 0x59de49.
constexpr float InitialMaxT = std::bit_cast<float>(0x4B18967Fu);
constexpr float CellsPerUnit = std::bit_cast<float>(0x3C23D70Au);
constexpr int BlockSide = 4;
constexpr int LastBlock = 251;
constexpr float LastCell = 254.0f;
constexpr std::size_t GridSide = 256;
constexpr float MaximumLoadedHeight = 765.0f;
constexpr float MaximumFixedHeight = 1201.0f;
constexpr double ProjectionThreshold = 0.5;
constexpr std::uint16_t CursorPassableBit = 0x20;
constexpr std::uint16_t CursorBlockedThreshold = 8;
constexpr std::uint32_t TargetMask = 0xff;
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits == 24);
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53);

float F32(double value)
{
    return static_cast<float>(value);
}

bool Finite(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool CellCoordinate(float value)
{
    return std::isfinite(value) && value >= 0.0f && value <= LastCell;
}

PickStatus Validate(const TerrainPickingState& state, const TerrainView& view, const TerrainRange& range,
                    const TerrainPolygon& polygon, const TerrainPickingSegment& segment)
{
    if (view.heights == nullptr || view.attributes == nullptr)
    {
        return Core::Unexpected(std::string("El picking necesita alturas y atributos del terreno"));
    }
    for (const int bound : {range.firstX, range.lastX, range.firstY, range.lastY})
    {
        if (bound < 0 || bound > LastBlock)
        {
            return Core::Unexpected(std::string("El rango del picking debe contener bloques entre 0 y 251"));
        }
    }
    if (!CellCoordinate(state.selectedX) || !CellCoordinate(state.selectedY) || !Finite(segment.origin) ||
        !Finite(segment.end) || !std::isfinite(view.fixedHeight) || view.fixedHeight < 0.0f ||
        view.fixedHeight > MaximumFixedHeight)
    {
        return Core::Unexpected(std::string("Coordenadas, segmento o altura fija fuera del dominio del picking"));
    }
    for (std::size_t i = 0; i < polygon.x.size(); ++i)
    {
        if (!std::isfinite(polygon.x[i]) || !std::isfinite(polygon.y[i]))
        {
            return Core::Unexpected(std::string("El polígono del picking debe ser finito"));
        }
    }
    return {};
}

// 0x622110 conserva -0; los min/max 0x622130/150 eligen primero/segundo en empate o unordered.
double BranchAbs(float value)
{
    return value < 0.0f || std::isnan(value) ? -static_cast<double>(value) : static_cast<double>(value);
}

double Minimum(float a, float b)
{
    return a > b ? static_cast<double>(b) : static_cast<double>(a);
}

double Maximum(float a, float b)
{
    return a > b ? static_cast<double>(a) : static_cast<double>(b);
}

bool WithinBounds(const TerrainPickingSegment& segment, const glm::vec3& delta, const glm::vec3& point)
{
    const float az = F32(BranchAbs(delta.z));
    const float ay = F32(BranchAbs(delta.y));
    const float ax = F32(BranchAbs(delta.x));
    const float minimum = F32(Minimum(F32(Minimum(ax, ay)), az));
    const double absX = BranchAbs(delta.x);
    int omitted = 0;
    if (absX != minimum && !std::isnan(absX) && !std::isnan(minimum))
    {
        const double absY = BranchAbs(delta.y);
        omitted = absY == minimum || std::isnan(absY) ? 1 : 2;
    }
    // Menor |delta|, prioridad x/y/z; órdenes y,z / z,x / x,y del ASM.
    for (int step = 1; step <= 2; ++step)
    {
        const int axis = (omitted + step) % 3;
        if (Minimum(segment.origin[axis], segment.end[axis]) > point[axis] ||
            !(Maximum(segment.origin[axis], segment.end[axis]) >= point[axis]))
        {
            return false;
        }
    }
    return true;
}

double Edge(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b, int axis)
{
    const auto difference = [&point](const glm::vec3& vertex, int coordinate)
    { return static_cast<double>(vertex[coordinate]) - static_cast<double>(point[coordinate]); };
    if (axis == 0)
    {
        return (difference(a, 2) * difference(b, 1)) - (difference(a, 1) * difference(b, 2));
    }
    if (axis == 1)
    {
        return (difference(b, 2) * difference(a, 0)) - (difference(a, 2) * difference(b, 0));
    }
    return (difference(a, 1) * difference(b, 0)) - (difference(b, 1) * difference(a, 0));
}

bool Inside(const glm::vec3& point, const std::array<glm::vec3, 3>& triangle, const glm::vec3& normal)
{
    // 0x6226dc: prioridad x/y y umbral INCLUSIVO ±0,5, no argmax de la normal.
    const int axis = !(normal.x > -ProjectionThreshold && normal.x < ProjectionThreshold)
                         ? 0
                         : (!(normal.y > -ProjectionThreshold && normal.y < ProjectionThreshold) ? 1 : 2);
    const bool positive = normal[axis] > 0.0f;
    std::size_t previous = triangle.size() - 1;
    for (std::size_t current = 0; current < triangle.size(); ++current)
    {
        const double edge = Edge(point, triangle[previous], triangle[current], axis);
        if (positive ? edge >= 0.0 : !(edge > 0.0))
        {
            return false;
        }
        previous = current;
    }
    return true;
}

bool Face(TerrainPickingState& state, const TerrainPickingSegment& segment, const std::array<glm::vec3, 3>& triangle)
{
    // En el dominio validado los dos anchos son positivos (~100) y nz > 0: FaceNormal tiene su largo.
    const glm::vec3 normal = FaceNormal(triangle[0], triangle[1], triangle[2]);
    const glm::vec3 delta{F32(static_cast<double>(segment.end.x) - static_cast<double>(segment.origin.x)),
                          F32(static_cast<double>(segment.end.y) - static_cast<double>(segment.origin.y)),
                          F32(static_cast<double>(segment.end.z) - static_cast<double>(segment.origin.z))};
    const double denominator = ((static_cast<double>(delta.z) * normal.z) + (static_cast<double>(delta.y) * normal.y)) +
                               (static_cast<double>(delta.x) * normal.x);
    if (denominator >= 0.0)
    {
        return false;
    }
    const double originDot =
        ((static_cast<double>(normal.y) * segment.origin.y) + (static_cast<double>(normal.x) * segment.origin.x)) +
        (static_cast<double>(normal.z) * segment.origin.z);
    const auto& vertex = triangle[0];
    const double vertexDot = ((static_cast<double>(vertex.z) * normal.z) + (static_cast<double>(vertex.y) * normal.y)) +
                             (static_cast<double>(vertex.x) * normal.x);
    const float t = F32(-((originDot - vertexDot) / denominator));
    if (!(t >= 0.0f) || t > state.maxT)
    {
        return false;
    }
    const glm::vec3 point{F32((static_cast<double>(t) * delta.x) + segment.origin.x),
                          F32((static_cast<double>(t) * delta.y) + segment.origin.y),
                          F32((static_cast<double>(t) * delta.z) + segment.origin.z)};
    if (!WithinBounds(segment, delta, point) || !Inside(point, triangle, normal))
    {
        return false;
    }
    state.maxT = t;
    state.hit.x = point.x;
    state.hit.y = point.y;
    state.hit.z = point.z;
    return true;
}

Core::Expected<std::array<glm::vec3, 4>, std::string> Vertices(const TerrainView& view, float xf, float yf, int xi,
                                                               int yi)
{
    const std::size_t first = (static_cast<std::size_t>(yi) * GridSide) + static_cast<std::size_t>(xi);
    const std::array<std::size_t, 4> indices{first, first + 1, first + GridSide + 1, first + GridSide};
    std::array<float, 4> heights{};
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        const float height = view.heights->cells[indices[i]];
        if (!std::isfinite(height) || height < 0.0f || height > MaximumLoadedHeight)
        {
            return Core::Unexpected(std::string("Altura fuera del dominio del terreno cargado en el picking"));
        }
        heights[i] = height;
    }
    // 0x5fac90 conserva X/Y en registros antes de los stores: no sumar 100 desde el float redondeado.
    const double x = static_cast<double>(xf) * static_cast<double>(TerrainCellSize);
    const double y = static_cast<double>(yf) * static_cast<double>(TerrainCellSize);
    std::array<glm::vec3, 4> vertices{{{F32(x), F32(y), heights[0]},
                                       {F32(x + TerrainCellSize), F32(y), heights[1]},
                                       {F32(x + TerrainCellSize), F32(y + TerrainCellSize), heights[2]},
                                       {F32(x), F32(y + TerrainCellSize), heights[3]}}};
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        if ((view.attributes->cells[indices[i]] & FixedHeightBit) != 0)
        {
            vertices[i].z = view.fixedHeight;
        }
    }
    return vertices;
}

PickResult Cell(TerrainPickingState& state, const TerrainView& view, const TerrainPickingSegment& segment, float xf,
                float yf, int xi, int yi)
{
    const auto vertices = Vertices(view, xf, yf, xi, yi);
    if (!vertices.HasValue())
    {
        return Core::Unexpected(vertices.Error());
    }
    const auto& v = vertices.Value();
    // Count=3 en ambos intentos; 023 sólo tras fallo de 012. Hidden0x08 no participa en picking.
    if (!Face(state, segment, {v[0], v[1], v[2]}) && !Face(state, segment, {v[0], v[2], v[3]}))
    {
        return false;
    }
    state.selected = true;
    state.selectedX = xf;
    state.selectedY = yf;
    return true;
}

PickResult Retry(TerrainPickingState& state, const TerrainView& view, const TerrainPickingSegment& segment)
{
    // Hftol convierte Y antes de X; en el dominio acotado, trunc signed64 cabe en los índices.
    const auto yi = static_cast<int>(static_cast<std::int64_t>(state.selectedY));
    const auto xi = static_cast<int>(static_cast<std::int64_t>(state.selectedX));
    return Cell(state, view, segment, state.selectedX, state.selectedY, xi, yi);
}

PickStatus Block(TerrainPickingState& state, const TerrainView& view, const TerrainPolygon& polygon,
                 const TerrainPickingSegment& segment, int blockX, int blockY, float xf, float yf, bool forceVisible)
{
    float y = yf;
    for (int row = 0; row < BlockSide; ++row)
    {
        const float centerY = F32(static_cast<double>(y) + 0.5);
        float x = xf;
        for (int column = 0; column < BlockSide; ++column)
        {
            const float centerX = F32(static_cast<double>(x) + 0.5);
            if (PolygonContains(polygon, centerX, centerY, CellMargin) || forceVisible)
            {
                const auto result = Cell(state, view, segment, x, y, blockX + column, blockY + row);
                if (!result.HasValue())
                {
                    return Core::Unexpected(result.Error());
                }
            }
            x = F32(static_cast<double>(x) + 1.0);
        }
        y = F32(static_cast<double>(y) + 1.0);
    }
    return {};
}

PickStatus Pass(TerrainPickingState& state, const TerrainView& view, const TerrainRange& range,
                const TerrainPolygon& polygon, const TerrainPickingSegment& segment, bool forceVisible)
{
    state.selected = false;
    state.maxT = InitialMaxT;
    float y = static_cast<float>(range.firstY);
    for (int blockY = range.firstY; blockY <= range.lastY; blockY += BlockSide)
    {
        const float centerY = F32(static_cast<double>(y) + 2.0);
        float x = static_cast<float>(range.firstX);
        for (int blockX = range.firstX; blockX <= range.lastX; blockX += BlockSide)
        {
            const float centerX = F32(static_cast<double>(x) + 2.0);
            if (PolygonContains(polygon, centerX, centerY, BlockMargin) || forceVisible)
            {
                const auto result = Block(state, view, polygon, segment, blockX, blockY, x, y, forceVisible);
                if (!result.HasValue())
                {
                    return result;
                }
            }
            x = F32(static_cast<double>(x) + BlockSide);
        }
        y = F32(static_cast<double>(y) + BlockSide);
    }
    if (state.selected)
    {
        const auto retry = Retry(state, view, segment);
        if (!retry.HasValue())
        {
            return Core::Unexpected(retry.Error());
        }
    }
    return {};
}

PickStatus PublishTarget(TerrainPickingTarget& target, const TerrainPickingState& state, const TerrainView& view)
{
    // Fuera del rayo del terreno cargado, no convertir un NaN/overflow a entero C++ (UB), ni
    // atribuir al original un retorno ordinario de FISTP con excepciones cuyo mask no se comprobó.
    constexpr double Integer64Limit = 0x1p63;
    const auto fits = [](double product)
    { return std::isfinite(product) && product < Integer64Limit && product >= -Integer64Limit; };
    const double xProduct = static_cast<double>(state.hit.x) * static_cast<double>(CellsPerUnit);
    if (!fits(xProduct))
    {
        return Core::Unexpected(std::string("El punto del picking no admite la conversión signed64 del target"));
    }
    const auto x = static_cast<std::int64_t>(xProduct);
    const double yProduct = static_cast<double>(state.hit.y) * static_cast<double>(CellsPerUnit);
    if (!fits(yProduct))
    {
        return Core::Unexpected(std::string("El punto del picking no admite la conversión signed64 del target"));
    }
    target.x = static_cast<std::uint32_t>(x) & TargetMask;
    const auto y = static_cast<std::int64_t>(yProduct);
    target.y = static_cast<std::uint32_t>(y) & TargetMask;
    const std::uint16_t attribute = view.attributes->cells[(static_cast<std::size_t>(target.y) * GridSide) + target.x];
    if (attribute < CursorBlockedThreshold || (attribute & CursorPassableBit) != 0)
    {
        target.dontMove = false;
        return {};
    }
    target.dontMove = true;
    if ((attribute & FixedHeightBit) != 0)
    {
        target.dontMove = false;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> PickTerrain(TerrainPickingState& state, const TerrainView& view,
                                              const TerrainRange& range, const TerrainPolygon& polygon,
                                              const TerrainPickingSegment& segment, bool forceVisible)
{
    const auto valid = Validate(state, view, range, polygon, segment);
    if (!valid.HasValue())
    {
        return valid;
    }
    TerrainPickingState next = state;
    const auto result = Pass(next, view, range, polygon, segment, forceVisible);
    if (result.HasValue())
    {
        state = next;
    }
    return result;
}

Core::Expected<bool, std::string> PickTerrainTarget(TerrainPickingState& state, TerrainPickingTarget& target,
                                                    const TerrainView& view, const TerrainRange& range,
                                                    const TerrainPolygon& polygon, const TerrainPickingSegment& segment,
                                                    bool forceVisible)
{
    TerrainPickingState next = state;
    const auto pass = PickTerrain(next, view, range, polygon, segment, forceVisible);
    if (!pass.HasValue())
    {
        return Core::Unexpected(pass.Error());
    }
    const auto retry = Retry(next, view, segment);
    if (!retry.HasValue())
    {
        return retry;
    }
    if (retry.Value())
    {
        const auto published = PublishTarget(target, next, view);
        if (!published.HasValue())
        {
            return Core::Unexpected(published.Error());
        }
    }
    state = next;
    return retry.Value();
}
} // namespace Client::World
