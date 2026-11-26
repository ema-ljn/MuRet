#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

// A temporary folder created empty and deleted when the test ends. With a random suffix, two runs at once (Debug and
// Release, two worktrees) do not collide.
namespace Client::Tests
{
class TemporaryFolder
{
public:
    // `name` goes in the folder's name, to know which test it belongs to if one is left over.
    explicit TemporaryFolder(std::string_view name)
        : m_path(std::filesystem::temp_directory_path() /
                 ("muret-" + std::string(name) + "-" + std::to_string(std::random_device{}())))
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};
} // namespace Client::Tests
