
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "pins.h"

#define COMMAND_BUFFER_SIZE 96

typedef enum {
    STATE_BOOT = 0,
    STATE_GROUND_LINK,
    STATE_TEST_MODE,
    STATE_FLIGHT_READY,
    STATE_AWAITING_SOE,
    STATE_EXPERIMENT_ACTIVE,
    STATE_SAFE_SHUTDOWN,
    STATE_ERROR
} mission_state_t;

static mission_state_t current_state = STATE_BOOT;

static char command_buffer[COMMAND_BUFFER_SIZE];
static size_t command_index = 0;

static const char *state_to_string(mission_state_t state) {
    switch (state) {
        case STATE_BOOT:return "BOOT";
        case STATE_GROUND_LINK:return "GROUND_LINK";
        case STATE_TEST_MODE:return "TEST_MODE";
        case STATE_FLIGHT_READY:return "FLIGHT_READY";
        case STATE_AWAITING_SOE:return "AWAITING_SOE";
        case STATE_EXPERIMENT_ACTIVE:return "EXPERIMENT_ACTIVE";
        case STATE_SAFE_SHUTDOWN:return "SAFE_SHUTDOWN";
        case STATE_ERROR:return "ERROR";
        default:return "UNKNOWN";
    }
}

static void init_inputs(void) {
    gpio_init(PIN_SOE);
    gpio_set_dir(PIN_SOE, GPIO_IN);
    gpio_pull_down(PIN_SOE);

    gpio_init(PIN_LO);
    gpio_set_dir(PIN_LO, GPIO_IN);
    gpio_pull_down(PIN_LO);
}

static void init_outputs(void) {
    const uint output_pins[] = {
        PIN_CAM_EN,
        PIN_LED_EN1,
        PIN_LED_EN2,
        PIN_LED_EN3,
        PIN_LED_EN4,
        PIN_VIDEO_A0,
        PIN_VIDEO_A1,
        PIN_VIDEO_A2,
        PIN_SRCLK,
        PIN_RCLK,
        PIN_OE,
        PIN_SER
    };

    for (size_t i = 0; i < sizeof(output_pins) / sizeof(output_pins[0]); i++) {
        gpio_init(output_pins[i]);
        gpio_set_dir(output_pins[i], GPIO_OUT);
        gpio_put(output_pins[i], 0);
    }

    gpio_put(PIN_OE, 1);

    gpio_put(PIN_CAM_EN, 0);
    gpio_put(PIN_LED_EN1, 0);
    gpio_put(PIN_LED_EN2, 0);
    gpio_put(PIN_LED_EN3, 0);
    gpio_put(PIN_LED_EN4, 0);
}

static void set_all_leds(bool on) {
    gpio_put(PIN_LED_EN1, on);
    gpio_put(PIN_LED_EN2, on);
    gpio_put(PIN_LED_EN3, on);
    gpio_put(PIN_LED_EN4, on);
}

static void cameras_set_power(bool on) {
    gpio_put(PIN_CAM_EN, on);
}

static void send_status(void) {
    bool soe = gpio_get(PIN_SOE);
    bool lo = gpio_get(PIN_LO);

    printf(
        "STATUS STATE=%s SOE=%d LO=%d CAM_EN=%d LED1=%d LED2=%d LED3=%d LED4=%d\n",
        state_to_string(current_state),
        soe,
        lo,
        gpio_get(PIN_CAM_EN),
        gpio_get(PIN_LED_EN1),
        gpio_get(PIN_LED_EN2),
        gpio_get(PIN_LED_EN3),
        gpio_get(PIN_LED_EN4)
    );
}

static void send_help(void) {
    printf("COMMANDS: GET_STATUS, ENTER_TEST_MODE, EXIT_TEST_MODE, FLIGHT_READY, AWAIT_SOE, LEDS_ON, LEDS_OFF, CAM_ON, CAM_OFF, SAFE_SHUTDOWN, HELP\n");
}

static void handle_command(const char *cmd) {
    if (strcmp(cmd, "GET_STATUS") == 0) {
        send_status();

    } else if (strcmp(cmd, "ENTER_TEST_MODE") == 0) {
        if (current_state == STATE_GROUND_LINK || current_state == STATE_FLIGHT_READY) {
            current_state = STATE_TEST_MODE;
            printf("ACK ENTER_TEST_MODE\n");
        } else {
            printf("ERR ENTER_TEST_MODE NOT_ALLOWED STATE=%s\n", state_to_string(current_state));
        }

    } else if (strcmp(cmd, "EXIT_TEST_MODE") == 0) {
        if (current_state == STATE_TEST_MODE) {
            current_state = STATE_GROUND_LINK;
            set_all_leds(false);
            cameras_set_power(false);
            printf("ACK EXIT_TEST_MODE\n");
        } else {
            printf("ERR EXIT_TEST_MODE NOT_IN_TEST_MODE\n");
        }

    } else if (strcmp(cmd, "FLIGHT_READY") == 0) {
        if (current_state == STATE_GROUND_LINK || current_state == STATE_TEST_MODE) {
            set_all_leds(false);
            cameras_set_power(false);
            current_state = STATE_FLIGHT_READY;
            printf("ACK FLIGHT_READY\n");
        } else {
            printf("ERR FLIGHT_READY NOT_ALLOWED STATE=%s\n", state_to_string(current_state));
        }

    } else if (strcmp(cmd, "AWAIT_SOE") == 0) {
        if (current_state == STATE_FLIGHT_READY) {
            current_state = STATE_AWAITING_SOE;
            printf("ACK AWAIT_SOE\n");
        } else {
            printf("ERR AWAIT_SOE NOT_ALLOWED STATE=%s\n", state_to_string(current_state));
        }

    } else if (strcmp(cmd, "LEDS_ON") == 0) {
        if (current_state == STATE_TEST_MODE) {
            set_all_leds(true);
            printf("ACK LEDS_ON\n");
        } else {
            printf("ERR LEDS_ON ONLY_ALLOWED_IN_TEST_MODE\n");
        }

    } else if (strcmp(cmd, "LEDS_OFF") == 0) {
        set_all_leds(false);
        printf("ACK LEDS_OFF\n");

    } else if (strcmp(cmd, "CAM_ON") == 0) {
        if (current_state == STATE_TEST_MODE) {
            cameras_set_power(true);
            printf("ACK CAM_ON\n");
        } else {
            printf("ERR CAM_ON ONLY_ALLOWED_IN_TEST_MODE\n");
        }

    } else if (strcmp(cmd, "CAM_OFF") == 0) {
        cameras_set_power(false);
        printf("ACK CAM_OFF\n");

    } else if (strcmp(cmd, "SAFE_SHUTDOWN") == 0) {
        set_all_leds(false);
        cameras_set_power(false);
        current_state = STATE_SAFE_SHUTDOWN;
        printf("ACK SAFE_SHUTDOWN\n");

    } else if (strcmp(cmd, "HELP") == 0) {
        send_help();

    } else if (strlen(cmd) == 0) {

    } else {
        printf("ERR UNKNOWN_COMMAND CMD=%s\n", cmd);
    }
}

static void poll_serial_commands(void) {
    int ch;

    while ((ch = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            command_buffer[command_index] = '\0';
            handle_command(command_buffer);
            command_index = 0;
            command_buffer[0] = '\0';
            continue;
        }

        if (command_index < COMMAND_BUFFER_SIZE - 1) {
            command_buffer[command_index++] = (char)ch;
        } else {
            command_index = 0;
            command_buffer[0] = '\0';
            printf("ERR COMMAND_TOO_LONG\n");
        }
    }
}

static void update_state_machine(void) {
    bool soe = gpio_get(PIN_SOE);

    if (current_state == STATE_AWAITING_SOE && soe) {
        current_state = STATE_EXPERIMENT_ACTIVE;

        cameras_set_power(true);
        set_all_leds(true);

        printf("EVENT SOE_DETECTED\n");
        printf("ACK EXPERIMENT_ACTIVE\n");
    }
}

int main(void) {
    stdio_init_all();

    sleep_ms(2000);

    init_inputs();
    init_outputs();

    watchdog_enable(3000, 1);

    current_state = STATE_GROUND_LINK;

    printf("CAPILUX flight computer booted\n");
    printf("Firmware milestone: command_parser_001\n");
    send_help();

    absolute_time_t last_status_time = get_absolute_time();

    while (true) {
        watchdog_update();

        poll_serial_commands();
        update_state_machine();

        if (absolute_time_diff_us(last_status_time, get_absolute_time()) > 1000000) {
            send_status();
            last_status_time = get_absolute_time();
        }

        sleep_ms(10);
    }
}
