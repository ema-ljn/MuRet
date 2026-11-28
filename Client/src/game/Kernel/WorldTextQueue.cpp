#include "Kernel/WorldTextQueue.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <cstring>
#include <string_view>

namespace Client::Kernel
{
void QueueWorldText(WorldTextQueue& queue, std::string_view text, const glm::vec3& position)
{
    WorldText entry;
    std::memcpy(entry.text.data(), text.data(), std::min(text.size(), entry.text.size()));
    entry.position = position;
    queue.push_back(entry);
}
} // namespace Client::Kernel
