#include <stdio.h>
#include "user_led.h"


/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO CONFIG_BLINK_GPIO
static uint8_t s_led_state = 0;

static const char *LED_TAG = "LED";


static void configure_led(led_strip_handle_t *led_strip_p);
static void led_struct_fill(led_str_t *leds_p, uint32_t red, uint32_t green, uint32_t blue, uint32_t blink_period_ms);
static void led_write_refresh(led_strip_handle_t *strip_p, led_str_t *leds_p);


static void configure_led(led_strip_handle_t *led_strip_p)
{
    ESP_LOGI(LED_TAG, "Example configured to blink addressable LED!");
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };
#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, led_strip_p));
#elif CONFIG_BLINK_LED_STRIP_BACKEND_SPI
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, led_strip_p));
#else
#error "unsupported LED strip backend"
#endif
    /* Set all LED off to clear all pixels */
    led_strip_clear(*led_strip_p);
}

static void led_struct_fill(led_str_t *leds_p, uint32_t red, uint32_t green, uint32_t blue, uint32_t blink_period_ms)
{
    leds_p->red = red;
    leds_p->green = green;
    leds_p->blue = blue;
    leds_p->blink_period_ms = blink_period_ms;
}

static void led_write_refresh(led_strip_handle_t *strip_p, led_str_t *leds_p)
{
    int64_t now = esp_timer_get_time();
    int64_t blink_time_elapsed = now - leds_p->last_blink_time;
    if (blink_time_elapsed > leds_p->blink_period_ms * 1000)
    {
        if (false == leds_p->is_led_on)
        {
            led_strip_set_pixel(*strip_p, leds_p->led_num, leds_p->red, leds_p->green, leds_p->blue);
            led_strip_refresh(*strip_p);
            leds_p->is_led_on = true;
        }
        else if (true == leds_p->is_led_on && leds_p->blink_period_ms > 0)
        {
            //do not clear strip if there is no blinks
            led_strip_clear(*strip_p);
            leds_p->is_led_on = false;
        }
        leds_p->last_blink_time = now;
    }
}

#define LED_BLINK_PERIOD (uint32_t)500

void led_task_handler(void *pvParameters)
{
    ESP_LOGI(LED_TAG, "LED task started\r\n");

    led_strip_handle_t led_strip;

    led_str_t local_led = {0, };

    configure_led(&led_strip);

    uint32_t led_state_bits = 0;

    while (1)
    {
        BaseType_t led_notify_ret = xTaskNotifyWait(0x00, ULONG_MAX, &led_state_bits, pdMS_TO_TICKS(10));
        if (led_notify_ret == pdPASS)
        {
            uint32_t led_state = log2(led_state_bits);
            switch(led_state)
            {
                case(LED_CMD_OFF):
                {
                    led_struct_fill(&local_led, 0, 0, 0, 0);
                }
                break;
                case(LED_CMD_BLINK_GREEN):
                {
                    led_struct_fill(&local_led, 0, 20, 0, LED_BLINK_PERIOD);
                }
                break;
                case(LED_CMD_SOLID_GREEN):
                {
                    led_struct_fill(&local_led, 0, 20, 0, 0);
                }
                break;
                case(LED_CMD_BLINK_BLUE):
                {
                    led_struct_fill(&local_led, 0, 0, 20, LED_BLINK_PERIOD);
                }
                break;
                case(LED_CMD_SOLID_BLUE):
                {
                    led_struct_fill(&local_led, 0, 0, 20, 0);
                }
                break;
                case(LED_CMD_BLINK_YELLOW):
                {
                    led_struct_fill(&local_led, 30, 20, 0, LED_BLINK_PERIOD);
                }
                break;
                case(LED_CMD_SOLID_YELLOW):
                {
                    led_struct_fill(&local_led, 30, 20, 0, 0);
                }
                break;
                case(LED_CMD_BLINK_RED):
                {
                    led_struct_fill(&local_led, 20, 0, 0, LED_BLINK_PERIOD);
                }
                break;
                default:
                {
                    ESP_LOGE(LED_TAG, "Unknown LED state\r\n");
                }
                break;
            }
        }
        led_write_refresh(&led_strip, &local_led);
    }
}