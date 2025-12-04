/*
 * Author(s): Pawel Hryniszak <phryniszak@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _PH_STRTT_C_H
#define _PH_STRTT_C_H

#include <stdint.h>
#include <stdbool.h>

#include "stlink.h"
#include "stlink_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RAM_START (0x20000000)
#define SANE_SIZE_MAX (256 * 1024) // 256KB

#define SEGGER_RTT_MODE_NO_BLOCK_SKIP (0)      // Skip. Do not block, output nothing. (Default)
#define SEGGER_RTT_MODE_NO_BLOCK_TRIM (1)      // Trim: Do not block, output as much as fits.
#define SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL (2) // Block: Wait until there is space in the buffer.

#define STLINK_TCP_PORT (7184)
#define STLINK_SPEED (24 * 1000)

// Dynamic buffer structure
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} buffer_t;

// Description for a circular buffer (also called "ring buffer")
// which is used as up-buffer (T->H)
typedef struct __attribute__((__packed__)) {
    uint32_t sName;        // Optional name. Standard names so far are: "Terminal", "SysView", "J-Scope_t4i4"
    uint32_t pBuffer;      // Pointer to start of buffer
    uint32_t SizeOfBuffer; // Buffer size in bytes. Note that one byte is lost, as this implementation does not fill up the buffer in order to avoid the problem of being unable to distinguish between full and empty.
    uint32_t WrOff;        // Position of next item to be written by either target.
    uint32_t RdOff;        // Position of next item to be read by host. Must be volatile since it may be modified by host.
    uint32_t Flags;        // Contains configuration flags
} segger_rtt_buffer_t;

// RTT control block which describes the number of buffers available
// as well as the configuration for each buffer
typedef struct __attribute__((__packed__)) {
    char acID[16];                    // Initialized to "SEGGER RTT"
    uint32_t MaxNumUpBuffers;         // Initialized to SEGGER_RTT_MAX_NUM_UP_BUFFERS (type. 2)
    uint32_t MaxNumDownBuffers;       // Initialized to SEGGER_RTT_MAX_NUM_DOWN_BUFFERS (type. 2)
    segger_rtt_buffer_t buffDesc[];   // Up/Down buffers, transferring information up/down from target via debug probe to host
} segger_rtt_cb_t;

typedef struct {
    segger_rtt_cb_t *pRttDescription;
    uint32_t offset;
} segger_rtt_info_t;

// Callback function signature
typedef void (*channel_callback_fn)(int channel_index, const buffer_t *buffer, void *user_data);

// Main RTT structure
typedef struct {
    struct hl_interface_param_s param;
    void *handle;

    // Memory used to find RTT
    buffer_t memory;

    // All information about RTT layout
    segger_rtt_info_t rtt_info;

    // Channel names
    char **rtt_info_names;
    size_t rtt_info_names_count;

    // Timestamp
    double duration;

    // Callback
    channel_callback_fn callback;
    void *callback_user_data;

    // Write shadow memory
    buffer_t wr_memory;

    // RAM start address
    uint32_t ram_start;

    // AP number
    uint8_t ap_num;
} strtt_t;

// Buffer management functions
buffer_t* buffer_create(size_t initial_capacity);
void buffer_free(buffer_t *buffer);
int buffer_resize(buffer_t *buffer, size_t new_size);
int buffer_push_back(buffer_t *buffer, uint8_t value);
void buffer_clear(buffer_t *buffer);

// StRtt functions
strtt_t* strtt_create(uint32_t ram_start, uint8_t ap_num);
void strtt_destroy(strtt_t *strtt);

int strtt_open(strtt_t *strtt, bool use_tcp, uint16_t port_tcp);
int strtt_close(strtt_t *strtt);

int strtt_find_rtt(strtt_t *strtt, uint32_t ram_kbytes);
int strtt_get_rtt_desc(strtt_t *strtt);
int strtt_get_rtt_buff_size(strtt_t *strtt, uint32_t buff_index, uint32_t *size_read, uint32_t *size_write);

int strtt_read_rtt(strtt_t *strtt);
int strtt_read_rtt_from_buff(strtt_t *strtt, int buff_index, buffer_t *buffer);
int strtt_write_rtt(strtt_t *strtt, int buff_index, buffer_t *buffer);

int strtt_get_id_code(strtt_t *strtt, uint32_t *id_code);

void strtt_add_channel_handler(strtt_t *strtt, channel_callback_fn callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif // _PH_STRTT_C_H
