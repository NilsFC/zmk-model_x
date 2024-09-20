#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/hid_indicators.h>
#include <zmk/events/hid_indicators_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LED_GPIO_NODE_ID DT_COMPAT_GET_ANY_STATUS_OKAY(gpio_leds)

// PWM configuration for Num Lock, Caps Lock, and Scroll Lock LEDs
static const struct pwm_dt_spec pwm_led_numlock = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led_numlock));
static const struct pwm_dt_spec pwm_led_capslock = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led_capslock));
static const struct pwm_dt_spec pwm_led_scrolllock = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led_scrolllock));

#define PWM_PERIOD_NS  20000000 // 20ms period (50Hz)

// Define the brightness percentage in idle as a modifiable constant
#define IDLE_BRIGHTNESS_PERCENT 25

// Global variables to store the current brightness for each LED
static uint8_t current_brightness_numlock = 0;
static uint8_t current_brightness_capslock = 0;
static uint8_t current_brightness_scrolllock = 0;

// Directly set PWM brightness for a given LED
static int direct_pwm_set(const struct pwm_dt_spec *pwm_spec, uint8_t brightness_percent, uint8_t *current_brightness) {
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }

    // Calculate the duty cycle in nanoseconds based on the percentage
    uint32_t duty_cycle_ns = (brightness_percent * PWM_PERIOD_NS) / 100;

    // Set the PWM with the calculated duty cycle
    int err = pwm_set_dt(pwm_spec, PWM_PERIOD_NS, duty_cycle_ns);
    if (err) {
        LOG_ERR("Error setting PWM: %d", err);
        return err;
    }

    // Update the current brightness
    *current_brightness = brightness_percent;

    LOG_INF("Set PWM brightness to %d%% for device %s", brightness_percent, pwm_spec->dev->name);
    return 0;
}

// Function called when the keyboard enters idle
static void on_idle(void) {
    printk("Keyboard is idle. Dimming LED brightness to %d%%.\n", IDLE_BRIGHTNESS_PERCENT);

    // Set the LED brightness to the percentage defined in IDLE_BRIGHTNESS_PERCENT
    direct_pwm_set(&pwm_led_numlock, (current_brightness_numlock * IDLE_BRIGHTNESS_PERCENT) / 100, &current_brightness_numlock);
    direct_pwm_set(&pwm_led_capslock, (current_brightness_capslock * IDLE_BRIGHTNESS_PERCENT) / 100, &current_brightness_capslock);
    direct_pwm_set(&pwm_led_scrolllock, (current_brightness_scrolllock * IDLE_BRIGHTNESS_PERCENT) / 100, &current_brightness_scrolllock);
}

// Function called when the keyboard wakes up from idle
static void on_wake_up(void) {
    printk("Keyboard has woken up from idle. Restoring LED brightness.\n");

    // Restore the original brightness of the LEDs based on the lock indicators
    zmk_hid_indicators_t flags = zmk_hid_indicators_get_current_profile();

    uint8_t numlock_brightness = (flags & 0x01) ? 100 : 0;
    uint8_t capslock_brightness = (flags & 0x02) ? 100 : 0;
    uint8_t scrolllock_brightness = (flags & 0x04) ? 100 : 0;

    direct_pwm_set(&pwm_led_numlock, numlock_brightness, &current_brightness_numlock);
    direct_pwm_set(&pwm_led_capslock, capslock_brightness, &current_brightness_capslock);
    direct_pwm_set(&pwm_led_scrolllock, scrolllock_brightness, &current_brightness_scrolllock);
}

// Callback for the activity state listener
static int on_activity_state_changed(const zmk_event_t *ev) {
    const struct zmk_activity_state_changed *activity_event = as_zmk_activity_state_changed(ev);
    
    if (!activity_event) {
        return -1;
    }

    if (activity_event->state == ZMK_ACTIVITY_IDLE) {
        on_idle();  // Call the function that handles idle state
    } else if (activity_event->state == ZMK_ACTIVITY_ACTIVE) {
        on_wake_up();  // Call the function that handles wake-up
    }

    return 0;
}

// Listener for idle and wake-up state
ZMK_LISTENER(my_custom_idle_listener, on_activity_state_changed);
ZMK_SUBSCRIPTION(my_custom_idle_listener, zmk_activity_state_changed);

// Listener to handle changes in lock state (Num, Caps, Scroll Lock)
static int led_locks_listener_cb(const zmk_event_t *eh) {
    zmk_hid_indicators_t flags = zmk_hid_indicators_get_current_profile();

    // Define brightness levels for Num Lock, Caps Lock, and Scroll Lock
    uint8_t numlock_brightness = (flags & 0x01) ? 100 : 0;
    uint8_t capslock_brightness = (flags & 0x02) ? 100 : 0;
    uint8_t scrolllock_brightness = (flags & 0x04) ? 100 : 0;

    // Set the brightness for each LED accordingly
    direct_pwm_set(&pwm_led_numlock, numlock_brightness, &current_brightness_numlock);
    direct_pwm_set(&pwm_led_capslock, capslock_brightness, &current_brightness_capslock);
    direct_pwm_set(&pwm_led_scrolllock, scrolllock_brightness, &current_brightness_scrolllock);

    LOG_INF("NUMLOCK is %d", numlock_brightness > 0);
    LOG_INF("CAPSLOCK is %d", capslock_brightness > 0);
    LOG_INF("SCROLLLOCK is %d", scrolllock_brightness > 0);

    return 0;
}

ZMK_LISTENER(led_indicators_listener, led_locks_listener_cb);
ZMK_SUBSCRIPTION(led_indicators_listener, zmk_hid_indicators_changed);

// Initialize the LEDs on boot
static int leds_init(void) {
    if (!pwm_is_ready_dt(&pwm_led_numlock)) {
        LOG_ERR("Error: PWM device %s is not ready\n", pwm_led_numlock.dev->name);
        return -ENODEV;
    }
	
    if (!pwm_is_ready_dt(&pwm_led_capslock)) {
        LOG_ERR("Error: PWM device %s is not ready\n", pwm_led_capslock.dev->name);
        return -ENODEV;
    }

    if (!pwm_is_ready_dt(&pwm_led_scrolllock)) {
        LOG_ERR("Error: PWM device %s is not ready\n", pwm_led_scrolllock.dev->name);
        return -ENODEV;
    }

    return 0;
}

// Run leds_init on boot
SYS_INIT(leds_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
