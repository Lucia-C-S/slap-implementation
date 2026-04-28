/* services/3_position_management.c
 *
 * SLAP Service 3 — Position Management (0x03)
 *
 * Provides the spacecraft's estimated orbital position and velocity
 * for on-board awareness and ground monitoring purposes.
 *
 * Position encoding (resolved Item 0.2):
 *   Standard:  CCSDS 502.0-B-2 (Orbit Data Messages, ODM/OPM)
 *   Frame:     ECI J2000 (Earth-Centered Inertial)
 *   Encoding:  6 × IEEE-754 single-precision float, big-endian
 *              3 position components [km] + 3 velocity components [km/s]
 *              Total: 24 bytes
 *
 * Data field layout for message 3.2 (total 31 bytes):
 *   bytes [0..6]   — CUC 4.2 timestamp (7 bytes)
 *   bytes [7..10]  — X position [km]   (float32 big-endian)
 *   bytes [11..14] — Y position [km]   (float32 big-endian)
 *   bytes [15..18] — Z position [km]   (float32 big-endian)
 *   bytes [19..22] — Vx velocity [km/s](float32 big-endian)
 *   bytes [23..26] — Vy velocity [km/s](float32 big-endian)
 *   bytes [27..30] — Vz velocity [km/s](float32 big-endian)
 *
 * Position data source: on-board SGP4 propagator or ADCS subsystem.
 * Limited accuracy compared to ground-based orbit determination —
 * sufficient for monitoring and visualisation (§3.3).
 *
 * Transactions (§3.3.2):
 *   3.1 Position request:  ground → OBC  (no secondary header)
 *   3.2 Position report:   OBC → ground  (no secondary header, data above)
 *
 * Application callbacks required (slap_app_interface.h):
 *   position_get() — returns serialised ECI state vector (24 bytes)
 */
#include "../slap_dispatcher.h"
#include "../slap_types.h"
#include "../osal/osal.h"
#include "../slap_app_interface.h"
#include "slap_service_defs.h"
#include <string.h>

/* ----------------------------------------------------------------
 * FLOAT SERIALISATION HELPERS
 *
 * Packs/unpacks IEEE-754 single-precision floats to/from big-endian
 * bytes without relying on platform endianness.
 * Uses memcpy for bit-exact copy — avoids strict-aliasing UB.
 * ---------------------------------------------------------------- */
static void pack_float_be(uint8_t *b, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof(uint32_t));
    b[0] = (uint8_t)(bits >> 24);
    b[1] = (uint8_t)(bits >> 16);
    b[2] = (uint8_t)(bits >>  8);
    b[3] = (uint8_t)(bits);
}

static float unpack_float_be(const uint8_t *b)
{
    uint32_t bits = ((uint32_t)b[0] << 24)
                  | ((uint32_t)b[1] << 16)
                  | ((uint32_t)b[2] <<  8)
                  |  (uint32_t)b[3];
    float v;
    memcpy(&v, &bits, sizeof(float));
    return v;
}

/* ----------------------------------------------------------------
 * SERVICE HANDLER
 * ---------------------------------------------------------------- */

int slap_service_position_management(slap_packet_t *req,
                                      slap_packet_t *resp)
{
    /* Only the Position Request message type is a valid inbound request */
    if (req->primary_header.msg_type != SLAP_MSG_POS_REQUEST)
        return SLAP_ERR_INVALID;

    /* Neither 3.1 nor 3.2 have a secondary header (§3.3.1).
     * Time and position data go directly into data[].              */
    resp->primary_header.packet_ver   = SLAP_PACKET_VER;
    resp->primary_header.app_id       = req->primary_header.app_id;
    resp->primary_header.service_type = SLAP_SVC_POSITION_MANAGEMENT;
    resp->primary_header.msg_type     = SLAP_MSG_POS_REPORT;
    resp->primary_header.ack          = SLAP_ACK;
    resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
    resp->data_len                    = 0U;

    /* Bytes [0..6]: CUC 4.2 timestamp (time of report generation).
     * Uses osal_get_time_cuc() which reads the STM32 RTC.          */
    osal_get_time_cuc(resp->data);

    /* Bytes [7..30]: ECI J2000 state vector from ADCS/SGP4.
     * position_get() must write exactly POS_STATE_VECTOR_BYTES (24)
     * bytes in big-endian float format. See slap_app_interface.h.  */
    uint16_t written = 0U;
    int r = position_get(resp->data + 7U,
                          (uint16_t)(SLAP_MAX_DATA - 7U),
                          &written);

    if (r != SLAP_OK || written != SLAP_POS_STATE_VECTOR_BYTES) {
        /* Position data unavailable (SGP4 not initialised, ADCS fault) */
        resp->primary_header.ack = SLAP_NACK;
        resp->data_len = 7U; /* timestamp only — position is zeroed */
    } else {
        resp->data_len = 7U + written; /* = 31 bytes */
    }

    return SLAP_OK;
}

/* ----------------------------------------------------------------
 * POSITION PARSING UTILITY (for ground-side or test use)
 *
 * Extracts the ECI state vector from a received 3.2 data field.
 * @param data     pointer to msg 3.2 data[] (31 bytes)
 * @param data_len must be >= 31
 * @param ts_out   output: 7-byte CUC timestamp (may be NULL)
 * @param x  y  z  output: ECI position in km
 * @param vx vy vz output: ECI velocity in km/s
 * @return SLAP_OK or SLAP_ERR_INVALID if data is too short
 * ---------------------------------------------------------------- */
int slap_position_parse_report(const uint8_t *data, uint16_t data_len,
                                uint8_t ts_out[7],
                                float *x,  float *y,  float *z,
                                float *vx, float *vy, float *vz)
{
    if (data_len < (uint16_t)(7U + SLAP_POS_STATE_VECTOR_BYTES))
        return SLAP_ERR_INVALID;

    if (ts_out != NULL)
        memcpy(ts_out, data, 7U);

    if (x)  *x  = unpack_float_be(data + 7U);
    if (y)  *y  = unpack_float_be(data + 11U);
    if (z)  *z  = unpack_float_be(data + 15U);
    if (vx) *vx = unpack_float_be(data + 19U);
    if (vy) *vy = unpack_float_be(data + 23U);
    if (vz) *vz = unpack_float_be(data + 27U);

    return SLAP_OK;
}