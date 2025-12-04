/*
 * Author(s): Pawel Hryniszak <phryniszak@gmail.com>
 * C port of strttapp.cpp
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#elif _WIN32
#include <conio.h>
#include <windows.h>
#else
#endif

#include "strtt.h"
#include "log.h"
#include "inputparser.h"

// CONSTANTS //////////////////////////////////////////////

#define DEFAULT_RAM_SIZE_KB 16
#define RAM_START_DEFAULT RAM_START
#define DEFAULT_AP_NUM 0
#define RTT_RETRY_DELAY_MS 500
#define RTT_TERMINAL_CHANNEL 0

#ifdef __linux__
#define PROCESS_PRIORITY -11
#endif

// GLOBAL VARIABLES ///////////////////////////////////////

static volatile sig_atomic_t stop_app = 0;

// SIGNAL HANDLER /////////////////////////////////////////

void signal_handler(int signum) {
    LOG_INFO("Interrupt signal %d received", signum);
    stop_app = 1;
}

// HELPER FUNCTIONS ///////////////////////////////////////

#ifdef __linux__
// Linux keyboard input functions
static struct termios old_term, new_term;
static bool term_initialized = false;

static void init_terminal(void) {
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
    term_initialized = true;
}

static void restore_terminal(void) {
    if (term_initialized) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    }
}

static int kbhit(void) {
    struct timeval tv = {0, 0};
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    return select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0;
}

static int getch(void) {
    return getchar();
}
#elif _WIN32
// Windows keyboard input functions
// kbhit() and getch() are available in conio.h
#else
static int kbhit(void) {
    return 0;
}

static int getch(void) {
    return getchar();
}
#endif

static void sleep_ms(int milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

static int parse_integer_option(const char *option_value) {
    if (!option_value) return 0;

    if (strlen(option_value) > 1 && option_value[0] == '0' &&
        (option_value[1] == 'x' || option_value[1] == 'X')) {
        return (int)strtol(option_value, NULL, 16);
    }
    return atoi(option_value);
}

static bool find_rtt_with_retry(strtt_t *strtt, int ram_size_kb, bool loop_mode) {
    bool rtt_found = false;

    do {
        int result = strtt_find_rtt(strtt, ram_size_kb);
        if (result == ERROR_OK) {
            rtt_found = true;
            LOG_INFO("RTT found successfully");
        } else {
            if (loop_mode) {
                LOG_INFO("RTT not found, retrying...");
                sleep_ms(RTT_RETRY_DELAY_MS);
            } else {
                LOG_ERROR("failed to find RTT (%d)", result);
                exit(-1);
            }
        }
    } while (!rtt_found && loop_mode && !stop_app);

    if (!rtt_found) {
        LOG_ERROR("RTT search cancelled");
        exit(-1);
    }

    return rtt_found;
}

static void channel_handler(int channel_index, const buffer_t *buffer, void *user_data) {
    (void)user_data; // Unused

    if (channel_index == RTT_TERMINAL_CHANNEL) {
        // TERMINAL, print to console
        for (size_t i = 0; i < buffer->size; i++) {
            fputc(buffer->data[i], stdout);
        }
        fflush(stdout);
    }
}

static void handle_rtt_read_error(strtt_t *strtt, int ram_size_kb, bool loop_mode, int error_code) {
    if (!loop_mode) {
        LOG_ERROR("readRtt returned error %d, program is exiting", error_code);
        stop_app = 1;
        return;
    }

    LOG_INFO("readRtt failed (%d), attempting to re-find RTT...", error_code);

    bool rtt_found = false;
    do {
        int result = strtt_find_rtt(strtt, ram_size_kb);
        if (result == ERROR_OK) {
            rtt_found = true;
            LOG_INFO("RTT re-found successfully");
            strtt_get_rtt_desc(strtt);
        } else {
            LOG_INFO("RTT not found, retrying...");
            sleep_ms(RTT_RETRY_DELAY_MS);
        }
    } while (!rtt_found && loop_mode && !stop_app);

    if (!rtt_found) {
        LOG_ERROR("Failed to re-find RTT, exiting");
        stop_app = 1;
    }
}

static void process_keyboard_input(buffer_t *buffer) {
    while (kbhit()) {
        uint8_t ch = (uint8_t)getch();
        buffer_push_back(buffer, ch);
    }
}

// MAIN APPLICATION ///////////////////////////////////////

int main(int argc, char **argv) {
#ifdef __linux__
    // Opportunistic call to renice us, so we can keep up under
    // higher load conditions. This may fail when run as non-root.
    setpriority(PRIO_PROCESS, 0, PROCESS_PRIORITY);
    init_terminal();
    atexit(restore_terminal);
#endif

    input_parser_t *input = input_parser_create(argc, argv);
    if (!input) {
        fprintf(stderr, "Failed to create input parser\n");
        return -1;
    }

    signal(SIGINT, signal_handler);

    // Initialize logging
    log_init();
    debug_level = LOG_LVL_ERROR;
    if (input_parser_cmd_option_exists(input, "-v")) {
        debug_level = atoi(input_parser_get_cmd_option(input, "-v"));
    }

    // Parse command line options
    int ram_size_kb = DEFAULT_RAM_SIZE_KB;
    if (input_parser_cmd_option_exists(input, "-ramsize")) {
        ram_size_kb = parse_integer_option(input_parser_get_cmd_option(input, "-ramsize"));
    }

    uint32_t ram_start = RAM_START_DEFAULT;
    if (input_parser_cmd_option_exists(input, "-ramstart")) {
        ram_start = parse_integer_option(input_parser_get_cmd_option(input, "-ramstart"));
    }

    bool show_cycle_time = input_parser_cmd_option_exists(input, "-t");
    bool use_tcp = input_parser_cmd_option_exists(input, "-tcp");
    bool loop_mode = input_parser_cmd_option_exists(input, "-loop");

    uint8_t ap_num = DEFAULT_AP_NUM;
    if (input_parser_cmd_option_exists(input, "-ap")) {
        ap_num = (uint8_t)atoi(input_parser_get_cmd_option(input, "-ap"));
    }

    // Initialize RTT
    strtt_t *strtt = strtt_create(ram_start, ap_num);
    if (!strtt) {
        LOG_ERROR("Failed to create RTT instance");
        input_parser_destroy(input);
        return -1;
    }

    // Open ST-Link connection
    int result = strtt_open(strtt, use_tcp, STLINK_TCP_PORT);
    if (result != ERROR_OK) {
        LOG_ERROR("failed to open STLINK (%d)", result);
        strtt_destroy(strtt);
        input_parser_destroy(input);
        return -1;
    }

    // Find RTT control block
    find_rtt_with_retry(strtt, ram_size_kb, loop_mode);
    strtt_get_rtt_desc(strtt);

    // Get buffer sizes
    uint32_t size_read, size_write;
    result = strtt_get_rtt_buff_size(strtt, RTT_TERMINAL_CHANNEL, &size_read, &size_write);

    // Set up channel handler
    strtt_add_channel_handler(strtt, channel_handler, NULL);

    // Main processing loop
    buffer_t *input_buffer = buffer_create(256);
    if (!input_buffer) {
        LOG_ERROR("Failed to create input buffer");
        strtt_close(strtt);
        strtt_destroy(strtt);
        input_parser_destroy(input);
        return -1;
    }

    while (!stop_app) {
        clock_t start_time = clock();

        // Read RTT data from target
        result = strtt_read_rtt(strtt);
        if (result != ERROR_OK) {
            handle_rtt_read_error(strtt, ram_size_kb, loop_mode, result);
            continue;
        }

        // Process keyboard input
        process_keyboard_input(input_buffer);
        if (input_buffer->size > 0) {
            strtt_write_rtt(strtt, RTT_TERMINAL_CHANNEL, input_buffer);
        }

        if (show_cycle_time) {
            double cycle_duration = (double)(clock() - start_time) / CLOCKS_PER_SEC * 1000.0;
            LOG_USER("Cycle time: %dms", (int)cycle_duration);
        }
    }

    // Cleanup
    buffer_free(input_buffer);
    strtt_close(strtt);
    strtt_destroy(strtt);
    input_parser_destroy(input);

    return 0;
}
