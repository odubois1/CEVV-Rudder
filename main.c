#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

#include <string.h>

#include "bsp/board.h"
#include "tusb.h"

#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum
{
    BLINK_NOT_MOUNTED = 250,
    BLINK_MOUNTED = 1000,
    BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

void led_blinking_task(void);
void hid_task(int);

#define MAX_VAL 2048.0

/* Read a power of 2 number of samples and return their average */
uint16_t __not_in_flash_func(adc_capture)(uint8_t pow)
{

    uint16_t count = (1 << pow) - 1;
    uint16_t res;
    uint32_t total = 0;

    adc_fifo_setup(true, false, 0, true, false);
    adc_run(true);

    for (int i = 0; i <= count; i = i + 1)
    {
        res = adc_fifo_get_blocking();

        total += (uint32_t)res;
    }

    adc_run(false);
    adc_fifo_drain();

    return (total >> pow);
}

// interpolation using only integer from arduino library
// A bit of type use optimization may be usefull.
long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/*------------- MAIN -------------*/
int main(void)
{
    uint16_t res, min, max, off;
    uint32_t start;
    int32_t scaledz;

    board_init();
    //tusb_init();

    // Set power supply to continuous mode to reduce adc noise
    gpio_pull_up(23);

    min = 4090;
    max = 0;

    //stdio_usb_init();
    stdio_init_all();

    // set ADC for measurement
    adc_init();
    adc_gpio_init(26);
    adc_select_input(0);

    while (1)
    {
        res = adc_capture(6);
        if (res < min)
        {
            min = res;
        }
        if (res > max)
        {
            max = res;
        }

        scaledz = map(res, min, max, 0, +MAX_VAL);
        // printf("%05i %05i\n", res, scaledz);
        printf("%05i %05i\n", res, scaledz);

        tud_task(); // tinyusb device task
        led_blinking_task();

        hid_task(scaledz);
    }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    blink_interval_ms = BLINK_MOUNTED;
}

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+


// Every 10ms, we will sent 1 report if change ?
void hid_task(int Z)
{
    // Poll every 10ms
    const uint32_t interval_ms = 10;
    static uint32_t start_ms = 0;
    static HID_JoystickReport_Data_t report = { 0 };

    if (board_millis() - start_ms < interval_ms)
        return; // not enough time
    start_ms += interval_ms;

    uint32_t const btn = board_button_read();

    // Remote wakeup
    if (tud_suspended() && btn)
    {
        // Wake up host if we are in suspend mode
        // and REMOTE_WAKEUP feature is enabled by host
        tud_remote_wakeup();
    }
    else
    {
      /*------------- Joystick -------------*/
      if ((tud_hid_ready())&& ((uint16_t)Z != report.zAxis)) {
          report.zAxis = (uint16_t)Z;
          tud_hid_report(0x00, &report, sizeof(report));
      }
    }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    // TODO not Implemented
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;

    if (report_type == HID_REPORT_TYPE_OUTPUT)
    {
        // Set keyboard LED e.g Capslock, Numlock etc...
        if (report_id == REPORT_ID_KEYBOARD)
        {
            // bufsize should be (at least) 1
            if (bufsize < 1)
                return;

            uint8_t const kbd_leds = buffer[0];

            if (kbd_leds & KEYBOARD_LED_CAPSLOCK)
            {
                // Capslock On: disable blink, turn led on
                blink_interval_ms = 0;
                board_led_write(true);
            }
            else
            {
                // Caplocks Off: back to normal blink
                board_led_write(false);
                blink_interval_ms = BLINK_MOUNTED;
            }
        }
    }
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
    static uint32_t start_ms = 0;
    static bool led_state = false;

    // blink is disabled
    if (!blink_interval_ms)
        return;

    // Blink every interval ms
    if (board_millis() - start_ms < blink_interval_ms)
        return; // not enough time
    start_ms += blink_interval_ms;

    board_led_write(led_state);
    led_state = 1 - led_state; // toggle
}