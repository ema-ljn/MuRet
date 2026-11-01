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

#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <variant>

namespace Client::Core
{

// El error, envuelto: así se distingue de un valor aunque T y E sean del mismo tipo.
template <typename E> class Unexpected
{
public:
    explicit Unexpected(E error) : wrapped(std::move(error)) {}

    E&& Error() &&
    {
        return std::move(wrapped);
    }

private:
    E wrapped;
};

namespace Detail
{

[[noreturn]] inline void StopOnBadAccess(const char* message)
{
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

} // namespace Detail

template <typename T, typename E> class [[nodiscard]] Expected
{
    static_assert(!std::is_reference_v<T> && !std::is_reference_v<E>, "Expected guarda valores, no referencias");

public:
    // Sin explicit, a propósito: un cargador hace `return table;` o `return Unexpected(error);`.
    Expected(T value) : storage(std::in_place_index<ValueIndex>, std::move(value)) {}

    Expected(Unexpected<E> error) : storage(std::in_place_index<ErrorIndex>, std::move(error).Error()) {}

    [[nodiscard]] bool HasValue() const
    {
        return storage.index() == ValueIndex;
    }

    [[nodiscard]] T& Value() &
    {
        RequireValue();
        return std::get<ValueIndex>(storage);
    }

    [[nodiscard]] const T& Value() const&
    {
        RequireValue();
        return std::get<ValueIndex>(storage);
    }

    // Para sacar el valor sin copiarlo: std::move(loaded).Value(). std::expected devuelve acá una
    // referencia (T&&); este devuelve el valor movido, así `const T& t = Load().Value();` no queda
    // colgando cuando el Expected temporal muere al final de la línea.
    [[nodiscard]] T Value() &&
    {
        RequireValue();
        return std::move(std::get<ValueIndex>(storage));
    }

    // Mira que haya un error, no que falte el valor: si el move de un T tiró una excepción en una
    // asignación, el std::variant queda sin ninguno de los dos (valueless_by_exception).
    [[nodiscard]] const E& Error() const&
    {
        if (storage.index() != ErrorIndex)
        {
            Detail::StopOnBadAccess("Error() de un Expected que no tiene error");
        }
        return std::get<ErrorIndex>(storage);
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

    std::variant<T, E> storage;
};

} // namespace Client::Core
