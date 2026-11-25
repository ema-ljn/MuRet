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

// Un resultado que tiene adentro un valor o un error, nunca los dos.
//
// Es la idea de std::expected, que llega con C++23; MuRet compila en C++20, y L-3 del camino de
// aprendizaje pide construir lo que el estándar todavía no trae. Un cargador de Data\ devuelve,
// por ejemplo, Expected<TextTable, DataError>, y quien llama mira si hay tabla antes de usarla:
//
//   if (file.size() < TextTableSize)
//   {
//       return Unexpected(DataError::FileTooShort);
//   }
//   return table;
//
// La clase es [[nodiscard]]: ignorar lo que devuelve un cargador da un warning, y en CI un error.
// Un código de error suelto se puede ignorar sin que nadie se entere.
//
// Los nombres siguen la regla 7 (HasValue, Value, Error), no los de std::expected (has_value,
// value, error). El día que el repo pase a C++23, cambiar uno por el otro es mecánico.
//
// E va como un enum class (DataError, por ejemplo). Con un E numérico, un error se puede volver
// valor sin que nadie lo note: en un Expected<long, int>, `return error;` con un int convierte a
// long y da un valor. Y no hay conversiones de más: en un Expected<std::string, E>, `return "x";`
// no compila (serían dos conversiones), y Unexpected("x") es un Unexpected<const char*>.
//
// Pedir Value() de uno que tiene un error, o Error() de uno que tiene un valor, es un bug de quien
// llama: el programa se corta ahí con un mensaje, también en Release. std::expected tira una
// excepción, y MuRet no usa excepciones para los errores.
namespace Client::Core
{
// El error, envuelto: así se distingue de un valor aunque T y E sean del mismo tipo.
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
    // Sin explicit, a propósito: un cargador hace `return table;` o `return Unexpected(error);`.
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

    // Para sacar el valor sin copiarlo: std::move(loaded).Value(). std::expected devuelve acá una
    // referencia (T&&); este devuelve el valor movido, así `const T& t = Load().Value();` no queda
    // colgando cuando el Expected temporal muere al final de la línea.
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
    // Por índice y no por tipo: std::get<int> no sabría cuál elegir en un std::variant<int, int>.
    static constexpr std::size_t ValueIndex = 0;
    static constexpr std::size_t ErrorIndex = 1;

    void RequireValue() const
    {
        if (!HasValue())
        {
            Detail::StopOnBadAccess("Value() de un Expected que no tiene valor");
        }
    }

    // Mira que haya un error, no que falte el valor: si el move de un T tiró una excepción en una
    // asignación, el std::variant queda sin ninguno de los dos (valueless_by_exception).
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

// Sin valor: lo que solo puede fallar, como subir las texturas de una escena (CL5.1). Como
// std::expected<void, E>, construido sin nada es el éxito, así una función hace `return {};`. No
// tiene Value(): no hay nada que sacar.
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
