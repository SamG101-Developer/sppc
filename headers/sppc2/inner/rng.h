#pragma once
#include <sys/random.h>
#include <stdint.h>

typedef struct {
    uint64_t state[4];
    bool seeded;
} prng_state_t;

static _Thread_local prng_state_t SPPC_PRNG_STATE = {0};

static uint64_t rotl64(const uint64_t x, const int k) {
    return x << k | x >> (64 - k);
}

static uint64_t splitmix64(uint64_t *x) {
    auto z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ z >> 30) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ z >> 27) * 0x94d049bb133111ebULL;
    return z ^ z >> 31;
}

static uint64_t xoshiro256ss(uint64_t s[4]) {
    const auto result = rotl64(s[1] * 5, 7) * 9;
    const auto t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] ^= rotl64(s[3], 45);
    return result;
}

static int prng_init_from_seed(prng_state_t *state, const uint64_t seed) {
    auto sm = seed;
    state->state[0] = splitmix64(&sm);
    state->state[1] = splitmix64(&sm);
    state->state[2] = splitmix64(&sm);
    state->state[3] = splitmix64(&sm);
    state->seeded = true;
    return 0;
}

static int prng_init_from_os(prng_state_t *state) {
    const auto err = getrandom(state->state, sizeof(state->state), 0);
    if (err < 0) { return err; }
    state->seeded = true;
    return 0;
}
