// services/4_time_based_scheduling.c
#include "4_time_based_scheduling.h"
#include "slap_types.h"
#include "osal.h"
#include <string.h>
#include <stdio.h>  /* snprintf for CSV */

#define MSG_ENABLE          1
#define MSG_DISABLE         2
#define MSG_INSERT          3
#define MSG_INSERT_REPORT   4
#define MSG_UPDATE_RELEASE  5
#define MSG_RESET           6
#define MSG_TABLE_SIZE_REQ  7
#define MSG_TABLE_SIZE_RESP 8
#define MSG_TABLE_DATA_REQ  9
#define MSG_TABLE_DATA_RESP 10

static sched_entry_t g_schedule[SCHED_MAX_ENTRIES];
static uint8_t       g_sched_enabled = 0;
static uint16_t      g_next_entry_id = 1;

int slap_service_time_based_scheduling(slap_packet_t *req, slap_packet_t *resp)
{
    uint8_t msg = req->primary_header.msg_type;

    /* 4.1 Enable */
    if (msg == MSG_ENABLE) {
        g_sched_enabled = 1;
        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = 0x04;
        resp->primary_header.msg_type     = MSG_ENABLE;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
        resp->sec_header_len = 0;
        resp->data_len       = 0;
        return SLAP_OK;
    }

    /* 4.2 Disable */
    if (msg == MSG_DISABLE) {
        g_sched_enabled = 0;
        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = 0x04;
        resp->primary_header.msg_type     = MSG_DISABLE;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
        resp->sec_header_len = 0;
        resp->data_len       = 0;
        return SLAP_OK;
    }

    /* 4.3 Insert telecommand
     * Secondary header (from raw data): release_time(7B) + tc_length(2B)
     * Data: tc string */
    if (msg == MSG_INSERT) {
        if (req->data_len < 9)
            return SLAP_ERR_INVALID;

        /* Parse secondary header embedded in raw payload */
        uint64_t rel_time = 0;
        for (int i = 0; i < 7; i++)
            rel_time = (rel_time << 8) | req->data[i];
        uint16_t tc_len = ((uint16_t)req->data[7] << 8) | req->data[8];

        if (tc_len > SCHED_TC_MAX_LEN || req->data_len < (uint16_t)(9 + tc_len))
            return SLAP_ERR_INVALID;

        /* Find free slot */
        int slot = -1;
        for (int i = 0; i < SCHED_MAX_ENTRIES; i++) {
            if (!g_schedule[i].valid) { slot = i; break; }
        }
        if (slot < 0)
            return SLAP_ERR_NOMEM;

        g_schedule[slot].entry_id    = g_next_entry_id++;
        g_schedule[slot].release_time = rel_time;
        g_schedule[slot].tc_len      = tc_len;
        memcpy(g_schedule[slot].tc, req->data + 9, tc_len);
        g_schedule[slot].tc[tc_len]  = '\0';
        g_schedule[slot].valid       = 1;

        /* Build 4.4 response: entry_id in secondary header */
        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = 0x04;
        resp->primary_header.msg_type     = MSG_INSERT_REPORT;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
        resp->secondary_header[0] = (uint8_t)(g_schedule[slot].entry_id >> 8);
        resp->secondary_header[1] = (uint8_t)(g_schedule[slot].entry_id);
        resp->sec_header_len = 2;
        resp->data_len       = 0;
        return SLAP_OK;
    }

    /* 4.5 Update release time */
    if (msg == MSG_UPDATE_RELEASE) {
        if (req->data_len < 9)
            return SLAP_ERR_INVALID;
        uint16_t entry_id = ((uint16_t)req->data[0] << 8) | req->data[1];
        uint64_t new_time = 0;
        for (int i = 0; i < 7; i++)
            new_time = (new_time << 8) | req->data[2 + i];

        for (int i = 0; i < SCHED_MAX_ENTRIES; i++) {
            if (g_schedule[i].valid && g_schedule[i].entry_id == entry_id) {
                if (new_time == 0)
                    g_schedule[i].valid = 0; /* cancel */
                else
                    g_schedule[i].release_time = new_time;
                return SLAP_OK;
            }
        }
        return SLAP_ERR_INVALID;
    }

    /* 4.6 Reset */
    if (msg == MSG_RESET) {
        g_sched_enabled = 0;
        memset(g_schedule, 0, sizeof(g_schedule));
        g_next_entry_id = 1;
        return SLAP_OK;
    }

    /* 4.7 Table size request → 4.8 */
    if (msg == MSG_TABLE_SIZE_REQ) {
        /* Build CSV to measure its size */
        uint32_t csv_size = 0;
        for (int i = 0; i < SCHED_MAX_ENTRIES; i++) {
            if (g_schedule[i].valid)
                csv_size += 10 + g_schedule[i].tc_len; /* rough estimate */
        }
        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = 0x04;
        resp->primary_header.msg_type     = MSG_TABLE_SIZE_RESP;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
        resp->secondary_header[0] = (uint8_t)(csv_size >> 24);
        resp->secondary_header[1] = (uint8_t)(csv_size >> 16);
        resp->secondary_header[2] = (uint8_t)(csv_size >> 8);
        resp->secondary_header[3] = (uint8_t)(csv_size);
        resp->sec_header_len = 4;
        resp->data_len       = 0;
        return SLAP_OK;
    }

    /* 4.9 Table data request → 4.10 (returns one segment) */
    if (msg == MSG_TABLE_DATA_REQ) {
        resp->primary_header.packet_ver   = SLAP_PACKET_VER;
        resp->primary_header.app_id       = req->primary_header.app_id;
        resp->primary_header.service_type = 0x04;
        resp->primary_header.msg_type     = MSG_TABLE_DATA_RESP;
        resp->primary_header.ack          = SLAP_ACK;
        resp->primary_header.ecf_flag     = SLAP_ECF_PRESENT;
        resp->sec_header_len = 0;

        uint16_t offset = 0;
        for (int i = 0; i < SCHED_MAX_ENTRIES && offset < SLAP_MAX_DATA - 32; i++) {
            if (!g_schedule[i].valid) continue;
            int written = snprintf((char *)resp->data + offset,
                                   SLAP_MAX_DATA - offset,
                                   "%u,%s,%llu\n",
                                   g_schedule[i].entry_id,
                                   g_schedule[i].tc,
                                   (unsigned long long)g_schedule[i].release_time);
            if (written > 0) offset += (uint16_t)written;
        }
        resp->data_len = offset;
        return SLAP_OK;
    }

    return SLAP_ERR_INVALID;
}

/* Call from a timer task — executes commands whose release time has arrived */
void slap_scheduling_tick(uint64_t current_time)
{
    if (!g_sched_enabled) return;
    for (int i = 0; i < SCHED_MAX_ENTRIES; i++) {
        if (g_schedule[i].valid && g_schedule[i].release_time <= current_time) {
            /* TODO: dispatch g_schedule[i].tc to your command executor */
            g_schedule[i].valid = 0;
        }
    }
}

