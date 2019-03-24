#ifndef _NVS_CACHEOPS_H_
#define _NVS_CACHEOPS_H_

#include <immintrin.h>

/**
 * These are fake functions for NVScope to identify corresponding cache
 * operations. The reason for doing this is that not every development machine
 * has CLFLUSHOPT and CLWB instructions, so directly calling _mm_clflushopt() or
 * _mm_clwb() may often fail to compile.
 *
 * NVScope can still identify real _mm_clflushopt() or _mm_clwb() calls if they
 * are supported on a target machine.
 *
 * Do not use the faked operations for performance evaluation.
 *
 * Most machines should support _mm_sfence() and _mm_clflush() so they are
 * simply wrapped.
 *
 * Having __attribute__((optnone,noinline)) is to avoid compiler optimizing it
 * away so it emulates the behavior of calling to a real intrinsic function, for
 * example:
 *   tail call void @llvm.x86.clflushopt(i8* %0)
 * is emulated by
 *   tail call void @clflushopt(i8* %0)
 */

void __attribute__((optnone, noinline)) sfence(void) { _mm_sfence(); }

void __attribute__((optnone, noinline)) clflush(void const *ptr) {
  _mm_clflush(ptr);
}

void __attribute__((optnone, noinline)) clflushopt(void const *ptr) {
  unsigned char loopcnt = *((char *)ptr);
  while (loopcnt > 0) --loopcnt;
}

void __attribute__((optnone, noinline)) clwb(void const *ptr) {
  unsigned char loopcnt = *((char *)ptr);
  while (loopcnt > 0) --loopcnt;
}

#endif
