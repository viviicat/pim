#include "threading/intrin.h"
#include <emmintrin.h>

void Intrin_Spin(u64 spins)
{
    u64 ticks = spins * 100;
    if (ticks >= 2500)
    {
        Intrin_Yield();
    }
    else
    {
        u64 end = Intrin_Timestamp() + ticks;
        do
        {
            // on sse2 _mm_pause indicates a spinloop to the cpu, in the hope that it clocks down temporarily
            Intrin_Pause();
        } while (Intrin_Timestamp() < end);
    }
}

#if PLAT_WINDOWS

// rdtsc
#include <windows.h>
#include <intrin.h>

#pragma intrinsic(__rdtsc)
#pragma intrinsic(_mm_pause)

void Intrin_Yield(void) { SwitchToThread(); }

#else

#include <sched.h>

#if !__has_builtin(__rdtsc)
pim_inline u64 __rdtsc(void)
{
    u32 hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    u64 value = hi;
    value = value << 32;
    value |= lo;
    return value;
}
#endif

void Intrin_Yield(void) { sched_yield(); }
#endif // PLAT_X

u64 Intrin_Timestamp(void) { return __rdtsc(); }
void Intrin_Pause(void) { _mm_pause(); }
