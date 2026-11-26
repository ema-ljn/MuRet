#pragma once

#include <cstddef>
#include <utility>

// The std::sort of MSVC6's STL (VC6's Dinkumware <algorithm>), unstable, which leaves equal elements in its own
// order: the original instantiates it over its lists' deques (0x49c260 with 0x4ab930, _Sort; 0x4ac4a0,
// _Insertion_sort; 0x4abe00, _Unguarded_insert; 0x4ac6c0, _Median; and 0x4ac760, _Unguarded_partition, with the
// deque's iterator, 0x4acc70, and the element copy, 0x4accd0). MuRet runs the same algorithm so equal elements land
// where the original leaves them. `less` is the original's comparator, which may not be a strict order.
namespace Client::Crt
{
// _SORT_MAX: up to 16, insertion only (0x49c260: the count against 0x11).
inline constexpr std::ptrdiff_t Msvc6SortMax = 16;

namespace Msvc6Detail
{
// _Unguarded_insert: moves `value` towards the front from `last` while it goes before its neighbor. Unguarded:
// something on its left stops it.
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

// _Insertion_sort over [first, last): one that goes before the first shifts everything right (copy_backward).
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

// _Unguarded_partition with a copy of the pivot.
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

// _Sort: partition with the median of the first, middle and last, recursion on the side that is not longer and a
// loop on the other, while more than 16 remain.
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

// VC6's sort(first, last, less) (_Sort_0): with more than 16, _Sort, insertion of the first 16 and unguarded insertion
// of the rest.
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
} // namespace Client::Crt
