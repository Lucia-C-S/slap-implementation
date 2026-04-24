/* hal/hal.c
 *
 * Hardware Abstraction Layer — transport implementation.
 *
 * Currently written for CSP over UART as a placeholder.
 * Replace the DRIVER comment blocks with your actual
 * transport driver calls once Item 0.1 is confirmed.
 *
 * For STM32 + UART DMA, the pattern is:
 *   hal_init()    -> HAL_UART_Receive_DMA(...)  (arms DMA reception)
 *   hal_send()    -> HAL_UART_Transmit(...)     (blocking or DMA TX)
 *   hal_receive() -> copies from DMA buffer     (called by slap_core)
 *
 * The ISR callback (HAL_UART_RxCpltCallback) calls
 * slap_core_notify_rx() to wake the SLAP receive task.
 */

#include "hal.h"
#include "../slap_core.h"   /* for slap_core_notify_rx() */
#include <string.h>

/* DMA receive buffer — filled by hardware, read by hal_receive().
 * Declared static so it is never on the stack.                    */
static uint8_t g_dma_rx_buf[SLAP_MTU];
static uint16_t g_dma_rx_len = 0;   /* set by the DMA complete ISR */

void hal_init(void)
{
    /* DRIVER: initialise your physical transport here.
     * Examples:
     *   STM32 UART DMA: HAL_UART_Receive_DMA(&huart2, g_dma_rx_buf, SLAP_MTU);
     *   CSP:            csp_init(); csp_uart_init(...);
     *   SPI:            HAL_SPI_Init(&hspi1);
     */
}

int hal_send(const uint8_t *data, uint16_t length)
{
    if (data == NULL || length == 0)
        return SLAP_ERR_INVALID;

    /* DRIVER: transmit `length` bytes from `data`.
     * Return the number of bytes actually sent, or SLAP_ERR_xxx.
     * Examples:
     *   STM32 blocking UART:
     *     HAL_StatusTypeDef r = HAL_UART_Transmit(&huart2,
     *                               (uint8_t*)data, length, 100);
     *     return (r == HAL_OK) ? length : SLAP_ERR_INVALID;
     *
     *   CSP:
     *     csp_packet_t *pkt = csp_buffer_get(length);
     *     memcpy(pkt->data, data, length);
     *     pkt->length = length;
     *     csp_send(conn, pkt, 0);
     *     return length;
     */

    (void)data;
    return (int)length; /* placeholder — replace with real driver */
}

int hal_receive(uint8_t *buffer, uint16_t max_len)
{
    if (buffer == NULL || max_len == 0)
        return SLAP_ERR_INVALID;

    /* DRIVER: copy received bytes into buffer.
     * For DMA-based reception: copy from g_dma_rx_buf which the
     * DMA hardware fills autonomously.
     *
     * Critical section protects g_dma_rx_len from being modified
     * by the DMA ISR mid-read.                                     */
    osal_enter_critical();
    uint16_t available = g_dma_rx_len;
    if (available == 0) {
        osal_exit_critical();
        return 0;  /* nothing received yet */
    }
    uint16_t copy_len = (available < max_len) ? available : max_len;
    memcpy(buffer, g_dma_rx_buf, copy_len);
    g_dma_rx_len = 0; /* mark buffer as consumed */
    osal_exit_critical();

    return (int)copy_len;
}

/* ----------------------------------------------------------------
 * HAL ISR CALLBACK — called by the transport driver when reception
 * is complete. This function wakes the SLAP receive task.
 *
 * For STM32 HAL UART: rename to HAL_UART_RxCpltCallback.
 * For CSP: call from your CSP receive hook.
 * ---------------------------------------------------------------- */
void hal_rx_complete_callback(uint16_t bytes_received)
{
    /* Store the received length so hal_receive() knows how much
     * data is available in g_dma_rx_buf.                          */
    g_dma_rx_len = bytes_received;

    /* Signal the SLAP core that data is ready.
     * This wakes slap_rx_task_fn() without polling.               */
    slap_core_notify_rx(g_dma_rx_buf, bytes_received);

    /* Re-arm DMA for the next packet.
     * DRIVER: HAL_UART_Receive_DMA(&huart2, g_dma_rx_buf, SLAP_MTU);
     */
}