#ifndef HOS_CPU_H
#define HOS_CPU_H
#if defined(__aarch64__)
static inline void hos_cpu_relax(void) { __asm__ volatile("yield" ::: "memory"); }
#elif defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
static inline void hos_cpu_relax(void) { _mm_pause(); }
#else
static inline void hos_cpu_relax(void) { }
#endif
#endif
