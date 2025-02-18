// Type your code here, or load an example.
#include <stdio.h>
#include <stddef.h>
#include <math.h>

#include <arm_neon.h>

void print_vec(uint32x4_t vec) {
    uint32_t val0 = vgetq_lane_u32(vec, 0);
    printf("Element 0: 0x%08x\n", 0, val0);

    uint32_t val1 = vgetq_lane_u32(vec, 1);
    printf("Element 1: 0x%08x\n", 1, val1);

    uint32_t val2 = vgetq_lane_u32(vec, 2);
    printf("Element 2: 0x%08x\n", 2, val2);

    uint32_t val3 = vgetq_lane_u32(vec, 3);
    printf("Element 3: 0x%08x\n", 3, val3);
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
    uint32x4_t bmap = {0xffffffff,0xffffffff,0xffffffff,0xffffffff};
    printf("bmap values: ");
    print_vec(bmap);

    return 0;
}

