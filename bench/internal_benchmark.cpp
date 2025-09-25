#include <benchmark/benchmark.h>
#include <cstdint>

uintptr_t align_conditional(uintptr_t ptr, size_t align) {
	uintptr_t p, a, modulo;

	p = ptr;
	a = (uintptr_t)align;
	// Same as (p % a) but faster as 'a' is a power of two
	modulo = p & (a-1);

	if (modulo != 0) {
		// If 'p' address is not aligned, push the address to the
		// next value which is aligned
		p += a - modulo;
	}
	return p;
}

uintptr_t align_mask(uintptr_t addr, uintptr_t align) {
    return (addr + (align - 1)) & -align;   // Round up to align-byte boundary
}

uintptr_t align_mask2(uintptr_t address, uintptr_t alignment) {
  return (address + (alignment - 1)) & ~(alignment - 1);
}


static void BM_AlignConditional(benchmark::State &state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(align_conditional(state.range(0), state.range(1)));
  }
}

BENCHMARK(BM_AlignConditional)
  ->DenseRange(UINTPTR_MAX / 2, (UINTPTR_MAX / 2) + 0x10000000)
  ->RangeMultiplier(2)
  ->Range(8, 32);


static void BM_AlignMask(benchmark::State &state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(align_mask(state.range(0), state.range(1)));
  }
}

BENCHMARK(BM_AlignMask)
    ->DenseRange(UINTPTR_MAX / 2, (UINTPTR_MAX / 2) + 0x10000000)
    ->RangeMultiplier(2)
    ->Range(8, 32);

static void BM_AlignMask2(benchmark::State &state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(align_mask2(state.range(0), state.range(1)));
  }
}

BENCHMARK(BM_AlignMask2)
  ->DenseRange(UINTPTR_MAX / 2, (UINTPTR_MAX / 2) + 0x10000000)
  ->RangeMultiplier(2)
  ->Range(8, 32);

BENCHMARK_MAIN();
