/*
 * prng_lcg.h
 *
 * Created: 25-Oct-19 12:14:24 PM
 *  Author: david
 */

#ifndef PRNG_LCG_H_
#define PRNG_LCG_H_

#define LCG_A 109
#define LCG_C 89
#define LCG_INITIAL_SEED 500

uint16_t last_lcg = LCG_INITIAL_SEED;

inline void srand_lcg(uint16_t seed)
{
    last_lcg = seed;
}

// 16 bit
inline uint16_t rand_lcg(void)
{
    last_lcg = (uint16_t)((uint32_t)(last_lcg) * LCG_A + LCG_C);
    return last_lcg;
}

#endif /* PRNG_LCG_H_ */
