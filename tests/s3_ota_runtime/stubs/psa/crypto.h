#pragma once

#include <stddef.h>
#include <stdint.h>

typedef int32_t psa_status_t;
typedef uint32_t psa_algorithm_t;
typedef struct { uint32_t active; } psa_hash_operation_t;

#define PSA_SUCCESS ((psa_status_t)0)
#define PSA_ERROR_GENERIC_ERROR ((psa_status_t)-132)
#define PSA_ALG_SHA_256 ((psa_algorithm_t)0x02000009u)
#define PSA_HASH_OPERATION_INIT {0u}

static inline psa_hash_operation_t psa_hash_operation_init(void)
{
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    return operation;
}

psa_status_t psa_hash_setup(psa_hash_operation_t *operation,
                            psa_algorithm_t algorithm);
psa_status_t psa_hash_update(psa_hash_operation_t *operation,
                             const uint8_t *input, size_t input_length);
psa_status_t psa_hash_finish(psa_hash_operation_t *operation,
                             uint8_t *hash, size_t hash_size,
                             size_t *hash_length);
psa_status_t psa_hash_abort(psa_hash_operation_t *operation);
