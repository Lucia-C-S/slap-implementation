/* slap_databank.h */
#ifndef SLAP_DATABANK_H
#define SLAP_DATABANK_H

#include "slap_packet.h"

/* Maximum number of slap_packet_t instances that can exist
 * simultaneously. Minimum needed: 2 (one req + one resp).
 * Set to 4 for safety margin.
 * Each slot costs ~2080 bytes of BSS — on a 256 KB MCU this
 * is ~8 KB total, which is acceptable.                          */
#define SLAP_POOL_SIZE 4

/** Initialise the pool and its mutex. Call once from slap_init(). */
void slap_databank_init(void);

/**
 * Allocate one packet slot from the pool.
 * Returns a pointer to a zero-initialised slap_packet_t,
 * or NULL if all SLAP_POOL_SIZE slots are currently in use.
 * Thread-safe: protected by an internal mutex.
 */
slap_packet_t *slap_databank_alloc(void);

/**
 * Return a slot to the pool.
 * pkt must be a pointer previously returned by slap_databank_alloc().
 * Passing NULL is safe (no-op). Passing a foreign pointer is undefined.
 * Thread-safe: protected by an internal mutex.
 */
void slap_databank_free(slap_packet_t *pkt);

#endif /* SLAP_DATABANK_H */