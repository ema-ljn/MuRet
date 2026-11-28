#include "support/FrameDigest.h"

#include "Kernel/WorldState.h"
#include "Chat/ChatLog.h"
#include "support/Fnv1a.h"
#include "support/RecordingFrameTarget.h"
#include "Chat/ChatRuntime.h"
#include "Ui/PanelRegistry.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace
{
using Client::Tests::DigestOf;
using Client::Tests::FrameDigest;
using Client::Tests::FrameEvent;
using Client::Tests::RecordedFrame;

RecordedFrame OneQuadAt(float x)
{
    RecordedFrame frame;
    FrameEvent quad;
    quad.vertices[0].x = x;
    frame.events.push_back(quad);
    return frame;
}

class FrameDigestTest : public ::testing::Test
{
protected:
    [[nodiscard]] FrameDigest Digest() const
    {
        return DigestOf(RecordedFrame{}, {}, 0, *m_world);
    }

    std::unique_ptr<Client::Kernel::WorldState> m_world = std::make_unique<Client::Kernel::WorldState>();
};

TEST_F(FrameDigestTest, DrawFollowsTheRecordedVertices)
{
    EXPECT_EQ(DigestOf(OneQuadAt(1.0f), {}, 0, *m_world).draw, DigestOf(OneQuadAt(1.0f), {}, 0, *m_world).draw);
    EXPECT_NE(DigestOf(OneQuadAt(1.0f), {}, 0, *m_world).draw, DigestOf(OneQuadAt(2.0f), {}, 0, *m_world).draw);
}

TEST_F(FrameDigestTest, SentIsTheFnv1aOfTheBytes)
{
    const std::vector<std::byte> sent{std::byte{0xc1}, std::byte{0x04}, std::byte{0xf4}, std::byte{0x02}};

    EXPECT_EQ(DigestOf(RecordedFrame{}, sent, 0, *m_world).sent, Client::Tests::Fnv1a64(sent));
}

TEST_F(FrameDigestTest, KeepsTheRandomState)
{
    EXPECT_EQ(DigestOf(RecordedFrame{}, {}, 0x1234abcd, *m_world).randState, 0x1234abcdU);
}

TEST_F(FrameDigestTest, StateSkipsDeadActors)
{
    const FrameDigest before = Digest();
    m_world->Actors()[7].position.x = 500.0f;
    EXPECT_EQ(Digest().state, before.state);

    m_world->Actors()[7].alive = true;
    EXPECT_NE(Digest().state, before.state);
}

TEST_F(FrameDigestTest, StateFollowsTheOwnCharacterTheMapThePanelsAndTheChat)
{
    const auto changes = [this](auto change)
    {
        const FrameDigest before = Digest();
        change();
        return Digest().state != before.state;
    };

    EXPECT_TRUE(changes([this] { m_world->OwnCharacter().life = 70; }));
    EXPECT_TRUE(changes([this] { m_world->OwnCharacter().zen = 2000; }));
    EXPECT_TRUE(changes([this] { m_world->Runtime().map = 0; }));
    EXPECT_TRUE(changes([this] { m_world->Runtime().panels.inventory = 1; }));
    EXPECT_TRUE(changes(
        [this]
        {
            Client::Chat::ChatEntry entry;
            entry.text = "hello";
            m_world->Runtime().chat.log.entries.push_front(entry);
        }));
}
} // namespace
