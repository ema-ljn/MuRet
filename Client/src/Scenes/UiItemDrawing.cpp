#include "Scenes/UiItemDrawing.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Pose.h"
#include "Render/Pass3D.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Scenes/GameProjection.h"
#include "UI/Widget.h"
#include "World/OriginalFtol.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int2.hpp>
#include <glm/ext/vector_int4.hpp>
#include <glm/trigonometric.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

float Bits(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

// ---------------------------------------------------------------------------------------------------------------
// 0x5e1480: el punto de la caja.

// Los factores de la tabla, con sus direcciones.
constexpr float F40 = 0.4f;   // 0x6b87b4
constexpr float F45 = 0.45f;  // 0x6ba224
constexpr float F50 = 0.5f;   // 0x6b882c
constexpr float F55 = 0.55f;  // 0x6ba2ac
constexpr float F60 = 0.6f;   // 0x6b8a20
constexpr float F65 = 0.65f;  // 0x6b8bc0
constexpr float F70 = 0.7f;   // 0x6b8a5c
constexpr float F75 = 0.75f;  // 0x6ba36c
constexpr float F80 = 0.8f;   // 0x6b8a38
constexpr float F85 = 0.85f;  // 0x6b8bac
constexpr float F90 = 0.9f;   // 0x6b8a1c
constexpr float F95 = 0.95f;  // 0x6ba30c
constexpr float F105 = 1.05f; // 0x6ba3d4
constexpr float F110 = 1.1f;  // 0x6b8830

// x + w · x e y + h · y; con `plusHeight` (0x5e1d44), y + h sin producto.
struct AnchorFactors
{
    float x = 0.0f;
    float y = 0.0f;
    bool plusHeight = false;
};

// La tabla de 0x5e14ef..0x5e1d4f en su orden. Sin valor, la esquina de la caja.
std::optional<AnchorFactors> AnchorFactorsOf(std::int32_t type, std::uint32_t level)
{
    const std::int32_t step = static_cast<std::int32_t>(level) >> 3;
    const std::uint32_t steps = level & 0xfffffff8u;
    constexpr AnchorFactors SixPlusHeight{.x = F60, .y = 0.0f, .plusHeight = true};
    if (type >= 0 && type < 0x200)
        return AnchorFactors{F80, F85};
    if (type >= 0x200 && type < 0x600)
        return type == 0x40d ? AnchorFactors{F60, F50} : AnchorFactors{F80, F70};
    if (type >= 0x600 && type < 0x800)
        return AnchorFactors{F60, F65};
    if (type == 0x811)
        return AnchorFactors{F50, F50};
    if (type == 0x813)
        return AnchorFactors{F70, F75};
    if (type == 0x814)
        return AnchorFactors{F50, F55};
    if (type >= 0x808 && type < 0xa00)
        return AnchorFactors{F70, F70};
    if (type >= 0xa00 && type < 0xc00)
        return AnchorFactors{F60, F55};
    if (type >= 0xc00 && type < 0xe00)
    {
        if (type == 0xc0f)
            return AnchorFactors{F50, F70};
        if (type == 0xc10)
            return AnchorFactors{F50, F90};
        return AnchorFactors{F50, F60};
    }
    if (type >= 0xe00 && type < 0x1000)
        return AnchorFactors{F50, F80};
    if (type >= 0x1000 && type < 0x1200)
    {
        if (type == 0x1002 || type == 0x1004 || type == 0x1006)
            return AnchorFactors{F50, F105};
        if (type == 0x1003 || type == 0x1008)
            return AnchorFactors{F50, F110};
        // El 0x1011, el 0x1012 y el 0x1014 saltan al 0,8 de 0x5e1c00: el mismo que los demás.
        return AnchorFactors{F50, F80};
    }
    if (type >= 0x1200 && type < 0x1800)
        return AnchorFactors{F50, F90};
    switch (type)
    {
    case 0x1a0e:
        return steps == 8 ? AnchorFactors{F55, F85} : SixPlusHeight;
    case 0x1a0f:
        return SixPlusHeight;
    case 0x1a10:
    case 0x1a11:
        return AnchorFactors{F50, F90};
    case 0x1a12:
        return AnchorFactors{F50, F75};
    case 0x1a13:
        if (step == 0)
            return AnchorFactors{F50, F50};
        if (step == 1)
            return AnchorFactors{F70, F80};
        if (step == 2)
            return AnchorFactors{F70, F70};
        return std::nullopt;
    case 0x1a14:
        if (step == 0)
            return AnchorFactors{F50, F65};
        if (step > 0 && step <= 3)
            return AnchorFactors{F50, F80};
        return std::nullopt;
    case 0x1a1d:
        return AnchorFactors{F50, F50};
    case 0x1a04:
        return AnchorFactors{F50, F60};
    case 0x1a1e:
        return AnchorFactors{F50, F50};
    case 0x1a1f:
        return AnchorFactors{F50, F90};
    case 0x1c07:
        return AnchorFactors{F50, F50};
    case 0x1a07:
        return AnchorFactors{F50, F90};
    case 0x1a0b:
        if (step == 0)
            return AnchorFactors{F50, F80};
        if (step == 1)
            return AnchorFactors{F50, F50};
        return std::nullopt;
    case 0x1a20:
    case 0x1c33:
    case 0x1c34:
    case 0x1c35:
    case 0x1c36:
    case 0x1c37:
        return AnchorFactors{F50, F50};
    default:
        break;
    }
    if (type >= 0x1a00 && type < 0x1c00)
        return AnchorFactors{F50, F70};
    switch (type)
    {
    case 0x1c0c:
        return AnchorFactors{F50, F50};
    case 0x1c0b:
        if (steps == 0x18 || steps == 0x68)
            return AnchorFactors{F50, F50};
        if (steps == 0x70 || steps == 0x78)
            return AnchorFactors{F50, F80};
        return AnchorFactors{F50, F95};
    case 0x1c09:
        return steps == 8 ? AnchorFactors{F50, F80} : AnchorFactors{F50, F95};
    case 0x1c11:
    case 0x1c12:
    case 0x1c13:
        return AnchorFactors{F50, F50};
    case 0x1c15:
        if (step == 0 || step == 3)
            return AnchorFactors{F50, F50};
        if (step == 1)
            return AnchorFactors{F40, F80};
        return std::nullopt;
    case 0x1c16:
    case 0x1c17:
        return AnchorFactors{F50, F95};
    case 0x1c18:
        return steps == 8 ? AnchorFactors{F50, F80} : AnchorFactors{F50, F95};
    case 0x1c19:
    case 0x1c1a:
        return AnchorFactors{F50, F90};
    case 0x1c1f:
        return AnchorFactors{F50, F50};
    case 0x181e:
    case 0x181f:
        return AnchorFactors{F55, F80};
    case 0x1803:
        return AnchorFactors{F50, F45};
    case 0x1804:
        return AnchorFactors{F50, F40};
    case 0x1805:
        return AnchorFactors{F50, F75};
    case 0x1806:
        return AnchorFactors{F50, F55};
    default:
        break;
    }
    if (type < 0x1c00 || type >= 0x1e00)
        return AnchorFactors{F50, F60};
    return AnchorFactors{F50, F95};
}

// ---------------------------------------------------------------------------------------------------------------
// 0x5e0720: los ángulos, los corrimientos y la escala.

constexpr std::uint16_t PlayerModel = 0x20a;
constexpr std::int32_t FirstArmor = 0x1012;
constexpr std::int32_t EndArmor = 0x1a12;
constexpr std::int32_t Zen = 0x1e1e;

// Los corrimientos del punto: cada uno, fld, fadd o fsub de un float y fstp.
constexpr float Shift001 = 0.01f;   // 0x6b88d4
constexpr float Shift002 = 0.02f;   // 0x6b8b90
constexpr float Shift0025 = 0.025f; // 0x6b8bd0
constexpr float Shift003 = 0.03f;   // 0x6b8bd4
constexpr float Shift004 = 0.04f;   // 0x6b8aec
constexpr float Shift005 = 0.05f;   // 0x6b8a48
constexpr float Shift006 = 0.06f;   // 0x6ba230
constexpr float Shift007 = 0.07f;   // 0x6ba190
constexpr float Shift01 = 0.1f;     // 0x6b87b8
constexpr float Shift012 = 0.12f;   // 0x6ba1d8
// 0x6ba618: el del 0x620 es un double (fsub qword).
constexpr double Shift001Double = 0.01;

void Add(float& value, float shift)
{
    value = F32(static_cast<double>(value) + static_cast<double>(shift));
}

void Subtract(float& value, float shift)
{
    value = F32(static_cast<double>(value) - static_cast<double>(shift));
}

// 0x5e078e..0x5e0dd6: los ángulos y los corrimientos de la tabla, en su orden.
Core::Expected<void, std::string> SetAngles(std::int32_t model, std::int32_t step, const Data::Tables::ItemTable* items,
                                            glm::vec3& position, glm::vec3& angles)
{
    const auto set = [&angles](float x, float y, float z) { angles = glm::vec3(x, y, z); };
    if (model == 0xa19 || model == 0xa21)
        set(0.0f, 270.0f, 15.0f);
    else if (model == 0xa23)
        set(0.0f, 90.0f, 15.0f);
    else if (model == 0x1031)
    {
        Subtract(position.y, Shift006);
        Add(position.x, Shift003);
        set(-90.0f, 0.0f, 0.0f);
    }
    else if (model == 0x1030)
    {
        Add(position.y, Shift007);
        Subtract(position.x, Shift003);
        set(-90.0f, 0.0f, 0.0f);
    }
    else if (model == 0x1230)
    {
        Add(position.y, Shift01);
        set(-90.0f, 0.0f, 0.0f);
    }
    else if (model == 0x122f)
    {
        Add(position.y, Shift007);
        set(-90.0f, 0.0f, 0.0f);
    }
    else if (model == 0xa27)
    {
        Add(position.y, Shift012);
        set(180.0f, 90.0f, 15.0f);
    }
    else if (model == 0xc1e)
    {
        Subtract(position.y, Shift01);
        set(180.0f, 0.0f, 8.0f);
        Add(position.x, Shift0025);
    }
    else if (model == 0x620)
    {
        Add(position.y, Shift01);
        set(180.0f, 90.0f, 13.0f);
        position.x = F32(static_cast<double>(position.x) - Shift001Double);
    }
    else if (model == 0xa26)
        set(180.0f, -90.0f, 15.0f);
    else if (model >= 0xa1a && model < 0xc12)
        set(90.0f, 180.0f, 20.0f);
    else if (model == 0x81c)
        set(180.0f, 270.0f, 20.0f);
    else if (model >= 0x212 && model < 0xe12)
    {
        // 0x5e09bd: el byte +0x1e del registro de Item.bmd del tipo (las dos manos): 15 en 0, 25 si no.
        const auto type = static_cast<std::size_t>(model - 0x212);
        if (items == nullptr)
            return Core::Unexpected(std::string("0x5e09bd: sin Item.bmd"));
        const auto record = items->Record(type);
        if (!record)
            return Core::Unexpected(std::format("0x5e09bd: Item.bmd no tiene el tipo {:#x}", type));
        constexpr std::size_t TwoHands = 0x1e;
        set(180.0f, 270.0f, std::to_integer<std::uint8_t>((*record)[TwoHands]) == 0 ? 15.0f : 25.0f);
    }
    else if (model >= 0xe12 && model < 0x1012)
        set(270.0f, 270.0f, 0.0f);
    else if (model == 0x1c15 || model == 0x1c16 || model == 0x1c31)
        set(-90.0f, -90.0f, 0.0f);
    else if (model == 0x1c17)
        set(-90.0f, -35.0f, 0.0f);
    else if (model == 0x1c30 || model == 0x224a)
        set(-90.0f, 0.0f, 0.0f);
    else if (model == 0x1c22 || model == 0x1c23)
        set(270.0f, -10.0f, 0.0f);
    else if (model == 0x1c24)
        set(290.0f, 0.0f, 0.0f);
    else if (model == 0x2245)
        set(-90.0f, -20.0f, -20.0f);
    else if (model == 0x2246)
        set(250.0f, 140.0f, 0.0f);
    else if (model == 0x2248)
        set(255.0f, 160.0f, 0.0f);
    else if (model == 0x2249)
        set(270.0f, 0.0f, 0.0f);
    else if (model >= 0x1c27 && model <= 0x1c2a)
        set(270.0f, 160.0f, 20.0f);
    else if (model == 0x1c2f)
        set(290.0f, 0.0f, 0.0f);
    else if (model == 0x1c32)
    {
        Add(position.x, Shift001);
        Subtract(position.y, Shift003);
        set(270.0f, 0.0f, 0.0f);
    }
    else if (model == 0x1c33)
    {
        Add(position.y, Shift002);
        set(270.0f, 0.0f, 0.0f);
    }
    else if (model == 0x1c34 || model == 0x1c35)
    {
        Add(position.x, Shift001);
        Add(position.y, Shift002);
        set(270.0f, 0.0f, 0.0f);
    }
    else if (model == 0x1c36)
    {
        Add(position.x, Shift001);
        Add(position.y, Shift005);
        set(270.0f, 0.0f, 0.0f);
    }
    else if (model == 0x1c37)
    {
        Add(position.x, Shift001);
        Add(position.y, Shift004);
        set(270.0f, 0.0f, 0.0f);
    }
    else if (model >= 0x1c1e && model < 0x1e12 && model != 0x1c20 && model != 0x1c21)
        set(360.0f, 0.0f, 0.0f);
    else if (model == Zen)
    {
        // 0x5e0cd9: con otro paso, sin tocarlos (Q-C478).
        if (step == 0)
            set(180.0f, 0.0f, 0.0f);
        else if (step == 1)
            set(270.0f, 90.0f, 0.0f);
        else if (step == 2)
            set(90.0f, 0.0f, 0.0f);
    }
    else if (model == 0x223f || model == 0x2241 || model == 0x1e26 || model == 0x1e2d)
        set(270.0f, 0.0f, 0.0f);
    else if (model == 0x2240)
        set(270.0f, 90.0f, 0.0f);
    else
        set(270.0f, -10.0f, 0.0f);
    return {};
}

// 0x5e0e7c..0x5e0ef2: la z de más de la raíz del que posa, por el que dibuja.
float RootHeightOf(std::int32_t model)
{
    if (model >= 0x1012 && model < 0x1212)
        return -160.0f;
    if (model >= 0x1212 && model < 0x1412)
        return -100.0f;
    if (model >= 0x1612 && model < 0x1812)
        return -70.0f;
    if (model >= 0x1412 && model < 0x1612)
        return -50.0f;
    return 0.0f;
}

// 0x5e0ef8..0x5e13be: la escala por el que dibuja, con los bits del binario; el 0x225, el 0xc1c y el 0xa24 con un
// nivel negativo dejan el nivel en 0, y el 0x1e32 al 0x1e34 corren la y y apagan dos ángulos.
float ScaleOf(std::int32_t model, std::int32_t& level, glm::vec3& position, glm::vec3& angles)
{
    if (model >= FirstArmor && model < EndArmor)
    {
        float scale = 0.0f;
        if (model < 0x1212)
            scale = Bits(0x3b7f9724u);
        else if (model < 0x1412)
            scale = Bits(0x3b7f9724u);
        else if (model >= 0x1612 && model < 0x1812)
            scale = Bits(0x3b79096cu);
        else if (model >= 0x1412 && model < 0x1612)
            scale = Bits(0x3b5844d0u);
        else if (model >= 0x1812)
            scale = Bits(0x3b51b717u);
        if (model == 0x1031)
            return Bits(0x3be56042u);
        if (model == 0x1230 || model == 0x1232)
            return Bits(0x3b656042u);
        if (model == 0x122f)
            return Bits(0x3b5844d0u);
        return scale;
    }
    const auto negativeLevel = [&level](std::uint32_t positive, std::uint32_t negative)
    {
        if (level >= 0)
            return Bits(positive);
        level = 0;
        return Bits(negative);
    };
    if (model == 0x1a18)
        return Bits(0x3ac49ba6u);
    if (model == 0x1a30 || model == 0x1a31)
        return Bits(0x3b83126fu);
    if (model >= 0x1a12 && model < 0x1c12)
        return Bits(0x3b03126fu);
    if (model >= 0x1e32 && model <= 0x1e34)
    {
        Add(position.y, Shift005);
        angles.x = 0.0f;
        angles.z = 0.0f;
        return Bits(0x3b03126fu);
    }
    switch (model)
    {
    case 0x1e27:
    case 0xa25:
        return Bits(0x3b03126fu);
    case 0x2245:
    case 0x1c16:
        return Bits(0x3ac49ba6u);
    case 0x1c17:
        return Bits(0x3ba3d70au);
    case 0x1c30:
    case 0x224a:
    case 0x1c22:
        return Bits(0x3b03126fu);
    case 0x1c23:
    case 0x1c24:
        return Bits(0x3aebedfau);
    case 0x225:
        return negativeLevel(0x3b23d70au, 0x3a83126fu);
    case 0xc1c:
        return negativeLevel(0x3af9096cu, 0x3a83126fu);
    case 0xa24:
        return negativeLevel(0x3b23d70au, 0x3ac49ba6u);
    default:
        break;
    }
    if (model >= 0x61a && model <= 0x61d)
        return Bits(0x3b449ba6u);
    switch (model)
    {
    case 0x61e:
        return Bits(0x3b23d70au);
    case 0x2246:
        return Bits(0x3a9d4952u);
    case 0x2247:
        return Bits(0x3b23d70au);
    case 0x2248:
        return Bits(0x3b378034u);
    case 0x2249:
        return Bits(0x3b16bb99u);
    default:
        break;
    }
    if (model >= 0x1e28 && model < 0x1e2b)
        return Bits(0x3b23d70au);
    if (model >= 0x1e2b && model < 0x1e2d)
        return Bits(0x3b378034u);
    switch (model)
    {
    case 0x1e19:
    case 0x1c19:
    case 0x1c1d:
        return Bits(0x3b23d70au);
    case 0x1c32:
        return Bits(0x3af9096cu);
    case 0x1c33:
    case 0x1c34:
    case 0x1c35:
        return Bits(0x3b83126fu);
    case 0x1c36:
        return Bits(0x3be56042u);
    case 0x1c37:
        return Bits(0x3ba3d70au);
    case 0xa27:
        return Bits(0x3b102de0u);
    case 0x1c20:
    case 0x1c21:
        return Bits(0x3b449ba6u);
    default:
        break;
    }
    if (model >= 0x1e12 && model < 0x2012)
        return Bits(0x3b656042u);
    if (model >= 0x812 && model < 0xa12)
        return Bits(0x3aebedfau);
    if (model >= 0xc12 && model < 0xe12)
        return Bits(0x3b102de0u);
    switch (model)
    {
    case 0xa21:
        return Bits(0x3a902de0u);
    case 0xa19:
        return Bits(0x3a9d4952u);
    case 0x2240:
        return Bits(0x3b7f9724u);
    case 0x2242:
        return Bits(0x3ac49ba6u);
    case 0x2243:
        return Bits(0x3af9096cu);
    case 0x2244:
        return Bits(0x3a83126fu);
    default:
        return Bits(0x3b23d70au);
    }
}
} // namespace

std::optional<UiItemCall> UiItemCallOf(std::int32_t type, std::uint32_t level)
{
    const auto packed = static_cast<std::int32_t>(level);
    const std::int32_t step = packed >> 3;
    const std::uint32_t steps = level & 0xfffffff8u;
    constexpr std::int32_t ModelOffset = 0x212;
    constexpr std::int32_t NoLevel = -1;
    const auto call = [packed](std::int32_t model) { return UiItemCall{.model = model, .level = packed}; };
    switch (type)
    {
    case 0x1c0b:
        if (steps == 8)
            return call(0x223e);
        if (steps == 0x10 || steps == 0x70 || steps == 0x78)
            return call(0x223f);
        if (steps == 0x18 || steps == 0x68)
            return call(0x2240);
        if (steps == 0x28)
            return call(0x2242);
        if (steps == 0x30)
            return call(0x2243);
        if (static_cast<std::int32_t>(steps) > 0x3f && static_cast<std::int32_t>(steps) < 0x61)
            return call(0x2244);
        break;
    case 0x1a0e:
        if (steps == 8)
            return call(0x224a);
        break;
    case 0x1c09:
        if (steps == 8)
            return call(0x2241);
        break;
    case 0x1c15:
        return call(step == 1 ? 0x2245 : 0x1e27);
    case 0x1a13:
        if (step == 0)
            return UiItemCall{.model = 0xc1c, .level = NoLevel};
        if (step == 1)
            return UiItemCall{.model = 0x225, .level = NoLevel};
        if (step == 2)
            return UiItemCall{.model = 0xa24, .level = NoLevel};
        return std::nullopt;
    case 0x1c17:
        if (step == 0)
            return call(0x1e29);
        if (step == 1)
            return UiItemCall{.model = 0x2246, .level = NoLevel};
        return std::nullopt;
    case 0x1c18:
        if (step == 0)
            return call(0x1e2a);
        if (step == 1)
            return UiItemCall{.model = 0x2247, .level = NoLevel};
        return std::nullopt;
    case 0x1a14:
        if (step == 0)
            return call(0x2249);
        if (step >= 1 && step <= 3)
            return call(0x2248);
        return std::nullopt;
    case 0x1a0b:
        if (steps == 8)
            return call(0x224c);
        break;
    default:
        break;
    }
    return call(type + ModelOffset);
}

bool UiItemHovered(const UiItemBox& box, UI::Point mouse, std::int32_t cursorItemCount, bool always)
{
    if (cursorItemCount != 0 && !always)
        return false;
    // fild del mouse, y la suma de la caja en la x87.
    const auto x = static_cast<double>(mouse.x);
    const auto y = static_cast<double>(mouse.y);
    return static_cast<double>(box.x) <= x && x < static_cast<double>(box.x) + static_cast<double>(box.width) &&
           static_cast<double>(box.y) <= y && y < static_cast<double>(box.y) + static_cast<double>(box.height);
}

Core::Expected<glm::ivec2, std::string> UiItemAnchor(std::int32_t type, std::uint32_t level, const UiItemBox& box)
{
    auto x = static_cast<double>(box.x);
    auto y = static_cast<double>(box.y);
    if (const std::optional<AnchorFactors> factors = AnchorFactorsOf(type, level))
    {
        x += static_cast<double>(box.width) * static_cast<double>(factors->x);
        y += factors->plusHeight ? static_cast<double>(box.height)
                                 : static_cast<double>(box.height) * static_cast<double>(factors->y);
    }
    const std::optional<std::int32_t> pixelX = World::OriginalFtolLow(x);
    const std::optional<std::int32_t> pixelY = World::OriginalFtolLow(y);
    if (!pixelX || !pixelY)
        return Core::Unexpected(std::string("0x5e1d58: el punto del ítem de la UI no es representable"));
    return glm::ivec2(*pixelX, *pixelY);
}

Core::Expected<UiItemPlacement, std::string> PlaceUiItem(const UiItemCall& call, const UnprojectedPoint& anchor,
                                                         bool hovered, bool always, float worldClock,
                                                         const Data::Tables::ItemTable* items, glm::vec3& angles)
{
    UiItemPlacement placement;
    std::int32_t model = call.model;
    placement.level = call.level;
    // 0x5e073c: sar y and, del nivel que llega.
    const std::int32_t step = (call.level >> 3) & 0xf;
    // 0x5e072f..0x5e075d: el punto menos la cámara (fsub y fstp), y 0x5fc790: la cámara más 0,07 (siempre) o 0,1 de
    // eso, en la x87 con el fstp.
    const float toCamera = always ? Bits(0x3d8f5c29u) : Bits(0x3dcccccdu);
    for (int axis = 0; axis < 3; ++axis)
    {
        const float difference =
            F32(static_cast<double>(anchor.point[axis]) - static_cast<double>(anchor.camera[axis]));
        placement.position[axis] = F32(static_cast<double>(toCamera) * static_cast<double>(difference) +
                                       static_cast<double>(anchor.camera[axis]));
    }
    if (auto set = SetAngles(model, step, items, placement.position, angles); !set.HasValue())
        return Core::Unexpected(set.Error());
    // 0x5e0de0: bajo el mouse, la y gira con el reloj (0x6ba224, 0,45).
    if (hovered)
        angles.y = F32(static_cast<double>(worldClock) * static_cast<double>(F45));
    // 0x5e0df8..0x5e0e4b: el que posa, y el 0x1e1e cambia el que dibuja con el paso 0 o 2.
    std::int32_t poseModel = model;
    if (model >= FirstArmor && model < EndArmor)
        poseModel = PlayerModel;
    else if (model == Zen && step == 0)
        model = poseModel = 0x223a;
    else if (model == Zen && step == 2)
        model = poseModel = 0x223b;
    placement.rootHeight = RootHeightOf(model);
    placement.scale = ScaleOf(model, placement.level, placement.position, angles);
    placement.model = static_cast<std::uint16_t>(model);
    placement.poseModel = static_cast<std::uint16_t>(poseModel);
    placement.angles = angles;
    return placement;
}

Core::Expected<UiItemPass, std::string> MakeUiItemPass(App::PixelSize screen)
{
    // 0x5ac15f..0x5ac17f: fild del ancho, fidiv del alto y fstp; 0x6203c0(1, aspecto, 20, 2.000).
    constexpr float FieldOfView = 1.0f;
    constexpr float NearPlane = 20.0f;
    constexpr float FarPlane = 2000.0f;
    UiItemPass made;
    made.pass.screen = screen;
    made.pass.viewport = glm::ivec4(0, 0, screen.width, screen.height);
    const auto aspect = F32(static_cast<double>(screen.width) / static_cast<double>(screen.height));
    made.pass.projection = glm::perspective(glm::radians(FieldOfView), aspect, NearPlane, FarPlane);
    made.pass.view = glm::mat4(1.0f);
    made.pass.farPlane = FarPlane;
    auto projection = MakeGameProjection(made.pass, FieldOfView);
    if (!projection.HasValue())
        return Core::Unexpected(projection.Error());
    made.projection = projection.Value();
    return made;
}

Core::Expected<void, std::string> DrawUiItemModel(UiItemResources& resources, Render::Models::CrtRandom& random,
                                                  const UiItemPlacement& placement, std::uint8_t excellent,
                                                  std::uint8_t set)
{
    if (resources.models.Find(placement.model) == nullptr ||
        resources.models.SkeletonOf(placement.poseModel) == nullptr)
        return Core::Unexpected(std::format("0x5e13d9: falta el modelo {:#x} o el esqueleto de {:#x} del ítem de la UI",
                                            placement.model, placement.poseModel));
    if (!resources.open)
    {
        if (resources.flushSprites)
            resources.flushSprites();
        // 0x5ac120 no pasa por 0x620db0: el material, la niebla y el modo de la textura siguen como estaban.
        resources.batch.Begin(resources.pass.pass, false);
        resources.open = true;
    }
    // 0x5e13c5: 0x4eb860 con el modelo que posa en el buffer global, la acción (+0xa0 del modelo), el frame y el
    // anterior en 0, los ángulos de 0x78aadcc, los de la cabeza (0x78aadd8, que nadie escribe) en 0 y sin la escala ni
    // la posición en la raíz.
    DrawnObject object;
    object.model = placement.poseModel;
    object.transform = Render::Models::ObjectTransform{.position = placement.position,
                                                       .angles = placement.angles,
                                                       .scale = placement.scale,
                                                       .lockedRootHeight = placement.rootHeight};
    // Sin el skin de la pose: lo hace 0x4ebc80 con el modelo que dibuja.
    object.alpha = 0.0f;
    if (auto posed = PublishFaunaFloorPose(resources.poseCarry, resources.models, object); !posed.HasValue())
        return posed;
    // 0x5e13f3: 0x607b60 con el que dibuja en +2, con sus rand(); 0x5e1404..0x5e1466: la escala, la posición, la luz
    // por vértice en 0 y +0x1d4 en 2. El alpha (+0x174) y la luz, en 1.
    Render::Models::ObjectRandoms randoms{};
    for (std::size_t index = 0; index < HangingRandomCount(placement.model); ++index)
        randoms[index] = random.Next();
    object.model = placement.model;
    object.state = Render::Models::NativeItemObjectState(placement.model, resources.frameClock, randoms);
    object.color = glm::vec3(1.0f);
    object.alpha = 1.0f;
    object.vertexLight = false;
    // 0x60cf70: 0x4ebc80 sobre el buffer global y 0x60b3d0, sin dueño (los efectos de 0x60cf70 no corren).
    if (auto skinned = TransformFaunaPose(resources.poseCarry, resources.models, object); !skinned.HasValue())
        return skinned;
    ItemDrawContext context = resources.itemContext;
    ItemSpecialDrawContext special = context.special != nullptr ? *context.special : ItemSpecialDrawContext{};
    // +0xf4..+0xfc del objeto local, que 0x607b60 pone en 0,3.
    glm::vec3 objectLight(0.3f);
    special.objectLight = &objectLight;
    special.publishedObject = &object;
    context.special = &special;
    context.ownerContext = nullptr;
    // 0x60b3d0 desempaca el nivel: (nivel >> 3) & 15.
    const auto level = static_cast<std::uint8_t>((placement.level >> 3) & 15);
    return DrawItemFromMeshes(resources.batch, resources.models, object, resources.frameClock,
                              resources.poseCarry.meshes, level, excellent, set, 4, context);
}

void CloseUiItemPass(UiItemResources& resources)
{
    if (!resources.open)
        return;
    if (resources.flushModels)
        resources.flushModels();
    resources.open = false;
}
} // namespace Client::Scenes
