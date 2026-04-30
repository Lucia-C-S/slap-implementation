/* services/2_time_management.c
 *
 * SLAP Service 2 — Time Management (0x02)
 *
 * Provides on-board time generation, distribution, and synchronisation
 * across TTC, OBC, and PAYLOAD subsystems. Uses CCSDS CUC 4.2 format:
 *   4 bytes coarse (elapsed seconds since 1958-01-01 TAI)
 *   3 bytes fine   (sub-second binary fraction, 2^-24 s resolution)
 *   Total: 7 bytes per timestamp.
 *
 * TAI (International Atomic Time) epoch offset from Unix (1970-01-01):
 *   378,691,200 seconds (implemented in osal_get_time_cuc()).
 *
 * Subservices defined (§3.2):
 *   1. Time reporting control: set the autonomous broadcast rate.
 *   2. Time reporting: deliver current on-board time on request
 *      or autonomously at the configured rate.
 *
 * Transactions (§3.2.2):
 *   2.1 Set rate:    ground → OBC (no mandatory response)
 *   2.3 Time req:    ground → OBC
 *   2.2 Time report: OBC → ground (response to 2.3, or autonomous)
 *
 * Application callbacks required: none.
 * Hardware dependency: osal_get_time_cuc() must be wired to the
 *   STM32 RTC via rtc_get_seconds() in bsp_rtc.c (Item 2.7A).
 */

#include "../slap_dispatcher.h"
#include "../slap_secondary_headers.h"
#include "../slap_types.h"
#include "../osal/osal.h"
#include "../hal/hal.h"
#include "slap_service_defs.h"
#include <string.h>

/* ----------------------------------------------------------------
 * MODULE STATE
 *
 * g_time_report_rate: current autonomous broadcast period in seconds.
 * 0 = disabled (no autonomous reports generated).
 * Set by message 2.1. Range: 0 to ~49710 days (uint32 seconds).
 *
 * Persistence: this value is lost on OBC reset. The ground station
 * must re-command it after each boot. If persistence is required in
 * a future version, save to non-volatile memory here.
 * ---------------------------------------------------------------- */
static uint32_t g_time_report_rate = 0U;
/* ----------------------------------------------------------------
 * SERVICE HANDLER
 * ---------------------------------------------------------------- */

int slap_service_time_management(slap_packet_t *req, slap_packet_t *resp)
{
    uint8_t msg = req->primary_header.msg_type;

    /* ----------------------------------------------------------
     * MSG 2.1 — Set the time report generation rate
     *
     * Configures how often the service autonomously broadcasts 2.2
     * packets (useful for system-wide clock synchronisation).
     * A value of 0 disables autonomous broadcasting.
     * The spec does not mandate a response — we return SLAP_OK
     * without populating resp. slap_core.c will not transmit resp
     * if the service returns SLAP_OK with data_len == 0 and no
     * response was built. TODO: confirm with ground station team
     * whether an ACK response is expected.
     * ---------------------------------------------------------- */
    if (msg == SLAP_MSG_TM_SET_RATE) {
         g_time_report_rate =
            req->secondary_header.time_rate.generation_rate;
        /* No response — return OK without populating resp */
        (void)resp;
        return SLAP_OK;
    }

    /* ----------------------------------------------------------
     * MSG 2.3 — On-board time request → 2.2 On-board time report
     *
     * Ground explicitly requests the current on-board time.
     * 2.3 has no secondary header and no data field.
     * We respond with 2.2 which carries the 7-byte CUC timestamp
     * in data[] (no secondary header for 2.2 either).
     * ---------------------------------------------------------- */
    if (msg == SLAP_MSG_TM_TIME_REQ) {
        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = SLAP_SVC_TIME_MANAGEMENT;
        resp->primary_header.msg_type     = SLAP_MSG_TM_TIME_REPORT;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
 
        /* 2.2 has no secondary header — time goes straight into data[] */
        osal_get_time_cuc(resp->data);
        resp->data_len = 7U;
 
        return SLAP_OK;
    }
 
    return SLAP_ERR_INVALID;
}

/* ----------------------------------------------------------------
 * AUTONOMOUS BROADCAST — call this from slap_sched_task_fn()
 *
 * Should be invoked every second (driven by SLAP_EVENT_SCHED_TICK).
 * Internally tracks elapsed seconds and only transmits when the
 * configured rate period has elapsed.
 *
 * @param tx_buf   wire buffer for the encoded broadcast packet
 * @param buf_size size of tx_buf (must be >= SLAP_MTU)
 * ---------------------------------------------------------------- */
void slap_time_broadcast(uint8_t *tx_buf, uint16_t buf_size)
{
    /* Counter tracking seconds since last broadcast */
    static uint32_t s_elapsed = 0U;

    if (g_time_report_rate == 0U)
        return; /* autonomous broadcasting disabled */

    s_elapsed++;
    if (s_elapsed < g_time_report_rate)
        return; /* period not yet elapsed */

    s_elapsed = 0U;

    /* Allocate a temporary packet struct from the static pool.
     * NEVER declare slap_packet_t as a local variable.           */
    slap_packet_t *pkt = slap_databank_alloc();
    if (pkt == NULL) return; /* pool exhausted — skip this tick */
 
    pkt->primary_header.packet_ver   = SLAP_PACKET_VER;
    pkt->primary_header.app_id       = 0x00U; /* broadcast */
    pkt->primary_header.service_type = SLAP_SVC_TIME_MANAGEMENT;
    pkt->primary_header.msg_type     = SLAP_MSG_TM_TIME_REPORT;
    pkt->primary_header.ack          = SLAP_ACK;
    pkt->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
    osal_get_time_cuc(pkt->data);
    pkt->data_len = 7U;
 
    int len = slap_encode_packet(pkt, tx_buf, buf_size);
    if (len > 0)
        hal_send(tx_buf, (uint16_t)len);
 
    slap_databank_free(pkt);
}
