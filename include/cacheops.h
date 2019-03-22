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
 * Most machines should support _mm_sfence() and _mm_clflush() so they are
 * simply wrapped.
 */

void sfence(void) { _mm_sfence(); }

void clflush(void const *ptr) { _mm_clflush(ptr); }

/**
 * Having __attribute__((optnone,noinline)) is to avoid compiler optimizing it
 * away so it emulates the behavior of calling to a real intrinsic function,
 * for example:
 *   tail call void @llvm.x86.clflushopt(i8* %0)
 * is emulated by
 *   tail call void @clflushopt(i8* %0)
 *
 * The asm implementations are from pmdk/src/libpmem/x86_64/flush.h but they may
 * not be accurate for performance evaluation.
 */

void __attribute__((optnone, noinline)) clflushopt(void const *ptr) {
  asm volatile(".byte 0x66; clflush %0" : "+m"(*(volatile char *)(ptr)));
}

void __attribute__((optnone, noinline)) clwb(void const *ptr) {
  asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)(ptr)));
}

#endif
