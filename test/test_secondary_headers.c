/* In test/test_secondary_headers.c:
 *
 * Build:
 *   gcc -Wall -I. test/test_secondary_headers.c \
 *       slap_secondary_headers.c -o test_sec && ./test_sec
 */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "slap_secondary_headers.h"

int main(void)
{
    uint8_t wire[16];
    slap_secondary_header_t sec, sec_rx;

    /* Test 1: HK request (1.1) packs to 2 bytes */
    sec.hk_req.hk_type    = 0;
    sec.hk_req.historical = 1;
    sec.hk_req.param_id   = 0x2A;
    int n = slap_sec_pack(1, 1, &sec, wire, sizeof(wire));
    assert(n == 2);
    assert((wire[0] >> 7) == 0);
    assert((wire[0] >> 6 & 1) == 1);
    assert(wire[1] == 0x2A);
    slap_sec_unpack(1, 1, wire, n, &sec_rx);
    assert(sec_rx.hk_req.historical == 1);
    assert(sec_rx.hk_req.param_id   == 0x2A);
    printf("[PASS] HK request 1.1\n");

    /* Test 2: LPT segment (5.3) packs seq_flag+seq_id into 3 bytes */
    sec.lpt_segment.seq_flag = SLAP_SEQ_FIRST;   /* 0b01 */
    sec.lpt_segment.seq_id   = 0x000001;
    n = slap_sec_pack(5, 3, &sec, wire, sizeof(wire));
    assert(n == 3);
    assert((wire[0] >> 6) == SLAP_SEQ_FIRST);
    assert(wire[2] == 0x01);
    slap_sec_unpack(5, 3, wire, n, &sec_rx);
    assert(sec_rx.lpt_segment.seq_flag == SLAP_SEQ_FIRST);
    assert(sec_rx.lpt_segment.seq_id   == 1);
    printf("[PASS] LPT segment 5.3\n");

    /* Test 3: ls size report (6.2) packs to 8 bytes — the maximum */
    sec.fm_ls_size.list_size        = 0x00001234;
    sec.fm_ls_size.num_directories  = 3;
    sec.fm_ls_size.num_files        = 7;
    n = slap_sec_pack(6, 2, &sec, wire, sizeof(wire));
    assert(n == 8);
    slap_sec_unpack(6, 2, wire, n, &sec_rx);
    assert(sec_rx.fm_ls_size.list_size       == 0x00001234);
    assert(sec_rx.fm_ls_size.num_directories == 3);
    assert(sec_rx.fm_ls_size.num_files       == 7);
    printf("[PASS] FM ls size report 6.2\n");

    /* Test 4: Echo has no secondary header */
    assert(slap_sec_wire_size(0, 1) == 0);
    printf("[PASS] Echo secondary header size == 0\n");

    printf("\nAll secondary header tests passed.\n");
    return 0;
}