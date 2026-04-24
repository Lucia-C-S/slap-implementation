/* main.c — application entry point */
#include "slap_core.h"
#include "FreeRTOS.h"
#include "task.h"

int main(void)
{
    /* Board-level hardware init (clocks, GPIOs, UART, RTC).
     * This is your BSP — not part of SLAP.                         */
    board_init();

    /* Initialise the SLAP stack and create its tasks.              */
    if (slap_init() != SLAP_OK) {
        /* Init failed — enter safe mode or blink an error LED.     */
        error_handler();
    }

    /* Start the FreeRTOS scheduler. This call never returns.
     * The two SLAP tasks begin executing from here.                */
    vTaskStartScheduler();

    /* Should never reach here on a working system.                 */
    while (1) {}
}