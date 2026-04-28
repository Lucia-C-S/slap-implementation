/* test/test_databank.c
 *
 * Compile and run on PC (Linux/macOS) to validate the DataBank
 * before touching any hardware.
 *
 * Build command (from the slap/ root directory):
 *   gcc -Wall -Wextra -I. \
 *       test/test_databank.c \
 *       slap_databank.c \
 *       osal/ports/osal_posix.c \
 *       -pthread -o test_databank && ./test_databank
 *
 * Expected output:
 *   [PASS] alloc 1..4 succeeded
 *   [PASS] alloc 5 returned NULL (pool exhausted)
 *   [PASS] free slot 2, re-alloc succeeded
 *   [PASS] free(NULL) did not crash
 *   All DataBank tests passed.
 */

#include <stdio.h>
#include <assert.h>
#include "slap_databank.h"
#include "osal/osal.h"

int main(void)
{
    /* Initialise OSAL first (creates the internal mutex) */
    osal_init();

    /* Initialise the pool */
    slap_databank_init();

    /* --- Test 1: fill the pool to capacity --- */
    slap_packet_t *slots[SLAP_POOL_SIZE];

    for (int i = 0; i < SLAP_POOL_SIZE; i++) {
        slots[i] = slap_databank_alloc();
        if (slots[i] == NULL) {
            printf("[FAIL] alloc %d returned NULL unexpectedly\n", i + 1);
            return 1;
        }
    }
    printf("[PASS] alloc 1..%d succeeded\n", SLAP_POOL_SIZE);

    /* --- Test 2: pool is now exhausted, next alloc must return NULL --- */
    slap_packet_t *overflow = slap_databank_alloc();
    if (overflow != NULL) {
        printf("[FAIL] alloc beyond pool size did not return NULL\n");
        return 1;
    }
    printf("[PASS] alloc %d returned NULL (pool exhausted)\n",
           SLAP_POOL_SIZE + 1);

    /* --- Test 3: free one slot, verify it can be reallocated --- */
    slap_databank_free(slots[1]);   /* free the second slot */
    slots[1] = NULL;

    slap_packet_t *realloc_slot = slap_databank_alloc();
    if (realloc_slot == NULL) {
        printf("[FAIL] realloc after free returned NULL\n");
        return 1;
    }
    printf("[PASS] free slot 2, re-alloc succeeded\n");
    slap_databank_free(realloc_slot);

    /* --- Test 4: free(NULL) must not crash --- */
    slap_databank_free(NULL);
    printf("[PASS] free(NULL) did not crash\n");

    /* --- Test 5: all slots are zeroed on alloc --- */
    slap_packet_t *clean = slap_databank_alloc();
    assert(clean != NULL);
    assert(clean->data_len == 0);
    assert(clean->sec_wire_len == 0);
    assert(clean->primary_header.service_type == 0);
    printf("[PASS] freshly allocated slot is zero-initialised\n");
    slap_databank_free(clean);

    /* Free remaining slots */
    for (int i = 0; i < SLAP_POOL_SIZE; i++) {
        slap_databank_free(slots[i]);
    }

    printf("\nAll DataBank tests passed.\n");
    return 0;
}