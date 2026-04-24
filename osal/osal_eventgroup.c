// osal/osal_eventgroup.c
#include "osal_eventgroup.h"
#include "osal.h"
#include "FreeRTOS.h"
#include "event_groups.h"

int osal_eventgroup_create(osal_eventgroup_t *eg)
{
    *eg = (osal_eventgroup_t)xEventGroupCreate();
    return (*eg != NULL) ? OSAL_OK : OSAL_ERR;
}

int osal_eventgroup_set(osal_eventgroup_t *eg, osal_event_bits_t bits)
{
    xEventGroupSetBits((EventGroupHandle_t)*eg, (EventBits_t)bits);
    return OSAL_OK;
}

int osal_eventgroup_set_from_isr(osal_eventgroup_t *eg,
                                  osal_event_bits_t   bits)
{
    BaseType_t higher_prio_woken = pdFALSE;
    xEventGroupSetBitsFromISR((EventGroupHandle_t)*eg,
                              (EventBits_t)bits,
                              &higher_prio_woken);
    portYIELD_FROM_ISR(higher_prio_woken);
    return OSAL_OK;
}

int osal_eventgroup_clear(osal_eventgroup_t *eg, osal_event_bits_t bits)
{
    xEventGroupClearBits((EventGroupHandle_t)*eg, (EventBits_t)bits);
    return OSAL_OK;
}

osal_event_bits_t osal_eventgroup_wait(osal_eventgroup_t *eg,
                                        osal_event_bits_t   bits,
                                        uint8_t             wait_all,
                                        uint8_t             clear_on_exit,
                                        uint32_t            timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF)
                     ? portMAX_DELAY
                     : pdMS_TO_TICKS(timeout_ms);

    return (osal_event_bits_t)xEventGroupWaitBits(
        (EventGroupHandle_t)*eg,
        (EventBits_t)bits,
        (BaseType_t)clear_on_exit,
        (BaseType_t)wait_all,
        ticks
    );
}

void osal_eventgroup_destroy(osal_eventgroup_t *eg)
{
    vEventGroupDelete((EventGroupHandle_t)*eg);
    *eg = NULL;
}