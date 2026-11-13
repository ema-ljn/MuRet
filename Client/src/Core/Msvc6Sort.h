#pragma once

#include <cstddef>
#include <utility>

// El std::sort de la STL de MSVC6 (el <algorithm> de Dinkumware de VC6), que no es estable y deja los iguales en un
// orden propio: el original lo instancia sobre los deque de sus listas (0x49c260 con 0x4ab930, el _Sort; 0x4ac4a0, el
// _Insertion_sort; 0x4abe00, el _Unguarded_insert; 0x4ac6c0, el _Median, y 0x4ac760, el _Unguarded_partition). Para
// que los iguales queden donde los deja el original, MuRet corre el mismo algoritmo y no el std::sort de su STL.
// `items` es un contenedor con operator[] y size() (un std::deque o un std::vector), y `less` el comparador del
// original, que puede no ser un orden estricto.
namespace Client::Core
{
// _SORT_MAX: hasta 16, solo la inserción (0x49c260: la cuenta contra 0x11).
inline constexpr std::ptrdiff_t Msvc6SortMax = 16;

namespace Msvc6Detail
{
// _Unguarded_insert: corre `value` hacia el principio desde `last` mientras vaya antes que el de al lado. Sin guarda:
// alguien a la izquierda lo frena.
template <typename Items, typename Less>
void UnguardedInsert(Items& items, std::ptrdiff_t last, typename Items::value_type value, Less& less)
{
    std::ptrdiff_t middle = last;
    while (less(value, items[static_cast<std::size_t>(--middle)]))
    {
        items[static_cast<std::size_t>(last)] = items[static_cast<std::size_t>(middle)];
        last = middle;
    }
    items[static_cast<std::size_t>(last)] = std::move(value);
}

// _Insertion_sort sobre [first, last): el que va antes que el primero corre todo uno a la derecha (copy_backward).
template <typename Items, typename Less>
void InsertionSort(Items& items, std::ptrdiff_t first, std::ptrdiff_t last, Less& less)
{
    if (first == last)
        return;
    for (std::ptrdiff_t middle = first + 1; middle != last; ++middle)
    {
        typename Items::value_type value = items[static_cast<std::size_t>(middle)];
        if (!less(value, items[static_cast<std::size_t>(first)]))
        {
            UnguardedInsert(items, middle, std::move(value), less);
            continue;
        }
        for (std::ptrdiff_t at = middle; at != first; --at)
            items[static_cast<std::size_t>(at)] = items[static_cast<std::size_t>(at - 1)];
        items[static_cast<std::size_t>(first)] = std::move(value);
    }
}

// _Median de tres copias.
template <typename Value, typename Less> Value Median(const Value& x, const Value& y, const Value& z, Less& less)
{
    if (less(x, y))
        return less(y, z) ? y : less(x, z) ? z : x;
    return less(x, z) ? x : less(y, z) ? z : y;
}

// _Unguarded_partition con una copia del pivote.
template <typename Items, typename Less>
std::ptrdiff_t UnguardedPartition(Items& items, std::ptrdiff_t first, std::ptrdiff_t last,
                                  const typename Items::value_type& pivot, Less& less)
{
    for (;; ++first)
    {
        while (less(items[static_cast<std::size_t>(first)], pivot))
            ++first;
        while (less(pivot, items[static_cast<std::size_t>(--last)]))
        {
        }
        if (last <= first)
            return first;
        std::swap(items[static_cast<std::size_t>(first)], items[static_cast<std::size_t>(last)]);
    }
}

// _Sort: la partición con la mediana del primero, el del medio y el último, la recursión sobre el lado que no es más
// largo y la vuelta sobre el otro, mientras queden más de 16.
template <typename Items, typename Less>
void QuickSort(Items& items, std::ptrdiff_t first, std::ptrdiff_t last, Less& less)
{
    while (Msvc6SortMax < last - first)
    {
        using Value = typename Items::value_type;
        const Value pivot = Median(Value(items[static_cast<std::size_t>(first)]),
                                   Value(items[static_cast<std::size_t>(first + (last - first) / 2)]),
                                   Value(items[static_cast<std::size_t>(last - 1)]), less);
        const std::ptrdiff_t middle = UnguardedPartition(items, first, last, pivot, less);
        if (last - middle <= middle - first)
        {
            QuickSort(items, middle, last, less);
            last = middle;
        }
        else
        {
            QuickSort(items, first, middle, less);
            first = middle;
        }
    }
}
} // namespace Msvc6Detail

// sort(first, last, less) de VC6 (_Sort_0): con más de 16, el _Sort, la inserción de los primeros 16 y la inserción sin
// guarda de los demás.
template <typename Items, typename Less> void Msvc6Sort(Items& items, Less less)
{
    const auto count = static_cast<std::ptrdiff_t>(items.size());
    if (count <= Msvc6SortMax)
    {
        Msvc6Detail::InsertionSort(items, 0, count, less);
        return;
    }
    Msvc6Detail::QuickSort(items, 0, count, less);
    Msvc6Detail::InsertionSort(items, 0, Msvc6SortMax, less);
    for (std::ptrdiff_t at = Msvc6SortMax; at != count; ++at)
        Msvc6Detail::UnguardedInsert(items, at, typename Items::value_type(items[static_cast<std::size_t>(at)]), less);
}
} // namespace Client::Core
