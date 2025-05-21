// Experimental file for testing and comparing Count Leading Zeros (CLZ) implementations.
// This file includes:
//   - A generic C implementation using GCC/Clang builtins (`__builtin_clzll`).
//   - An ARM NEON intrinsic-based implementation (`vclzq_u32`).
//
// Note:
//   - The code is specific to ARM architecture (due to arm_neon.h) and
//     compilers supporting GCC builtins (GCC, Clang).
//   - This file is not part of the main CMake build process and is intended for
//     standalone testing or experimentation.
//   - The `v_clz` NEON function appears to be incomplete or contains a bug,
//     as it calculates leading zero counts but then returns the minimum of the
//     original input values, not the leading zero counts.

#include <stdio.h>
#include <stddef.h>
#include <math.h>

#include <arm_neon.h>

void print_vec(uint32x4_t vec) {
    uint32_t val0 = vgetq_lane_u32(vec, 0);
    printf("Element 0: 0x%032b\n", val0);

    uint32_t val1 = vgetq_lane_u32(vec, 1);
    printf("Element 1: 0x%032b\n", val1);

    uint32_t val2 = vgetq_lane_u32(vec, 2);
    printf("Element 2: 0x%032b\n", val2);

    uint32_t val3 = vgetq_lane_u32(vec, 3);
    printf("Element 3: 0x%032b\n", val3);
    printf("\n"); // Newline at the end

}

uint8_t n_clz(uint64_t* vec) {
    int clz0 = __builtin_clzll(vec[0]);
    if (clz0 > 0)
        return clz0;
    int clz1 = __builtin_clzll(vec[1]);
    return 64 + clz1;
}

uint8_t v_clz(uint32x4_t vec){
    uint32x4_t lzvec = vclzq_u32(vec);
    uint32x2_t min_pair = vmin_u32(vget_low_u32(vec), vget_high_u32(vec));
    uint32_t min = (vget_lane_u32(min_pair, 0) < vget_lane_u32(min_pair, 1)) ? vget_lane_u32(min_pair, 0) : vget_lane_u32(min_pair, 1);
    return min;
}

int main() {
    uint32x4_t bmap = {0xaabb,0xbbcc,0xccdd,0xeeff};
    uint64_t vec[] = {0xaabbbbcc, 0xccddeeff};
    printf("bmap values: \n");
    print_vec(bmap);

    printf("clz generic: %d\n", n_clz(vec));
    printf("clz neon: %d\n", v_clz(bmap));

    return 0;
}

