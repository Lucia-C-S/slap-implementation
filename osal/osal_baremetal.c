// osal_baremetal.c
// For targets without FreeRTOS: ARM Cortex-M bare-metal
// Replace HAL_xxx calls with your actual MCU HAL (STM32, etc.)
// use for the actual flight target
// Your build system (Makefile or CMake) then selects which osal_xxx.c to compile. 

#include "osal.h"
#include <string.h>

/* ---- TIME ---- */
/* You need a hardware RTC or a 64-bit counter driven by a timer ISR.
 * This example assumes a 32-bit seconds counter + 24-bit sub-second
 * counter at 1/2^24 second resolution, incremented by a timer ISR. */

extern volatile uint32_t g_rtc_coarse;  /* seconds since 1958 TAI epoch */
extern volatile uint32_t g_rtc_fine;    /* sub-second ticks (24-bit used) */

void osal_get_time_cuc(uint8_t out[7])
{
    /* Read atomically — disable IRQ briefly */
    osal_enter_critical();
    uint32_t c = g_rtc_coarse;
    uint32_t f = g_rtc_fine;
    osal_exit_critical();

    /* Coarse: 4 bytes big-endian */
    out[0] = (uint8_t)(c >> 24);
    out[1] = (uint8_t)(c >> 16);
    out[2] = (uint8_t)(c >> 8);
    out[3] = (uint8_t)(c);
    /* Fine: 3 bytes big-endian (upper 24 bits of fractional part) */
    out[4] = (uint8_t)(f >> 16);
    out[5] = (uint8_t)(f >> 8);
    out[6] = (uint8_t)(f);
}

uint64_t osal_get_time_raw(void)
{
    osal_enter_critical();
    uint64_t t = ((uint64_t)g_rtc_coarse << 24) | (g_rtc_fine & 0xFFFFFF);
    osal_exit_critical();
    return t;
}

void osal_delay_ms(uint32_t ms)
{
    /* Replace with your MCU busy-wait or SysTick-based delay */
    volatile uint32_t count = ms * 8000; /* rough for 8 MHz */
    while (count--) {}
}

/* ---- CRITICAL SECTIONS (ARM Cortex-M) ---- */
void osal_enter_critical(void) { __asm volatile ("cpsid i"); }
void osal_exit_critical(void)  { __asm volatile ("cpsie i"); }

/* ---- MUTEXES (bare-metal: just critical sections) ---- */
int  osal_mutex_create(osal_mutex_t *mtx)   { *mtx = NULL; return OSAL_OK; }
int  osal_mutex_lock(osal_mutex_t *mtx, uint32_t timeout_ms)
                                             { osal_enter_critical(); return OSAL_OK; }
int  osal_mutex_unlock(osal_mutex_t *mtx)   { osal_exit_critical(); return OSAL_OK; }
void osal_mutex_destroy(osal_mutex_t *mtx)  { (void)mtx; }

/* ---- QUEUES (bare-metal: simple circular buffer of pointers) ---- */
/* For bare-metal single-loop use, queues are not needed — stubs only */
int  osal_queue_create(osal_queue_t *q, uint32_t d, uint32_t s)
                                             { (void)q;(void)d;(void)s; return OSAL_OK; }
int  osal_queue_send(osal_queue_t *q, const void *i, uint32_t t)
                                             { (void)q;(void)i;(void)t; return OSAL_OK; }
int  osal_queue_recv(osal_queue_t *q, void *i, uint32_t t)
                                             { (void)q;(void)i;(void)t; return OSAL_EMPTY; }
void osal_queue_destroy(osal_queue_t *q)    { (void)q; }

/* ---- TASKS (bare-metal: no tasks, single loop) ---- */
int  osal_task_create(osal_task_t *t, const char *n, osal_task_func_t f,
                      void *a, uint32_t ss, uint32_t p)
                                             { (void)t;(void)n;(void)f;(void)a;
                                               (void)ss;(void)p; return OSAL_ERR; }
void osal_task_delete(osal_task_t *t)       { (void)t; }
void osal_task_yield(void)                  {}

/* ---- MEMORY (bare-metal: static arena) ---- */
#define OSAL_HEAP_SIZE 4096
static uint8_t  g_heap[OSAL_HEAP_SIZE];
static uint32_t g_heap_top = 0;

void *osal_malloc(uint32_t size)
{
    /* Extremely simple bump allocator — no free */
    if (g_heap_top + size > OSAL_HEAP_SIZE) return NULL;
    void *p = &g_heap[g_heap_top];
    g_heap_top += size;
    return p;
}
void osal_free(void *ptr) { (void)ptr; /* no-op on bare-metal arena */ }

/* ---- WATCHDOG ---- */
void osal_watchdog_init(uint32_t timeout_ms)
{
    /* Example: STM32 IWDG init — replace with your MCU's watchdog API */
    (void)timeout_ms;
    /* IWDG_Init(timeout_ms); */
}
void osal_watchdog_kick(void)
{
    /* IWDG_ReloadCounter(); */
}

/* ---- INIT ---- */
void osal_init(void)
{
    g_heap_top = 0;
    /* Initialize your hardware timer for osal_get_time_cuc() here */
}