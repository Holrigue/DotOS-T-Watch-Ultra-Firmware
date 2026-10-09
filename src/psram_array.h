#pragma once
// psram_array.h - a fixed-size table that lives in PSRAM instead of internal RAM.
//
// Internal RAM is the scarce budget on this watch (the display DMA buffers, Wi-Fi, BLE and
// the static tables below all compete for ~320 KB); PSRAM has 8 MB. A plain
// `static Foo s_tbl[N];` sits in internal .bss for the whole life of the firmware even if
// the feature is never opened. `static PsramArray<Foo, N> s_tbl;` costs 4 bytes of .bss and
// allocates (zero-filled, like .bss) on first access, in PSRAM, falling back to the normal
// heap if PSRAM is unavailable.
//
// Drop-in for tables of plain data (POD): s_tbl[i], s_tbl.data(), PsramArray::size().
// Not for objects with constructors (use placement-new on a PSRAM block for those), and not
// for anything an ISR or an IRAM-resident callback touches (PSRAM is not reachable while the
// flash cache is off). If the allocation fails outright, operator[] returns a shared scratch
// element so a write can never run off the end; it never returns null.
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
static inline void *psram_array_calloc(size_t count, size_t size)
{
    void *p = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(count, size);
}
#else   // host tests
static inline void *psram_array_calloc(size_t count, size_t size) { return calloc(count, size); }
#endif

template <typename T, size_t N>
class PsramArray {
    static_assert(std::is_trivially_copyable<T>::value, "PsramArray holds plain data only");

public:
    T &operator[](size_t i)
    {
        T *d = data();
        return d == scratch() ? d[0] : d[i < N ? i : N - 1];   // scratch has ONE element
    }
    const T &operator[](size_t i) const { return const_cast<PsramArray *>(this)->operator[](i); }

    // First access allocates; nothing else in the table's lifetime does.
    T *data()
    {
        if (!p_) {
            p_ = static_cast<T *>(psram_array_calloc(N, sizeof(T)));
            if (!p_) p_ = scratch();
        }
        return p_;
    }
    static constexpr size_t size() { return N; }
    bool allocated() const { return p_ != nullptr; }
    // Zero the table. Not allocated yet = already zero, so this never allocates.
    void clear()
    {
        if (p_ && p_ != scratch()) memset(p_, 0, N * sizeof(T));
    }

private:
    // Allocation failed: every index maps onto this one element (see the header comment).
    static T *scratch()
    {
        static T one[1];
        return one;
    }
    T *p_ = nullptr;
};
