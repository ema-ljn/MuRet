#include "World/Fish.h"
#include "World/IFishStepServices.h"
#include "Core/Expected.h"
#include "Render/Models/OriginalRotation.h"

#include <gtest/gtest.h>
#include <glm/ext/vector_float3.hpp>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace
{
using Client::Core::Expected;
using Client::Core::Unexpected;
using namespace Client::World;
struct Storage
{
    std::uint8_t active = 0, field8f = 19, fieldE8 = 20, action = 21, previousAction = 22, field16d = 23;
    std::int16_t type = -1;
    std::int32_t counter = 9, field58 = 10, timer = 11, field64 = 12;
    float scale = 14, oscillation = 15, phase = 16, aheadX = 17, aheadY = 18, speed = 1, turnRate = 24;
    float frame = 25, previousFrame = 26, field170 = 27, field174 = 13;
    glm::vec3 position{1, 2, 3}, angles{4, 5, 6}, previousPosition{7, 8, 9}, light{10, 11, 12};
    Client::Render::Models::OriginalMatrix matrix{};
    FishStepView View()
    {
        return {active,         type,   counter,       scale,       position, angles,   previousPosition,
                field58,        timer,  field64,       oscillation, phase,    field8f,  matrix,
                aheadX,         aheadY, speed,         turnRate,    fieldE8,  light,    action,
                previousAction, frame,  previousFrame, field16d,    field170, field174, this};
    }
};
// Captura valores y direcciones prestadas; no retiene la solicitud efímera ni ejecuta efectos.
struct EffectObservation
{
    std::int32_t type;
    std::int32_t subtype;
    std::uint32_t scaleBits;
    const glm::vec3* firstPosition;
    const glm::vec3* secondPosition;
    const glm::vec3* angles;
    void* owner;
};
class Services : public IFishStepServices
{
public:
    std::vector<std::string> calls;
    std::vector<std::int32_t> random;
    std::size_t cursor = 0;
    std::uint8_t gate = 0, modelAction = 0;
    glm::vec3 rotated{0, 0, 0};
    double height = 40, product = 0.25;
    std::function<void(const std::string&)> hook;
    std::string failure;
    std::vector<void*> retainedContexts;
    std::vector<float> numeric;
    std::vector<double> conversions;
    std::vector<float> modelSpeeds;
    std::vector<float> rotateLengths;
    std::vector<std::int32_t> movementModes;
    std::vector<EffectObservation> effects;
    std::vector<std::int32_t> modes;
    Expected<void, std::error_code> Mark(const std::string& name)
    {
        calls.push_back(name);
        if (hook)
            hook(name);
        if (name == failure)
            return Unexpected(std::make_error_code(std::errc::io_error));
        return {};
    }
    Expected<std::int32_t, std::error_code> Random() override
    {
        auto r = Mark("rng");
        if (!r.HasValue())
            return Unexpected(r.Error());
        if (cursor >= random.size())
            return Unexpected(std::make_error_code(std::errc::no_message_available));
        return random[cursor++];
    }
    Expected<std::int32_t, std::error_code> Truncate(double value) override
    {
        conversions.push_back(value);
        numeric.push_back(static_cast<float>(value));
        auto r = Mark("convert");
        if (!r.HasValue())
            return Unexpected(r.Error());
        return static_cast<std::int32_t>(value);
    }
    Expected<std::uint8_t, std::error_code> EventGate() override
    {
        auto r = Mark("gate");
        if (!r.HasValue())
            return Unexpected(r.Error());
        return gate;
    }
    Expected<std::uint32_t, std::error_code> CreateEffect(const FishEffectRequest& q) override
    {
        effects.push_back({q.type, q.subtype, std::bit_cast<std::uint32_t>(q.scale), &q.firstPosition,
                           &q.secondPosition, &q.angles, q.ownerContext});
        EXPECT_EQ(&q.firstPosition, &q.secondPosition);
        EXPECT_NE(q.ownerContext, nullptr);
        retainedContexts.push_back(q.ownerContext);
        EXPECT_EQ(q.key, -1);
        EXPECT_EQ(q.skill, 0);
        EXPECT_EQ(q.skillSerial, 0);
        EXPECT_EQ(q.target, -1);
        modes.push_back(q.subtype);
        numeric.push_back(q.scale);
        auto r = Mark("effect" + std::to_string(q.type));
        if (!r.HasValue())
            return Unexpected(r.Error());
        return std::uint32_t{0};
    }
    Expected<double, std::error_code> Height(float x, float y) override
    {
        numeric.push_back(x);
        numeric.push_back(y);
        auto r = Mark("height");
        if (!r.HasValue())
            return Unexpected(r.Error());
        return height;
    }
    Expected<FishModelStepView, std::error_code> LookupModel(std::int16_t type) override
    {
        modes.push_back(type);
        auto r = Mark("lookup");
        if (!r.HasValue())
            return Unexpected(r.Error());
        return FishModelStepView{modelAction, this};
    }
    Expected<std::uint8_t, std::error_code> AdvanceModel(FishModelStepView model, FishStepView& fish,
                                                         float speed) override
    {
        EXPECT_EQ(model.identity, this);
        EXPECT_EQ(model.action, fish.action);
        modelSpeeds.push_back(speed);
        numeric.push_back(speed);
        auto r = Mark("model");
        if (!r.HasValue())
            return Unexpected(r.Error());
        return std::uint8_t{0x80};
    }
    Expected<void, std::error_code> Move(FishStepView&, std::int32_t mode) override
    {
        movementModes.push_back(mode);
        modes.push_back(mode);
        return Mark("move");
    }
    Expected<void, std::error_code> Steer(FishStepView& fish, std::size_t index,
                                          std::span<FishStepView, FishPoolSize> pool) override
    {
        EXPECT_EQ(&fish.position, &pool[index].position);
        EXPECT_EQ(pool.size(), 10U);
        return Mark("steer");
    }
    Expected<void, std::error_code> AngleMatrix(FishStepView&) override
    {
        return Mark("matrix");
    }
    Expected<glm::vec3, std::error_code> Rotate(const glm::vec3& input,
                                                const Client::Render::Models::OriginalMatrix&) override
    {
        rotateLengths.push_back(input.x);
        numeric.push_back(input.x);
        auto r = Mark("rotate");
        if (!r.HasValue())
            return Unexpected(r.Error());
        return rotated;
    }
    Expected<std::int32_t, std::error_code> Sound(std::int32_t id, void* context, std::int32_t mode) override
    {
        retainedContexts.push_back(context);
        EXPECT_EQ(id, 15);
        EXPECT_EQ(mode, 0);
        auto r = Mark("sound");
        if (!r.HasValue())
            return Unexpected(r.Error());
        return -17;
    }
    Expected<void, std::error_code> Finalize(FishStepView&) override
    {
        return Mark("final");
    }
    Expected<double, std::error_code> OscillationProductPC53(float phase) override
    {
        numeric.push_back(phase);
        auto r = Mark("sineProduct");
        if (!r.HasValue())
            return Unexpected(r.Error());
        return product;
    }
};
class FishLifecycleTest : public ::testing::Test
{
protected:
    std::array<Storage, 10> m_slots;
    std::int32_t m_map = 2;
    glm::vec3 m_position{0, 0, 0};
    std::uint8_t m_flag = 0;
    FishHeroView m_heroView{m_position, m_flag};
    const FishHeroView* m_hero = &m_heroView;
    std::array<std::uint8_t, 65536> m_surfaces{};
    std::array<std::uint16_t, 65536> m_attributes{};
    Services m_services;
    Expected<void, std::error_code> Step()
    {
        std::array<FishStepView, 10> views{m_slots[0].View(), m_slots[1].View(), m_slots[2].View(), m_slots[3].View(),
                                           m_slots[4].View(), m_slots[5].View(), m_slots[6].View(), m_slots[7].View(),
                                           m_slots[8].View(), m_slots[9].View()};
        return StepFish(views, {m_map, m_hero, m_surfaces, m_attributes}, m_services);
    }
};
TEST_F(FishLifecycleTest, RejectedCandidatesKeepStoresAndSkipFinalizer)
{
    m_services.random = {512, 512, 513, 514, 515, 516};
    auto r = Step();
    ASSERT_TRUE(r.HasValue());
    EXPECT_EQ(m_services.cursor, 6U);
    EXPECT_EQ(m_slots[0].position.z, 0);
    EXPECT_EQ(m_slots[1].position.x, 1);
    EXPECT_EQ(m_slots[1].position.y, 2);
    EXPECT_EQ(m_slots[0].timer, 11);
    EXPECT_EQ(m_slots[0].counter, 9);
    EXPECT_EQ(m_slots[0].active, 0);
    EXPECT_EQ(m_slots[3].position.x, 1);
    EXPECT_EQ(m_services.calls,
              (std::vector<std::string>{"rng", "rng", "convert", "convert", "gate", "rng", "rng", "convert", "convert",
                                        "gate", "rng", "rng", "convert", "convert", "gate"}));
}

TEST_F(FishLifecycleTest, ReducedMovementKeepsActiveAndWrapsTimerBeforeReset)
{
    for (auto& f : m_slots)
    {
        f.active = 0x80;
        f.type = -1;
        f.timer = 0;
    }
    m_services.random = {0, -129, 1, 1};
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_slots[0].active, 0x80);
    EXPECT_EQ(m_slots[0].timer, -1);
    EXPECT_EQ(m_slots[1].timer, -1);
    EXPECT_EQ(m_slots[3].timer, 0);
    EXPECT_EQ(m_services.calls, (std::vector<std::string>{"move", "rng", "rng", "final", "move", "rng", "final", "move",
                                                          "rng", "final"}));
}
TEST_F(FishLifecycleTest, ModelSelectorIsStoredAndPostModelTypeControlsMovement)
{
    for (auto& f : m_slots)
    {
        f.active = 1;
        f.type = -2;
        f.timer = 0;
    }
    m_services.hook = [&](const std::string& name)
    {
        if (name == "model")
            for (auto& f : m_slots)
                f.type = 0xb1;
    };
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_services.modelAction, 21);
    EXPECT_EQ(m_services.modes.front(), -2);
    EXPECT_EQ(m_services.calls, (std::vector<std::string>{"lookup", "model", "move", "final", "lookup", "model", "move",
                                                          "final", "lookup", "model", "move", "final"}));
    EXPECT_EQ(m_slots[0].timer, -1);
}
TEST_F(FishLifecycleTest, FullMovementUsesLiveHeightStoresAndContinuesAfterDeath)
{
    for (auto& f : m_slots)
    {
        f.active = 1;
        f.type = 0xb2;
        f.timer = 2;
        f.counter = 1;
    }
    m_services.random = {0, 0, 0};
    m_services.rotated = {2, 3, 4};
    m_services.hook = [&](const std::string& name)
    {
        if (name == "height")
            m_slots[0].position.x = 100;
    };
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_slots[0].active, 0);
    EXPECT_EQ(m_slots[0].counter, 2);
    EXPECT_EQ(m_slots[0].angles.z, 186);
    EXPECT_EQ(m_slots[0].position.z, 40);
    EXPECT_EQ(m_slots[0].aheadX, 106);
    EXPECT_EQ(m_slots[0].aheadY, 14);
    EXPECT_EQ(m_slots[0].previousPosition.x, 100);
    EXPECT_EQ(m_slots[0].previousPosition.z, 40);
    EXPECT_EQ(m_slots[0].timer, 1);
    EXPECT_EQ(m_services.cursor, 3U);
    EXPECT_EQ(m_services.calls[2], "move");
    EXPECT_EQ(m_services.calls[3], "steer");
    EXPECT_EQ(m_services.calls[4], "matrix");
    EXPECT_EQ(m_services.calls[5], "rng");
    EXPECT_EQ(m_services.calls[6], "rotate");
    EXPECT_EQ(m_services.calls[7], "height");
    EXPECT_EQ(m_services.calls.back(), "final");
}
TEST_F(FishLifecycleTest, FinalizerMapMutationExtendsVisitedPrefix)
{
    for (auto& f : m_slots)
    {
        f.active = 1;
        f.type = 0xb1;
        f.timer = 0;
    }
    m_services.hook = [&](const std::string& name)
    {
        if (name == "final")
            m_map = 8;
    };
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_services.calls.size(), 40U);
    EXPECT_EQ(m_slots[9].timer, -1);
}
TEST_F(FishLifecycleTest, RequiredServiceErrorPreservesOnlyExecutedPrefix)
{
    m_slots[0].active = 1;
    m_slots[0].type = -1;
    m_slots[0].timer = 0;
    m_services.random = {0, 77};
    m_services.failure = "final";
    auto r = Step();
    ASSERT_FALSE(r.HasValue());
    EXPECT_EQ(r.Error(), std::make_error_code(std::errc::io_error));
    EXPECT_EQ(m_slots[0].timer, 77);
    EXPECT_EQ(m_slots[1].timer, 11);
    EXPECT_EQ(m_services.cursor, 2U);
    EXPECT_EQ(m_services.calls, (std::vector<std::string>{"move", "rng", "rng", "final"}));
}
TEST_F(FishLifecycleTest, SpawnDispatchAllMapFamiliesAndSignedDefault)
{
    struct Row
    {
        std::int32_t map;
        std::int16_t type;
        std::int32_t timer;
        std::size_t draws;
    };
    const std::array<Row, 18> rows{{{0, 0xb2, 2, 6},
                                    {1, 0xb0, 2, 6},
                                    {6, 0xb1, 2, 5},
                                    {7, 0xb5, 2, 10},
                                    {8, 0xb2, 99, 6},
                                    {33, 0xb2, 99, 6},
                                    {34, 0x148, 99, 7},
                                    {24, -1, 69, 6},
                                    {25, -1, 69, 6},
                                    {26, -1, 69, 6},
                                    {27, -1, 69, 6},
                                    {28, -1, 69, 6},
                                    {29, -1, 69, 6},
                                    {36, -1, 69, 6},
                                    {2, 0xb1, 2, 5},
                                    {54, 0xb1, 2, 5},
                                    {35, 0xb1, 2, 5},
                                    {-1, 0xb1, 2, 5}}};
    for (const auto& row : rows)
    {
        SCOPED_TRACE(row.map);
        m_map = row.map;
        m_services = Services{};
        m_services.gate = 0x80;
        m_surfaces.fill(5);
        m_attributes.fill(0);
        for (auto& f : m_slots)
        {
            f = Storage{};
            f.active = 1;
            f.type = 0xb1;
            f.timer = 0;
        }
        m_slots[0].active = 0;
        m_services.random = {512, 512, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        bool observed = false;
        m_services.hook = [&](const std::string& name)
        {
            if (name != "final" || observed)
                return;
            observed = true;
            EXPECT_EQ(m_slots[0].type, row.type);
            EXPECT_EQ(m_slots[0].timer, row.timer);
            EXPECT_EQ(m_services.cursor, row.draws);
            EXPECT_EQ(m_slots[0].active, 1);
            EXPECT_EQ(m_slots[0].field58, -1);
            EXPECT_EQ(m_slots[0].field174, 0);
            EXPECT_EQ(m_slots[0].light.x, 0.5f);
            EXPECT_EQ(m_slots[0].field8f, 1);
            m_map = 2;
        };
        ASSERT_TRUE(Step().HasValue());
        EXPECT_TRUE(observed);
    }
}
TEST_F(FishLifecycleTest, Map34FlagOneConsumesOnlyCommonDrawsAndSkipsUpdate)
{
    m_map = 34;
    m_flag = 1;
    m_services.gate = 0;
    for (auto& f : m_slots)
    {
        f.active = 1;
        f.type = 0xb1;
        f.timer = 0;
    }
    m_slots[0].active = 0;
    m_services.random = {512, 512, 3, 0};
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_services.cursor, 4U);
    EXPECT_EQ(m_slots[0].active, 0);
    EXPECT_EQ(m_slots[0].type, 0xb1);
    EXPECT_EQ(m_slots[0].timer, 3);
    EXPECT_EQ(m_services.calls[5], "rng");
    EXPECT_EQ(m_services.calls[7], "lookup");
}
TEST_F(FishLifecycleTest, OscillationRetainsProductUntilFinalFloatStore)
{
    for (auto& f : m_slots)
    {
        f.active = 1;
        f.type = 0xb9;
        f.timer = 2;
        f.counter = 0;
    }
    m_services.random = {0, 0, 0};
    m_services.product = 0.2500000298023225;
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_slots[0].oscillation, 0.7500000596046448f);
    EXPECT_EQ(m_slots[0].phase, static_cast<float>(16.0 + static_cast<double>(0.1f)));
    EXPECT_EQ(m_services.calls[2], "sineProduct");
    EXPECT_EQ(m_services.calls[3], "move");
}
TEST_F(FishLifecycleTest, SoundIgnoresNativeNegativeResultAndSeesInactiveSlot)
{
    for (auto& f : m_slots)
    {
        f.active = 1;
        f.type = 0xb0;
        f.timer = 2;
        f.counter = 1;
    }
    m_attributes.fill(8);
    m_services.random = {0, 0, 0, 0, 0, 0};
    bool heard = false;
    m_services.hook = [&](const std::string& name)
    {
        if (name == "sound")
        {
            heard = true;
            EXPECT_EQ(m_slots[0].active, 0);
        }
    };
    ASSERT_TRUE(Step().HasValue());
    EXPECT_TRUE(heard);
    EXPECT_EQ(m_slots[0].timer, 1);
    ASSERT_EQ(m_services.retainedContexts.size(), 3U);
    for (std::size_t i = 0; i < 3; ++i)
        EXPECT_EQ(m_services.retainedContexts[i], &m_slots[i]);
}
TEST_F(FishLifecycleTest, TileWrapsBothCoordinatesIndependently)
{
    EXPECT_EQ(FishTileIndex(-1, 0), 255U);
    EXPECT_EQ(FishTileIndex(0, -1), 65280U);
    EXPECT_EQ(FishTileIndex(-1, -1), 65535U);
    EXPECT_EQ(FishTileIndex(256, 256), 0U);
}

TEST_F(FishLifecycleTest, SteeringMutatesLaterCanonicalSlotBeforeItsVisit)
{
    for (auto& f : m_slots)
    {
        f.active = 1;
        f.type = -1;
        f.timer = 2;
    }
    m_services.random = {0, 1};
    m_services.hook = [&](const std::string& name)
    {
        if (name == "steer")
        {
            m_slots[1].type = 0xb1;
            m_slots[1].timer = 0;
            m_slots[2].timer = 0;
        }
    };
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_slots[1].timer, -1);
    EXPECT_EQ(m_slots[2].timer, -1);
    EXPECT_EQ(m_services.cursor, 2U);
    EXPECT_EQ(m_services.calls,
              (std::vector<std::string>{"move", "steer", "matrix", "rng", "rotate", "height", "convert", "convert",
                                        "gate", "final", "lookup", "model", "move", "final", "move", "rng", "final"}));
}
TEST_F(FishLifecycleTest, SpawnReadsHeroAgainAfterEachRandomDraw)
{
    m_services.random = {512, 512, 512, 512, 512, 512};
    m_services.hook = [&](const std::string& name)
    {
        if (name == "rng")
            m_position += glm::vec3(100, 200, 300);
    };
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_slots[0].position.x, 100);
    EXPECT_EQ(m_slots[0].position.y, 400);
    EXPECT_EQ(m_slots[0].position.z, 600);
    EXPECT_EQ(m_slots[2].position.x, 500);
    EXPECT_EQ(m_slots[2].position.y, 1200);
}
TEST_F(FishLifecycleTest, EffectChangingMapRunsKalimaSupplementWithoutEarlyCopy)
{
    m_map = 8;
    m_services.gate = 0x80;
    m_services.random = {512, 512, 3, 0, 0, 1, 0};
    for (auto& f : m_slots)
    {
        f.active = 1;
        f.type = 0xb1;
        f.timer = 0;
    }
    m_slots[0].active = 0;
    m_services.hook = [&](const std::string& name)
    {
        if (name == "effect1560")
        {
            m_map = 24;
            m_slots[0].position.z = 91;
        }
        if (name == "effect1552")
        {
            EXPECT_EQ(m_slots[0].previousPosition.z, 0);
            EXPECT_EQ(m_slots[0].position.z, 91);
        }
        if (name == "final")
            m_map = 2;
    };
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_slots[0].type, -1);
    EXPECT_EQ(m_slots[0].timer, 69);
    EXPECT_EQ(m_services.cursor, 7U);
}
TEST_F(FishLifecycleTest, MapSevenUpperBranchInitializesOscillationBeforeSameStepUpdate)
{
    m_map = 7;
    m_position.y = 12801;
    m_attributes.fill(0);
    for (auto& f : m_slots)
    {
        f.active = 1;
        f.type = 0xb1;
        f.timer = 0;
    }
    m_slots[0].active = 0;
    m_services.random = {512, 512, 3, 0, 0, 1, 0, 31, 149, 0};
    m_services.hook = [&](const std::string& name)
    {
        if (name == "final")
            m_map = 2;
    };
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_slots[0].type, 0xba);
    EXPECT_EQ(m_slots[0].field64, 0);
    EXPECT_EQ(m_slots[0].oscillation, 0.75f);
    EXPECT_FLOAT_EQ(m_slots[0].phase, 3.2f);
    EXPECT_EQ(m_services.cursor, 10U);
    EXPECT_EQ(m_slots[0].position.z, 40);
}
TEST_F(FishLifecycleTest, ReachedSineErrorLeavesPhaseAndEarlierModelSideEffects)
{
    m_slots[0].active = 1;
    m_slots[0].type = 0xb9;
    m_services.failure = "sineProduct";
    auto r = Step();
    ASSERT_FALSE(r.HasValue());
    EXPECT_EQ(m_slots[0].phase, 16);
    EXPECT_EQ(m_slots[0].oscillation, 15);
    EXPECT_EQ(m_services.modelAction, 21);
    EXPECT_EQ(m_services.calls, (std::vector<std::string>{"lookup", "model", "sineProduct"}));
}
TEST_F(FishLifecycleTest, DirectAdmissionShortCircuitsFallbackAndSurfaceNeedsNoAttributes)
{
    m_map = 0;
    m_surfaces.fill(5);
    m_services.failure = "gate";
    m_services.random = {512, 512, 3, 0, 0, 0};
    for (auto& f : m_slots)
    {
        f.active = 1;
        f.type = 0xb1;
        f.timer = 0;
    }
    m_slots[0].active = 0;
    auto r = Step();
    ASSERT_FALSE(r.HasValue()); // gate recién se alcanza durante colisión.
    EXPECT_EQ(m_slots[0].type, 0xb2);
    EXPECT_EQ(m_slots[0].active, 1);
    EXPECT_EQ(m_slots[0].timer, 3);
    EXPECT_EQ(m_services.calls.back(), "gate");
    EXPECT_EQ(m_services.cursor, 6U);
}
TEST_F(FishLifecycleTest, RejectedAdmissionBypassesUpdateEvenIfGateActivatesSlot)
{
    m_attributes.fill(8);
    m_services.random = {512, 512, 512, 512, 512, 512};
    m_services.hook = [&](const std::string& name)
    {
        if (name == "gate")
            m_slots[0].active = 0x80;
    };
    auto result = Step();
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(m_slots[0].timer, 11);
    EXPECT_EQ(m_services.cursor, 6U);
    EXPECT_EQ(m_services.calls,
              (std::vector<std::string>{"rng", "rng", "convert", "convert", "gate", "rng", "rng", "convert", "convert",
                                        "gate", "rng", "rng", "convert", "convert", "gate"}));
}

TEST_F(FishLifecycleTest, ContextRetainedByEffectsSurvivesTemporaryViewsAndRequests)
{
    m_map = 8;
    m_services.gate = 0x80;
    m_services.random = {512, 512, 3, 0, 0, 0};
    for (auto& f : m_slots)
    {
        f.active = 1;
        f.type = 0xb1;
        f.timer = 0;
    }
    m_slots[0].active = 0;
    m_services.hook = [&](const std::string& name)
    {
        if (name == "final")
            m_map = 2;
    };
    ASSERT_TRUE(Step().HasValue());
    ASSERT_EQ(m_services.retainedContexts.size(), 1U);
    EXPECT_EQ(m_services.retainedContexts[0], &m_slots[0]);
    ASSERT_NE(m_services.retainedContexts[0], nullptr);
    auto* retained = static_cast<Storage*>(m_services.retainedContexts[0]);
    retained->field174 = 0.375f;
    EXPECT_EQ(m_slots[0].field174, 0.375f);
}
TEST_F(FishLifecycleTest, NegativeRandomUsesSignedRemainderAndConversionOrderIsYX)
{
    m_services.random = {-1, -1025, 0, 0, 0, 0};
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_slots[0].position.x, -513);
    EXPECT_EQ(m_slots[0].position.y, -513);
    ASSERT_GE(m_services.numeric.size(), 2U);
    EXPECT_FLOAT_EQ(m_services.numeric[0], -5.13f);
    EXPECT_FLOAT_EQ(m_services.numeric[1], -5.13f);
}

TEST_F(FishLifecycleTest, DistanceEqualityIsFarAt1500AndSilentAt600)
{
    for (const float distance : std::array<float, 2>{600, 1500})
    {
        m_services = Services{};
        m_services.random = {0, 0, 0};
        for (auto& f : m_slots)
        {
            f = Storage{};
            f.active = 1;
            f.type = 0xb0;
            f.timer = 2;
            f.counter = 0;
            f.position = {distance, 0, 0};
        }
        ASSERT_TRUE(Step().HasValue());
        EXPECT_EQ(m_services.cursor, 3U);
        EXPECT_EQ(m_slots[0].active, distance == 1500 ? 0 : 1);
    }
}
TEST_F(FishLifecycleTest, TimerDecrementWrapsAsSignedDwordWithoutResetWhenPositive)
{
    for (auto& f : m_slots)
    {
        f.active = 1;
        f.type = -1;
        f.timer = std::numeric_limits<std::int32_t>::min();
    }
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_services.cursor, 0U);
    EXPECT_EQ(m_slots[0].timer, std::numeric_limits<std::int32_t>::max());
}

TEST_F(FishLifecycleTest, AsymmetricSpawnConversionArgumentsAreYX)
{
    m_services.random = {-1, -1026, 0, 0, 0, 0};
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(m_slots[0].position.x, -513);
    EXPECT_EQ(m_slots[0].position.y, -514);
    ASSERT_GE(m_services.conversions.size(), 2U);
    // Oracle de fracciones exactas: -514/-513 multiplicados por F32bits3c23d70a, sin store F32.
    EXPECT_EQ(std::bit_cast<std::uint64_t>(m_services.conversions[0]), 0xc0148f5c21400000ULL);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(m_services.conversions[1]), 0xc014851eb0a00000ULL);
}
TEST_F(FishLifecycleTest, AsymmetricMovementConversionArgumentsAreYX)
{
    for (auto& fish : m_slots)
    {
        fish.active = 1;
        fish.type = -1;
        fish.timer = 2;
        fish.position = {123, 456, 0};
    }
    m_services.rotated = {10, 20, 0};
    m_services.random = {0, 0, 0};
    ASSERT_TRUE(Step().HasValue());
    ASSERT_GE(m_services.conversions.size(), 2U);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(m_services.conversions[0]), 0x40130a3d69800000ULL);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(m_services.conversions[1]), 0x3ff547ae0c800000ULL);
}
TEST_F(FishLifecycleTest, SpawnFamiliesHaveExactNumericOutputsAndEffectPayloads)
{
    struct Row
    {
        std::int32_t map;
        bool upper;
        std::uint32_t scaleBits;
        std::uint32_t speedBits;
        std::uint32_t turnBits;
        std::uint32_t field170Bits;
        std::uint32_t modelSpeedBits;
        std::uint32_t rotateLengthBits;
        std::int32_t mode;
        bool hasModel;
        std::int32_t effect;
        std::int32_t subtype;
        std::uint32_t effectScaleBits;
    };
    // Valores independientes de447030 con RNG explícito, literales F32 y perfil PC53/RN.
    // Oracle privado reproduce operaciones por fracciones exactas; no se consulta el SUT para expected.
    const std::array<Row, 19> rows{{{0, false, 0x3f333333U, 0x3f5b6db8U, 0x41500000U, 0x3e99999aU, 0x3edb6db8U,
                                     0x40c00001U, 0, true, 0x0, 0, 0x00000000U},
                                    {1, false, 0x3f333333U, 0x3f5b6db8U, 0x41500000U, 0x3f800000U, 0x3edb6db8U,
                                     0x40f6db6fU, 0, true, 0x0, 0, 0x00000000U},
                                    {6, false, 0x3f333333U, 0x3f5b6db8U, 0x41500000U, 0x3f800000U, 0x3edb6db8U,
                                     0x40f6db6fU, 1, true, 0x0, 0, 0x00000000U},
                                    {7, false, 0x3f666667U, 0x3f8e38e3U, 0x40400000U, 0x3f800000U, 0x3f0e38e3U,
                                     0x410e38e3U, 0, true, 0x0, 0, 0x00000000U},
                                    {8, false, 0x3f8ccccdU, 0x4011745dU, 0x41100000U, 0x3f800000U, 0x3f91745dU,
                                     0x417e8ba3U, 0, true, 0x618, 4, 0x41f00000U},
                                    {33, false, 0x3f8ccccdU, 0x4011745dU, 0x41100000U, 0x3f800000U, 0x3f91745dU,
                                     0x417e8ba3U, 0, true, 0x618, 4, 0x41f00000U},
                                    {34, false, 0x3f8ccccdU, 0x3f0ba2e9U, 0x3f800000U, 0x3f800000U, 0x3e8ba2e9U,
                                     0x40745d18U, 1, true, 0x697, 0, 0x41f00000U},
                                    {24, false, 0x3f8ccccdU, 0x4011745dU, 0x41100000U, 0x3f800000U, 0x3f91745dU,
                                     0x417e8ba3U, 0, false, 0x610, 8, 0x42480000U},
                                    {25, false, 0x3f8ccccdU, 0x4011745dU, 0x41100000U, 0x3f800000U, 0x3f91745dU,
                                     0x417e8ba3U, 0, false, 0x610, 8, 0x42480000U},
                                    {26, false, 0x3f8ccccdU, 0x4011745dU, 0x41100000U, 0x3f800000U, 0x3f91745dU,
                                     0x417e8ba3U, 0, false, 0x610, 8, 0x42480000U},
                                    {27, false, 0x3f8ccccdU, 0x4011745dU, 0x41100000U, 0x3f800000U, 0x3f91745dU,
                                     0x417e8ba3U, 0, false, 0x610, 8, 0x42480000U},
                                    {28, false, 0x3f8ccccdU, 0x4011745dU, 0x41100000U, 0x3f800000U, 0x3f91745dU,
                                     0x417e8ba3U, 0, false, 0x610, 8, 0x42480000U},
                                    {29, false, 0x3f8ccccdU, 0x4011745dU, 0x41100000U, 0x3f800000U, 0x3f91745dU,
                                     0x417e8ba3U, 0, false, 0x610, 8, 0x42480000U},
                                    {36, false, 0x3f8ccccdU, 0x4011745dU, 0x41100000U, 0x3f800000U, 0x3f91745dU,
                                     0x417e8ba3U, 0, false, 0x610, 8, 0x42480000U},
                                    {2, false, 0x3f333333U, 0x3f800000U, 0x41500000U, 0x3f800000U, 0x3f000000U,
                                     0x41100000U, 1, true, 0x0, 0, 0x00000000U},
                                    {54, false, 0x3f333333U, 0x3f800000U, 0x41500000U, 0x3f800000U, 0x3f000000U,
                                     0x41100000U, 1, true, 0x0, 0, 0x00000000U},
                                    {35, false, 0x3f333333U, 0x3f800000U, 0x41500000U, 0x3f800000U, 0x3f000000U,
                                     0x41100000U, 1, true, 0x0, 0, 0x00000000U},
                                    {-1, false, 0x3f333333U, 0x3f800000U, 0x41500000U, 0x3f800000U, 0x3f000000U,
                                     0x41100000U, 1, true, 0x0, 0, 0x00000000U},
                                    {7, true, 0x3f666667U, 0x3f0e38e3U, 0x40000000U, 0x3f800000U, 0x3e8e38e3U,
                                     0x408e38e3U, 0, true, 0x0, 0, 0x00000000U}}};
    for (const auto& row : rows)
    {
        SCOPED_TRACE(row.map);
        SCOPED_TRACE(row.upper);
        m_map = row.map;
        m_position = {100, row.upper ? 12801.0f : 200.0f, 300};
        m_services = Services{};
        m_services.gate = 1;
        m_surfaces.fill(5);
        m_attributes.fill(0);
        for (auto& fish : m_slots)
        {
            fish = Storage{};
            fish.active = 1;
            fish.type = 0xb1;
            fish.timer = 0;
        }
        m_slots[0].active = 0;
        m_services.random = {512, 512, 3, 3, 3, 5, 1, 7, 11, 2, 0, 0};
        bool observed = false;
        m_services.hook = [&](const std::string& name)
        {
            if (name != "final" || observed)
                return;
            observed = true;
            EXPECT_EQ(std::bit_cast<std::uint32_t>(m_slots[0].scale), row.scaleBits);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(m_slots[0].speed), row.speedBits);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(m_slots[0].turnRate), row.turnBits);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(m_slots[0].field170), row.field170Bits);
            EXPECT_EQ(m_services.modelSpeeds.size(), row.hasModel ? 1U : 0U);
            if (!m_services.modelSpeeds.empty())
            {
                EXPECT_EQ(std::bit_cast<std::uint32_t>(m_services.modelSpeeds[0]), row.modelSpeedBits);
            }
            ASSERT_EQ(m_services.rotateLengths.size(), 1U);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(m_services.rotateLengths[0]), row.rotateLengthBits);
            ASSERT_EQ(m_services.movementModes.size(), 1U);
            EXPECT_EQ(m_services.movementModes[0], row.mode);
            EXPECT_EQ(m_services.effects.size(), row.effect != 0 ? 1U : 0U);
            if (!m_services.effects.empty())
            {
                const auto& effect = m_services.effects[0];
                EXPECT_EQ(effect.type, row.effect);
                EXPECT_EQ(effect.subtype, row.subtype);
                EXPECT_EQ(effect.scaleBits, row.effectScaleBits);
                EXPECT_EQ(effect.firstPosition, &m_slots[0].position);
                EXPECT_EQ(effect.secondPosition, &m_slots[0].position);
                EXPECT_EQ(effect.angles, &m_slots[0].angles);
                EXPECT_EQ(effect.owner, &m_slots[0]);
            }
        };
        ASSERT_TRUE(Step().HasValue());
        EXPECT_TRUE(observed);
    }
}
} // namespace
