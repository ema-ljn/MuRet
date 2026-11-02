#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

// Una carpeta temporal que se crea vacía y se borra al salir del test. Con un sufijo al azar, dos
// corridas a la vez (Debug y Release, dos worktrees) no se pisan.
namespace Client::Tests
{
class TemporaryFolder
{
public:
    // `name` va en el nombre de la carpeta, para saber de qué test es si queda una.
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
