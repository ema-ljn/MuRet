#include "CastleSiege/CastleBattleActors.h"

#include "Kernel/ActorState.h"
#include "Guild/GuildState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <span>

namespace
{
class CastleBattleActorsTest : public ::testing::Test
{
protected:
    using Actors = std::array<Client::Kernel::ActorState, Client::Scenes::CastleBattleActorCount + 1>;
    std::unique_ptr<Actors> m_actors = std::make_unique<Actors>();
    std::unique_ptr<Client::Scenes::GuildState> m_guild = std::make_unique<Client::Scenes::GuildState>();

    void ChangeOwner()
    {
        const auto changed = Client::Scenes::ApplyTheCastleOwner(
            "Pinos", std::span(*m_actors).first<Client::Scenes::CastleBattleActorCount>(), *m_guild);
        ASSERT_TRUE(changed.HasValue()) << changed.Error();
    }
};

// 42aea2 lee tabla+4, la alianza; guildName(+d) no decide el dueño.
TEST_F(CastleBattleActorsTest, TheOwnerMatchesTheUnionAndKeepsUnrelatedFlags)
{
    m_guild->table[0].unionName = {'P', 'i', 'n', 'o', 's'};
    m_guild->table[1].guildName = {'P', 'i', 'n', 'o', 's'};
    auto& owner = (*m_actors)[0];
    owner.alive = owner.seen = true;
    owner.word1f2 = 0;
    owner.flags78 = 0xc80001;
    auto& former = (*m_actors)[1];
    former.alive = former.seen = true;
    former.word1f2 = 1;
    former.flags78 = 0x440002;
    ChangeOwner();
    EXPECT_EQ(owner.flags78, 0x40001u);
    EXPECT_EQ(owner.byte360, 12);
    EXPECT_EQ(former.flags78, 0x480002u);
    EXPECT_EQ(former.byte360, 6);
}

TEST_F(CastleBattleActorsTest, EveryAliveSeenActorLosesItsLayerBeforeTheComparison)
{
    for (std::size_t index = 0; index < m_actors->size(); ++index)
    {
        auto& actor = (*m_actors)[index];
        actor.alive = actor.seen = true;
        actor.layer.emplace();
        actor.byte360 = 5;
        actor.word1f2 = 0;
        actor.flags78 = 0x800000;
    }
    (*m_actors)[0].alive = false;
    (*m_actors)[1].seen = false;
    ChangeOwner();
    EXPECT_TRUE((*m_actors)[0].layer.has_value());
    EXPECT_TRUE((*m_actors)[1].layer.has_value());
    EXPECT_TRUE(m_actors->back().layer.has_value());
    for (std::size_t index = 2; index < Client::Scenes::CastleBattleActorCount; ++index)
    {
        EXPECT_FALSE((*m_actors)[index].layer.has_value());
        EXPECT_EQ((*m_actors)[index].byte360, 8);
    }
}

// El alias−1 de787aa4c apunta al nombre del miembro74, no a una cadena vacía inventada.
TEST_F(CastleBattleActorsTest, TheNegativeOneGuildIndexReadsTheMemberAlias)
{
    auto& actor = (*m_actors)[0];
    actor.alive = actor.seen = true;
    actor.word1f2 = 0xffff;
    m_guild->members[Client::Scenes::MemberBeforeTheTable].name = {'P', 'i', 'n', 'o', 's'};
    ChangeOwner();
    EXPECT_EQ(actor.byte360, 12);
    EXPECT_EQ(actor.flags78, 0x40000u);
}
} // namespace
