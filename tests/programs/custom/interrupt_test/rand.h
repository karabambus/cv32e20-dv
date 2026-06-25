/*
** Minimal deterministic PRNG for the interrupt directed tests.
**
** The upstream test included a "rand.h" that is not present in this repo.
** A deterministic generator keeps the directed test reproducible across runs.
**
**   random_num32()        -> 32-bit pseudo-random value
**   random_num(max, min)  -> value in the inclusive range [min, max]
*/
#ifndef _RAND_H_
#define _RAND_H_

#include <stdint.h>

static uint32_t _rand_state = 0x1234ABCDu;

static inline uint32_t random_num32(void)
{
    /* xorshift32 */
    uint32_t x = _rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    _rand_state = x;
    return x;
}

static inline uint32_t random_num(uint32_t max, uint32_t min)
{
    if (max < min) {
        uint32_t t = max; max = min; min = t;
    }
    uint32_t span = max - min + 1u;
    if (span == 0u)            /* full 32-bit range */
        return random_num32();
    return min + (random_num32() % span);
}

#endif /* _RAND_H_ */
