#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

// std::expected arrives with C++23 and MuRet compiles as C++20. Names follow rule 7 (HasValue, Value, Error). E is
// meant to be an enum class: with a numeric E, `return error;` in an Expected<long, int> silently becomes a value.
// Value() on an error, or Error() on a value, aborts with a message, also in Release: MuRet does not use exceptions for
// errors.
namespace Client::Core
{
// Wrapped, so an error differs from a value even when T and E are the same type.
template <typename E> class Unexpected
{
public:
    explicit Unexpected(E error) : m_wrapped(std::move(error)) {}

    E&& Error() &&
    {
        return std::move(m_wrapped);
    }

private:
    E m_wrapped;
};

template <typename T, typename E> class Expected;

namespace Detail
{
[[noreturn]] inline void StopOnBadAccess(const char* message)
{
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

template <typename Type, typename E> inline constexpr bool IsExpectedWithError = false;
template <typename T, typename E> inline constexpr bool IsExpectedWithError<Expected<T, E>, E> = true;

template <typename Type, typename T> inline constexpr bool IsExpectedWithValue = false;
template <typename T, typename E> inline constexpr bool IsExpectedWithValue<Expected<T, E>, T> = true;
} // namespace Detail

template <typename T, typename E> class [[nodiscard]] Expected
{
    static_assert(!std::is_reference_v<T> && !std::is_reference_v<E>, "Expected guarda valores, no referencias");

public:
    // Not explicit, on purpose: a loader does `return table;` or `return Unexpected(error);`.
    Expected(T value) : m_storage(std::in_place_index<ValueIndex>, std::move(value)) {}

    Expected(Unexpected<E> error) : m_storage(std::in_place_index<ErrorIndex>, std::move(error).Error()) {}

    [[nodiscard]] bool HasValue() const
    {
        return m_storage.index() == ValueIndex;
    }

    [[nodiscard]] T& Value() &
    {
        RequireValue();
        return std::get<ValueIndex>(m_storage);
    }

    [[nodiscard]] const T& Value() const&
    {
        RequireValue();
        return std::get<ValueIndex>(m_storage);
    }

    // Returns the moved value, not std::expected's T&&, so `const T& t = Load().Value();` does not dangle when the
    // temporary Expected dies at the end of the line.
    [[nodiscard]] T Value() &&
    {
        RequireValue();
        return std::move(std::get<ValueIndex>(m_storage));
    }

    [[nodiscard]] const E& Error() const&
    {
        RequireError();
        return std::get<ErrorIndex>(m_storage);
    }

    // AndThen, Transform and OrElse mean what and_then, transform and or_else mean in std::expected.
    template <typename F> auto AndThen(F&& next) const&
    {
        return AndThenOn(*this, std::forward<F>(next));
    }

    template <typename F> auto AndThen(F&& next) &&
    {
        return AndThenOn(std::move(*this), std::forward<F>(next));
    }

    template <typename F> auto Transform(F&& map) const&
    {
        return TransformOn(*this, std::forward<F>(map));
    }

    template <typename F> auto Transform(F&& map) &&
    {
        return TransformOn(std::move(*this), std::forward<F>(map));
    }

    template <typename F> auto OrElse(F&& recover) const&
    {
        return OrElseOn(*this, std::forward<F>(recover));
    }

    template <typename F> auto OrElse(F&& recover) &&
    {
        return OrElseOn(std::move(*this), std::forward<F>(recover));
    }

private:
    // By index, not type: std::get<int> could not choose in a std::variant<int, int>.
    static constexpr std::size_t ValueIndex = 0;
    static constexpr std::size_t ErrorIndex = 1;

    void RequireValue() const
    {
        if (!HasValue())
        {
            Detail::StopOnBadAccess("Value() de un Expected que no tiene valor");
        }
    }

    // Checks for an error, not for a missing value: if moving a T threw during an assignment, the std::variant holds
    // neither (valueless_by_exception).
    void RequireError() const
    {
        if (m_storage.index() != ErrorIndex)
        {
            Detail::StopOnBadAccess("Error() de un Expected que no tiene error");
        }
    }

    [[nodiscard]] const E& StoredError() const&
    {
        return Error();
    }

    [[nodiscard]] E&& StoredError() &&
    {
        RequireError();
        return std::get<ErrorIndex>(std::move(m_storage));
    }

    template <typename Self> using ValueOf = decltype(std::declval<Self>().Value());
    template <typename Self> using ErrorOf = decltype(std::declval<Self>().StoredError());

    template <typename Self, typename F> static auto AndThenOn(Self&& self, F&& next)
    {
        using Next = std::remove_cvref_t<std::invoke_result_t<F, ValueOf<Self>>>;
        static_assert(Detail::IsExpectedWithError<Next, E>, "AndThen takes a function that returns an Expected<U, E>");
        if (self.HasValue())
        {
            return Next(std::invoke(std::forward<F>(next), std::forward<Self>(self).Value()));
        }
        return Next(Unexpected<E>(std::forward<Self>(self).StoredError()));
    }

    template <typename Self, typename F> static auto TransformOn(Self&& self, F&& map)
    {
        using Mapped = std::remove_cv_t<std::invoke_result_t<F, ValueOf<Self>>>;
        if (!self.HasValue())
        {
            return Expected<Mapped, E>(Unexpected<E>(std::forward<Self>(self).StoredError()));
        }
        if constexpr (std::is_void_v<Mapped>)
        {
            std::invoke(std::forward<F>(map), std::forward<Self>(self).Value());
            return Expected<Mapped, E>();
        }
        else
        {
            return Expected<Mapped, E>(std::invoke(std::forward<F>(map), std::forward<Self>(self).Value()));
        }
    }

    template <typename Self, typename F> static auto OrElseOn(Self&& self, F&& recover)
    {
        using Next = std::remove_cvref_t<std::invoke_result_t<F, ErrorOf<Self>>>;
        static_assert(Detail::IsExpectedWithValue<Next, T>, "OrElse takes a function that returns an Expected<T, G>");
        if (self.HasValue())
        {
            return Next(std::forward<Self>(self).Value());
        }
        return Next(std::invoke(std::forward<F>(recover), std::forward<Self>(self).StoredError()));
    }

    std::variant<T, E> m_storage;
};

// Like std::expected<void, E>: default-constructed is success, so a function does `return {};`.
template <typename E> class [[nodiscard]] Expected<void, E>
{
    static_assert(!std::is_reference_v<E>, "Expected guarda valores, no referencias");

public:
    Expected() = default;

    Expected(Unexpected<E> error) : m_error(std::move(error).Error()) {}

    [[nodiscard]] bool HasValue() const
    {
        return !m_error.has_value();
    }

    [[nodiscard]] const E& Error() const&
    {
        if (!m_error.has_value())
        {
            Detail::StopOnBadAccess("Error() de un Expected que no tiene error");
        }
        return *m_error;
    }

    template <typename F> auto AndThen(F&& next) const&
    {
        return AndThenOn(*this, std::forward<F>(next));
    }

    template <typename F> auto AndThen(F&& next) &&
    {
        return AndThenOn(std::move(*this), std::forward<F>(next));
    }

    template <typename F> auto Transform(F&& map) const&
    {
        return TransformOn(*this, std::forward<F>(map));
    }

    template <typename F> auto Transform(F&& map) &&
    {
        return TransformOn(std::move(*this), std::forward<F>(map));
    }

    template <typename F> auto OrElse(F&& recover) const&
    {
        return OrElseOn(*this, std::forward<F>(recover));
    }

    template <typename F> auto OrElse(F&& recover) &&
    {
        return OrElseOn(std::move(*this), std::forward<F>(recover));
    }

private:
    [[nodiscard]] const E& StoredError() const&
    {
        return Error();
    }

    [[nodiscard]] E&& StoredError() &&
    {
        return std::move(*m_error);
    }

    template <typename Self> using ErrorOf = decltype(std::declval<Self>().StoredError());

    template <typename Self, typename F> static auto AndThenOn(Self&& self, F&& next)
    {
        using Next = std::remove_cvref_t<std::invoke_result_t<F>>;
        static_assert(Detail::IsExpectedWithError<Next, E>, "AndThen takes a function that returns an Expected<U, E>");
        if (self.HasValue())
        {
            return Next(std::invoke(std::forward<F>(next)));
        }
        return Next(Unexpected<E>(std::forward<Self>(self).StoredError()));
    }

    template <typename Self, typename F> static auto TransformOn(Self&& self, F&& map)
    {
        using Mapped = std::remove_cv_t<std::invoke_result_t<F>>;
        if (!self.HasValue())
        {
            return Expected<Mapped, E>(Unexpected<E>(std::forward<Self>(self).StoredError()));
        }
        if constexpr (std::is_void_v<Mapped>)
        {
            std::invoke(std::forward<F>(map));
            return Expected<Mapped, E>();
        }
        else
        {
            return Expected<Mapped, E>(std::invoke(std::forward<F>(map)));
        }
    }

    template <typename Self, typename F> static auto OrElseOn(Self&& self, F&& recover)
    {
        using Next = std::remove_cvref_t<std::invoke_result_t<F, ErrorOf<Self>>>;
        static_assert(Detail::IsExpectedWithValue<Next, void>,
                      "OrElse takes a function that returns an Expected<void, G>");
        if (self.HasValue())
        {
            return Next();
        }
        return Next(std::invoke(std::forward<F>(recover), std::forward<Self>(self).StoredError()));
    }

    std::optional<E> m_error;
};

// Reserved for an error that is a message (CODING_RULES §7, "Types and aliases").
template <typename T> using Result = Expected<T, std::string>;
using Status = Result<void>;
} // namespace Client::Core
