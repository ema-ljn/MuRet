#include "Scenes/ActionReply.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Protocol/Movement.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActionSend.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Emotes.h"
#include "World/GameCamera.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SentActions.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Scenes::ApplyActionReply: el 18 de otro personaje (0x4cb750; 04 § La acción, 18). Las claves, las clases y las
// posiciones son inventadas; la tabla esperada sale de los bloques del ASM (0x4cb812..0x4cbcdb), no de una corrida.
namespace
{
using Client::Scenes::ActorState;

constexpr std::uint16_t OtherKey = 0x1234;
constexpr std::size_t OtherSlot = 12;
constexpr std::uint8_t ElfClass = 2;
// Un tipo que no es jugador, con diez acciones.
constexpr std::uint16_t SmallModel = 0x20;
constexpr std::uint32_t SittingFlag = 0x200000;

class ReplyModels final : public Client::Scenes::IModelLibrary
{
public:
    ReplyModels()
    {
        small.actions.resize(10);
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == Client::Data::Models::PlayerSkeletonModel)
            return &player;
        return model == SmallModel ? &small : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton player = Client::Tests::PlayerActionsSkeleton();
    Client::Render::Models::Skeleton small;
};

// 0x4cb871..0x4cbcc8: la animación de cada acción para las otras clases y para el elfo.
struct TableRow
{
    std::uint8_t action;
    std::uint8_t other;
    std::uint8_t elf;
};

constexpr std::array<TableRow, 28> TableAnimations{{
    {0x80, 0xc5, 0xc7}, {0x81, 0xcb, 0xcc}, {0x82, 0xc9, 0xca}, {0x83, 0x99, 0x9a}, {0x84, 0x9b, 0x9c},
    {0x85, 0x9d, 0x9e}, {0x86, 0xa3, 0xa4}, {0x87, 0xa1, 0xa2}, {0x88, 0xa5, 0xa6}, {0x89, 0xa7, 0xa8},
    {0x8a, 0x9f, 0xa0}, {0x8b, 0xa9, 0xaa}, {0x8c, 0xab, 0xac}, {0x8d, 0xad, 0xae}, {0x8e, 0xaf, 0xb0},
    {0x8f, 0xb1, 0xb2}, {0x90, 0xb3, 0xb4}, {0x91, 0xb5, 0xb6}, {0x92, 0xb7, 0xb7}, {0x93, 0xb8, 0xb8},
    {0x94, 0xc0, 0xc0}, {0x95, 0xb9, 0xb9}, {0x96, 0xba, 0xba}, {0x97, 0xbb, 0xbb}, {0x98, 0xbc, 0xbc},
    {0x99, 0xbd, 0xbd}, {0x9a, 0xbe, 0xbe}, {0x9b, 0xbf, 0xbf},
}};

// La animación que 0x4cb750 le pide a 0x4ea030 para `action`.
std::uint8_t ExpectedAnimation(std::uint8_t action, std::uint8_t characterClass)
{
    if (action == 0x12)
        return 0x98;
    if (action == 0x7e)
        return 5;
    const auto found = std::ranges::find(TableAnimations, action, &TableRow::action);
    if (found == TableAnimations.end())
        return action;
    return (characterClass & 7) == ElfClass ? found->elf : found->other;
}

class ActionReplyTest : public ::testing::Test
{
protected:
    ActionReplyTest()
    {
        m_world->Runtime().map = 0;
        Other() = Client::Tests::UnarmedPlayer();
        Other().alive = true;
        Other().key = OtherKey;
        Other().cellX = 11;
        Other().cellY = 12;
        Other().targetX = 13;
        Other().targetY = 14;
        Other().position = {1.0f, 2.0f, 3.0f};
        Other().movement = 1;
    }

    ActorState& Other()
    {
        return m_world->Actors()[OtherSlot];
    }

    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = *m_world,
                .selection = m_selection,
                .dataRoot = {},
                .cloth = m_cloth,
                .camera = m_camera,
                .cameraInput = m_input,
                .models = m_models,
                .log = m_log};
    }

    Client::Core::Expected<void, std::string> Apply(std::uint8_t direction, std::uint8_t action,
                                                    std::uint16_t key = OtherKey)
    {
        Client::Scenes::ActionSender send{};
        if (m_withSender)
            send = Client::Tests::RecordingActions(m_sent);
        return Client::Scenes::ApplyActionReply(
            Client::Protocol::Movement::Action{.key = key, .direction = direction, .action = action}, Resources(),
            send);
    }

    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    Client::Scenes::SelectionState m_selection{*m_world};
    Client::World::GameCamera m_camera;
    Client::World::GameCameraInput m_input;
    Client::Render::Models::ClothWorld m_cloth;
    ReplyModels m_models;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    bool m_withSender = true;
    std::vector<Client::Tests::SentAction> m_sent;
};

// 0x4cb7fd..0x4cbcdb: cada acción que no va a 0x4f2250 ni a 0x4f4a10, con cada clase: la animación de su bloque (las
// que el modelo no tiene, 205 o más, no se ponen), el sonido 0x6f solo con la 0x12 y ningún rand().
TEST_F(ActionReplyTest, EveryActionWithEveryClass)
{
    const auto seed = m_cloth.random.State();
    for (const std::uint8_t characterClass :
         {std::uint8_t{0}, std::uint8_t{1}, ElfClass, std::uint8_t{3}, std::uint8_t{0x12}, std::uint8_t{0x21}})
    {
        for (int value = 0; value <= 0xff; ++value)
        {
            const auto action = static_cast<std::uint8_t>(value);
            if (action >= 0x78 && action <= 0x7b)
                continue;
            SCOPED_TRACE(testing::Message() << "clase " << int{characterClass} << ", acción " << value);
            Other().characterClass = characterClass;
            Other().animation.action = 0;
            Other().movement = 1;
            m_world->Runtime().lastSoundId.reset();
            ASSERT_TRUE(Apply(2, action).HasValue());
            const std::uint8_t animation = ExpectedAnimation(action, characterClass);
            EXPECT_EQ(Other().animation.action, animation < Client::Tests::PlayerActionCount ? animation : 0);
            EXPECT_EQ(Other().movement, 0);
            EXPECT_EQ(Other().angles.z, 45.0f);
            EXPECT_EQ(m_world->Runtime().lastSoundId,
                      action == 0x12 ? std::optional<std::int32_t>{0x6f} : std::nullopt);
        }
    }
    EXPECT_EQ(m_cloth.random.State(), seed);
    EXPECT_TRUE(m_sent.empty());
}

// Lo que los demás ven de un emote (la acción de su C1 18) es la animación que el que lo hizo se puso (0x576a60 con el
// +1 del elfo salvo de 0xb7 a 0xc0), en los 22 casos de 0x576eb0.
TEST_F(ActionReplyTest, TheEmoteLooksTheSameFromOutside)
{
    const std::vector<Client::UI::EmoteCase> cases = {
        {0x99, 0x83}, {0x9b, 0x84}, {0x9d, 0x85}, {0xa3, 0x86}, {0xa1, 0x87}, {0xa5, 0x88}, {0xa9, 0x8b}, {0xa7, 0x89},
        {0xab, 0x8c}, {0xaf, 0x8e}, {0x9f, 0x8a}, {0xad, 0x8d}, {0xb1, 0x8f}, {0xb3, 0x90}, {0xb5, 0x91}, {0xb7, 0x92},
        {0xb8, 0x93}, {0xc0, 0x94}, {0xbc, 0x98}, {0xbd, 0x99}, {0xbf, 0x9b}, {0xbe, 0x9a},
    };
    for (const std::uint8_t characterClass : {std::uint8_t{0}, ElfClass})
    {
        for (const auto& [animation, action] : cases)
        {
            SCOPED_TRACE(testing::Message() << "clase " << int{characterClass} << ", acción " << int{action});
            Other().characterClass = characterClass;
            ASSERT_TRUE(Apply(1, action).HasValue());
            const bool shared = animation >= 0xb7 && animation <= 0xc0;
            const auto played =
                static_cast<std::uint8_t>(characterClass == ElfClass && !shared ? animation + 1 : animation);
            EXPECT_EQ(Other().animation.action, played);
        }
    }
}

// 0x4cb794..0x4cb7fa: el ángulo es (dirección − 1) · 45 sin envolver, y la X y la Y van al centro de la celda blanco
// (+0x39a y +0x39b), no a la lógica, para cualquier acción (Q-C779). La Z y las celdas no se tocan.
TEST_F(ActionReplyTest, TheAngleComesFromTheDirectionAndThePositionSnapsToTheTarget)
{
    struct Turn
    {
        std::uint8_t direction;
        float angle;
    };
    const std::vector<Turn> angles = {{0, -45.0f}, {1, 0.0f}, {3, 90.0f}, {8, 315.0f}, {9, 360.0f}, {0xff, 11430.0f}};
    for (const auto& [direction, angle] : angles)
    {
        SCOPED_TRACE(int{direction});
        ASSERT_TRUE(Apply(direction, 0x7e).HasValue());
        EXPECT_EQ(Other().angles.z, angle);
    }
    EXPECT_EQ(Other().position, glm::vec3(1350.0f, 1450.0f, 3.0f));
    Other().targetX = 0;
    Other().targetY = 0xff;
    ASSERT_TRUE(Apply(1, 0x83).HasValue());
    EXPECT_EQ(Other().position, glm::vec3(50.0f, 25550.0f, 3.0f));
    EXPECT_EQ(Other().cellX, 11);
    EXPECT_EQ(Other().cellY, 12);
    EXPECT_EQ(Other().targetX, 0);
    EXPECT_EQ(Other().targetY, 0xff);
}

// 0x4cb781: con el caballo oscuro en +0x318 no hace nada; las otras mascotas no lo frenan.
TEST_F(ActionReplyTest, TheDarkHorseIsLeftAlone)
{
    Other().pet.model = 0x1c16;
    Other().animation.action = 1;
    ASSERT_TRUE(Apply(3, 0x83).HasValue());
    EXPECT_EQ(Other().animation.action, 1);
    EXPECT_EQ(Other().angles.z, 0.0f);
    EXPECT_EQ(Other().position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(Other().movement, 1);

    for (const std::uint16_t pet : {std::uint16_t{0x1c14}, std::uint16_t{0x1c15}, std::uint16_t{0x1c37}})
    {
        Other().pet.model = pet;
        Other().animation.action = 1;
        ASSERT_TRUE(Apply(3, 0x83).HasValue());
        EXPECT_EQ(Other().animation.action, 0x99);
    }
}

// 0x5110d0 (MOVSX): sin nadie vivo con la clave, o con una desde 0x8000, el original escribe en el lugar 400 (Q-C778);
// MuRet no toca a nadie. Con dos con la misma, el primero.
TEST_F(ActionReplyTest, AnUnknownKeyTouchesNobody)
{
    const auto before = m_world->Actors()[400].angles;
    ASSERT_TRUE(Apply(3, 0x83, 0x4321).HasValue());
    EXPECT_EQ(Other().animation.action, 0);
    EXPECT_EQ(m_world->Actors()[400].angles, before);

    Other().alive = false;
    ASSERT_TRUE(Apply(3, 0x83).HasValue());
    EXPECT_EQ(Other().animation.action, 0);

    Other().alive = true;
    Other().key = 0x8001;
    ASSERT_TRUE(Apply(3, 0x83, 0x8001).HasValue());
    EXPECT_EQ(Other().animation.action, 0);

    Other().key = OtherKey;
    ActorState& first = m_world->Actors()[5];
    first = Client::Tests::UnarmedPlayer();
    first.alive = true;
    first.key = OtherKey;
    ASSERT_TRUE(Apply(3, 0x83).HasValue());
    EXPECT_EQ(first.animation.action, 0x99);
    EXPECT_EQ(Other().animation.action, 0);
}

// 0x4cb820..0x4cb852: 0x78 y 0x79 pegan con 0x4f4a10 (sin armas, la 0x22, sin sonido: el único rand() es el cierre del
// registro propio de 0x4f3f80) y después ponen +0x381 en 1, el frame en 0 (aunque 0x4ea030 lo haya dejado en otro
// valor por seguir en la misma acción), el blanco +0x3a4 con el WORD de abajo de [0x57678b0] y [0x5767964] con el
// índice. En Lorencia y en Devias.
TEST_F(ActionReplyTest, TheStrikeAttacksAndRemembersTheAttacker)
{
    constexpr std::uint32_t OwnRecordDraws = 0xf08;
    m_world->Runtime().heroSlot = 0x10007;
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t action : {std::uint8_t{0x78}, std::uint8_t{0x79}})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", acción " << int{action});
            m_world->Runtime().map = map;
            m_world->Runtime().lastAttacker = 0;
            m_world->Runtime().lastSoundId.reset();
            Other().movement = 1;
            Other().byte381 = 0;
            Other().attackCount = 0;
            Other().animation.action = 0x22;
            Other().animation.frame = 1.5f;
            auto expected = m_cloth.random;
            expected.Discard(OwnRecordDraws);
            const auto applied = Apply(3, action);
            ASSERT_TRUE(applied.HasValue()) << applied.Error();
            EXPECT_EQ(Other().angles.z, 90.0f);
            EXPECT_EQ(Other().movement, 0);
            EXPECT_EQ(Other().position, glm::vec3(1350.0f, 1450.0f, 3.0f));
            EXPECT_EQ(Other().animation.action, 0x22);
            EXPECT_EQ(Other().animation.frame, 0.0f);
            EXPECT_EQ(Other().byte381, 1);
            EXPECT_EQ(Other().attackCount, 1);
            EXPECT_EQ(Other().attackTarget, 7);
            EXPECT_EQ(m_world->Runtime().lastAttacker, static_cast<std::int32_t>(OtherSlot));
            EXPECT_FALSE(m_world->Runtime().lastSoundId.has_value());
            EXPECT_EQ(m_cloth.random.State(), expected.State());
        }
    }
    EXPECT_TRUE(m_sent.empty());
}

// Un corte de 0x4f4a10 (el 0x27, con su efecto de la CL11) deja el ángulo, el Movement y la posición, y no escribe ni
// +0x381 ni [0x5767964].
TEST_F(ActionReplyTest, AStrikeThatCutsLeavesTheAttackerAlone)
{
    Other().type = 0x27;
    const auto applied = Apply(3, 0x78);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4f4f5b"), std::string::npos);
    EXPECT_EQ(Other().movement, 0);
    EXPECT_EQ(Other().byte381, 0);
    EXPECT_EQ(m_world->Runtime().lastAttacker, 0);
}

// 0x4cb812: 0x7a y 0x7b van a 0x4f2250, con su sufijo (un rand()); sin armas, la 1 y la 2 con el elfo, en Lorencia y
// en Devias.
TEST_F(ActionReplyTest, TheStopActionsGoThroughSetActorStop)
{
    for (const std::int32_t map : {0, 2})
    {
        m_world->Runtime().map = map;
        for (const std::uint8_t action : {std::uint8_t{0x7a}, std::uint8_t{0x7b}})
        {
            for (const std::uint8_t characterClass : {std::uint8_t{0}, ElfClass})
            {
                SCOPED_TRACE(testing::Message()
                             << "mapa " << map << ", clase " << int{characterClass} << ", acción " << int{action});
                const auto before = m_cloth.random;
                Other().characterClass = characterClass;
                Other().animation.action = 0x20;
                ASSERT_TRUE(Apply(3, action).HasValue());
                EXPECT_EQ(Other().animation.action, characterClass == ElfClass ? 2 : 1);
                auto expected = before;
                static_cast<void>(expected.Next());
                EXPECT_EQ(m_cloth.random.State(), expected.State());
            }
        }
    }
    EXPECT_TRUE(m_sent.empty());
}

// Sentado (+0x7c con 0x200000), 0x4f2250 pone la 0xca y manda el C1 18 0x82 con el ángulo que el 18 acaba de poner.
TEST_F(ActionReplyTest, ASittingActorSendsTheSitAction)
{
    Other().flags7c = SittingFlag;
    for (const std::int32_t map : {0, 2})
    {
        m_world->Runtime().map = map;
        m_sent.clear();
        Other().animation.action = 1;
        ASSERT_TRUE(Apply(3, 0x7a).HasValue());
        EXPECT_EQ(Other().animation.action, 0xca);
        EXPECT_EQ(m_sent, (std::vector<Client::Tests::SentAction>{{3, 0x82}}));
    }

    m_withSender = false;
    Other().animation.action = 1;
    const auto unsent = Apply(3, 0x7a);
    ASSERT_FALSE(unsent.HasValue());
    EXPECT_NE(unsent.Error().find("0x4f2416"), std::string::npos);
}

// 0x4ea030 mira las acciones del modelo del personaje: las que no tiene no se ponen, y sin el modelo, el error.
TEST_F(ActionReplyTest, TheAnimationUsesTheModelOfTheActor)
{
    Other().type = SmallModel;
    ASSERT_TRUE(Apply(3, 0x7e).HasValue());
    EXPECT_EQ(Other().animation.action, 5);
    ASSERT_TRUE(Apply(3, 0x83).HasValue());
    EXPECT_EQ(Other().animation.action, 5);

    Other().type = 0x99;
    const auto applied = Apply(3, 0x83);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4ea030"), std::string::npos);
}
} // namespace
