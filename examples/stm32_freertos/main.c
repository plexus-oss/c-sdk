/**
 * @file main.c
 * @brief STM32F4 + FreeRTOS + LwIP telemetry example for Plexus C SDK
 *
 * This example targets STM32F446RE (Nucleo-F446RE) with:
 * - FreeRTOS for task scheduling
 * - LwIP for TCP/IP networking
 * - UART2 for debug output
 *
 * Prerequisites:
 *   1. CubeMX-generated project with FreeRTOS + LwIP enabled
 *   2. Ethernet/WiFi module connected and configured
 *   3. Set PLEXUS_API_KEY and PLEXUS_SOURCE_ID below
 *
 * Build:
 *   STM32CubeIDE or PlatformIO with stm32cube framework
 *
 * Note: The STM32 HAL uses HTTP (not HTTPS). For HTTPS, integrate mbedTLS
 * with LwIP's altcp_tls layer.
 */

#include <stdio.h>
#include <string.h>

#include "stm32f4xx_hal.h"
#include "cmsis_os.h"
#include "lwip/init.h"
#include "lwip/netif.h"

#include "plexus.h"

/* ========================================================================= */
/* Configuration - Update these values                                       */
/* ========================================================================= */

#define PLEXUS_API_KEY   "plx_your_api_key_here"
#define PLEXUS_SOURCE_ID "stm32-sensor-001"

/* Use HTTP endpoint (STM32 HAL does not support HTTPS without mbedTLS) */
#define PLEXUS_HTTP_ENDPOINT "http://app.plexus.company/api/ingest"

#define TELEMETRY_INTERVAL_MS 5000

/* ========================================================================= */
/* Hardware handles (required by hal/stm32/plexus_hal_stm32.c)              */
/* ========================================================================= */

UART_HandleTypeDef huart2;
RTC_HandleTypeDef hrtc;

/* ========================================================================= */
/* UART init for debug output                                                */
/* ========================================================================= */

static void uart_init(void) {
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;  /* PA2=TX, PA3=RX */
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2);
}

/* printf redirect to UART */
int _write(int fd, char* ptr, int len) {
    (void)fd;
    HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    return len;
}

/* ========================================================================= */
/* Network initialization stub                                               */
/* ========================================================================= */

/**
 * IMPORTANT: Replace this function with your CubeMX-generated LwIP
 * initialization code. The exact implementation depends on your network
 * hardware (Ethernet PHY, WiFi module, etc.).
 *
 * For Nucleo boards with Ethernet:
 *   - Enable ETH peripheral in CubeMX
 *   - Enable LwIP middleware with DHCP
 *   - CubeMX generates MX_LWIP_Init() automatically
 *
 * For WiFi modules (ESP-AT, WizFi360, etc.):
 *   - Configure the AT command interface
 *   - Bridge to LwIP or use the module's built-in TCP/IP stack
 */
static void network_init_stub(void) {
    printf("Network init: replace this stub with CubeMX LwIP init\r\n");

    /* Example for CubeMX-generated projects:
     *   MX_LWIP_Init();
     *
     * Then wait for DHCP:
     *   while (!netif_is_up(netif_default)) {
     *       osDelay(100);
     *   }
     */
}

/* ========================================================================= */
/* Simulated sensor readings                                                 */
/* ========================================================================= */

static float read_temperature(void) {
    /* Replace with real ADC/I2C sensor read */
    static float base = 25.0f;
    base += 0.1f;
    if (base > 35.0f) base = 20.0f;
    return base;
}

static float read_pressure(void) {
    /* Replace with real sensor read */
    static float base = 1013.0f;
    base += 0.5f;
    if (base > 1025.0f) base = 1005.0f;
    return base;
}

static int read_alarm_state(void) {
    /* Replace with real GPIO read */
    return 0;
}

/* ========================================================================= */
/* Telemetry task                                                            */
/* ========================================================================= */

static void telemetry_task(void const* argument) {
    (void)argument;

    printf("Plexus SDK v%s\r\n", plexus_version());

    /* Wait for network to be ready */
    network_init_stub();
    osDelay(2000);

    /* Initialize Plexus client */
    plexus_client_t* client = plexus_init(PLEXUS_API_KEY, PLEXUS_SOURCE_ID);
    if (!client) {
        printf("ERROR: Failed to initialize Plexus client\r\n");
        osThreadTerminate(NULL);
        return;
    }

    /* Use HTTP endpoint (STM32 HAL is HTTP-only) */
    plexus_set_endpoint(client, PLEXUS_HTTP_ENDPOINT);

    /* Configure flush behavior */
    plexus_set_flush_interval(client, TELEMETRY_INTERVAL_MS);

    printf("Starting telemetry loop (interval: %dms)\r\n", TELEMETRY_INTERVAL_MS);

    for (;;) {
        float temp     = read_temperature();
        float pressure = read_pressure();
        int alarm      = read_alarm_state();

        printf("Readings: temp=%.1fC pressure=%.1fhPa alarm=%d\r\n",
               temp, pressure, alarm);

        /* Queue metrics */
        plexus_send_number(client, "temperature", (double)temp);
        plexus_send_number(client, "pressure", (double)pressure);
#if PLEXUS_ENABLE_BOOL_VALUES
        plexus_send_bool(client, "alarm", alarm != 0);
#endif

        /* Let plexus_tick() handle time-based auto-flush.
         * It flushes when the configured interval elapses. */
        plexus_err_t err = plexus_tick(client);

        if (err == PLEXUS_OK) {
            printf("Telemetry sent (%lu total)\r\n",
                   (unsigned long)plexus_total_sent(client));
        } else if (err == PLEXUS_ERR_NO_DATA) {
            /* No data or interval hasn't elapsed yet — normal */
        } else if (err == PLEXUS_ERR_AUTH) {
            printf("FATAL: Authentication failed — check API key\r\n");
            break;  /* Stop on auth failure */
        } else if (err == PLEXUS_ERR_NETWORK) {
            printf("Network error — will retry next cycle\r\n");
            /* Metrics stay in buffer for next flush attempt */
        } else {
            printf("Flush error: %s\r\n", plexus_strerror(err));
        }

        osDelay(1000);
    }

    plexus_free(client);
    osThreadTerminate(NULL);
}

/* ========================================================================= */
/* Main                                                                      */
/* ========================================================================= */

int main(void) {
    HAL_Init();

    /* SystemClock_Config() — replace with CubeMX-generated clock config */
    /* SystemClock_Config(); */

    uart_init();
    printf("\r\n=== Plexus STM32 FreeRTOS Example ===\r\n");

    /* Create telemetry task */
    osThreadDef(telemetryTask, telemetry_task, osPriorityNormal, 0, 2048);
    osThreadCreate(osThread(telemetryTask), NULL);

    /* Start FreeRTOS scheduler */
    osKernelStart();

    /* Should never reach here */
    for (;;) {}
}
