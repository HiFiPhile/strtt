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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "strtt.h"
#include "log.h"

#define MAX_STR_LENGTH 64

// ============================================================================
// Buffer Management Functions
// ============================================================================

buffer_t* buffer_create(size_t initial_capacity) {
    buffer_t *buffer = (buffer_t*)malloc(sizeof(buffer_t));
    if (!buffer) return NULL;

    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;

    if (initial_capacity > 0) {
        buffer->data = (uint8_t*)malloc(initial_capacity);
        if (!buffer->data) {
            free(buffer);
            return NULL;
        }
        buffer->capacity = initial_capacity;
    }

    return buffer;
}

void buffer_free(buffer_t *buffer) {
    if (buffer) {
        if (buffer->data) {
            free(buffer->data);
        }
        free(buffer);
    }
}

int buffer_resize(buffer_t *buffer, size_t new_size) {
    if (!buffer) return -1;

    if (new_size > buffer->capacity) {
        size_t new_capacity = new_size;
        uint8_t *new_data = (uint8_t*)realloc(buffer->data, new_capacity);
        if (!new_data) return -1;

        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }

    buffer->size = new_size;
    return 0;
}

int buffer_push_back(buffer_t *buffer, uint8_t value) {
    if (!buffer) return -1;

    if (buffer->size >= buffer->capacity) {
        size_t new_capacity = buffer->capacity == 0 ? 256 : buffer->capacity * 2;
        uint8_t *new_data = (uint8_t*)realloc(buffer->data, new_capacity);
        if (!new_data) return -1;

        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }

    buffer->data[buffer->size++] = value;
    return 0;
}

void buffer_clear(buffer_t *buffer) {
    if (buffer) {
        buffer->size = 0;
    }
}

// ============================================================================
// Internal Helper Functions
// ============================================================================

static void strtt_init(strtt_t *strtt) {
    log_init();

    memset(&strtt->param, 0, sizeof(strtt->param));
    strtt->param.device_desc = "ST-LINK";
    strtt->param.transport = HL_TRANSPORT_SWD;

    uint16_t pids[] = {STLINK_V2_PID, STLINK_V2_1_PID, STLINK_V2_1_NO_MSD_PID,
                       STLINK_V3_USBLOADER_PID, STLINK_V3E_PID, STLINK_V3S_PID,
                       STLINK_V3_2VCP_PID, STLINK_V3E_NO_MSD_PID};

    for (size_t i = 0; i < HLA_MAX_USB_IDS; ++i) {
        if (i < sizeof(pids) / sizeof(pids[0])) {
            strtt->param.vid[i] = STLINK_VID;
            strtt->param.pid[i] = pids[i];
        } else {
            strtt->param.vid[i] = 0;
            strtt->param.pid[i] = 0;
        }
    }

    strtt->param.use_stlink_tcp = false;
    strtt->param.initial_interface_speed = STLINK_SPEED;
    strtt->param.connect_under_reset = false;
    strtt->param.ap_num = strtt->ap_num;
}

static unsigned strtt_get_avail_write_space(segger_rtt_buffer_t *pRing) {
    unsigned RdOff = pRing->RdOff;
    unsigned WrOff = pRing->WrOff;
    unsigned r;

    if (RdOff <= WrOff) {
        r = pRing->SizeOfBuffer - 1u - WrOff + RdOff;
    } else {
        r = RdOff - WrOff - 1u;
    }
    return r;
}

// ============================================================================
// Public API Functions
// ============================================================================

strtt_t* strtt_create(uint32_t ram_start, uint8_t ap_num) {
    strtt_t *strtt = (strtt_t*)malloc(sizeof(strtt_t));
    if (!strtt) return NULL;

    memset(strtt, 0, sizeof(strtt_t));
    strtt->ram_start = ram_start;
    strtt->ap_num = ap_num;

    strtt->memory.data = NULL;
    strtt->memory.size = 0;
    strtt->memory.capacity = 0;

    strtt->wr_memory.data = NULL;
    strtt->wr_memory.size = 0;
    strtt->wr_memory.capacity = 0;

    strtt->rtt_info_names = NULL;
    strtt->rtt_info_names_count = 0;

    strtt_init(strtt);

    return strtt;
}

void strtt_destroy(strtt_t *strtt) {
    if (!strtt) return;

    if (strtt->handle) {
        stlink_usb_layout_api.close(strtt->handle);
    }

    if (strtt->memory.data) {
        free(strtt->memory.data);
    }

    if (strtt->wr_memory.data) {
        free(strtt->wr_memory.data);
    }

    if (strtt->rtt_info_names) {
        for (size_t i = 0; i < strtt->rtt_info_names_count; i++) {
            if (strtt->rtt_info_names[i]) {
                free(strtt->rtt_info_names[i]);
            }
        }
        free(strtt->rtt_info_names);
    }

    free(strtt);
}

int strtt_open(strtt_t *strtt, bool use_tcp, uint16_t port_tcp) {
    if (!strtt) return -1;

    strtt->param.use_stlink_tcp = use_tcp;
    strtt->param.stlink_tcp_port = port_tcp;
    return stlink_usb_layout_api.open(&strtt->param, &strtt->handle);
}

int strtt_close(strtt_t *strtt) {
    if (!strtt) return -1;
    return stlink_usb_layout_api.close(strtt->handle);
}

int strtt_get_id_code(strtt_t *strtt, uint32_t *id_code) {
    if (!strtt || !id_code) return -1;

    clock_t start = clock();
    int ret = stlink_usb_layout_api.idcode(strtt->handle, id_code);
    strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;

    return ret;
}

int strtt_find_rtt(strtt_t *strtt, uint32_t ram_kbytes) {
    if (!strtt) return -1;

    clock_t start = clock();

    // Read the whole RAM
    if (buffer_resize(&strtt->memory, ram_kbytes * 1024) != 0) {
        strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
        return -1;
    }

    int ret = stlink_usb_layout_api.read_mem(strtt->handle, strtt->ram_start, -1,
                                             ram_kbytes * 0x400, strtt->memory.data);
    if (ret != ERROR_OK) {
        strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
        return ret;
    }

    // Find SEGGER_RTT_CB address
    const char *str_segger_rtt = "SEGGER RTT";

    for (uint32_t offset = 0; offset < (ram_kbytes * 1024) - 16; offset++) {
        if (strncmp((char *)&strtt->memory.data[offset], str_segger_rtt, 16) == 0) {
            LOG_DEBUG("RTT addr = 0x%x", strtt->ram_start + offset);

            strtt->rtt_info.pRttDescription = (segger_rtt_cb_t *)&strtt->memory.data[offset];
            strtt->rtt_info.offset = offset;
            break;
        }
    }

    // Check results
    if (strtt->rtt_info.offset == 0) {
        LOG_ERROR("RTT not found");
        strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
        return -1;
    }

    if ((strtt->rtt_info.pRttDescription->MaxNumUpBuffers == 0) ||
        (strtt->rtt_info.pRttDescription->MaxNumDownBuffers == 0)) {
        LOG_ERROR("Max number of UP buffers: %d and DOWN buffers: %d",
                  strtt->rtt_info.pRttDescription->MaxNumUpBuffers,
                  strtt->rtt_info.pRttDescription->MaxNumDownBuffers);
        strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
        return -1;
    }

    LOG_DEBUG("Max number of buffers UP: %d and DOWN: %d",
              strtt->rtt_info.pRttDescription->MaxNumUpBuffers,
              strtt->rtt_info.pRttDescription->MaxNumDownBuffers);

    strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
    return ERROR_OK;
}

int strtt_get_rtt_desc(strtt_t *strtt) {
    if (!strtt) return -1;

    clock_t start = clock();

    unsigned int size = strtt->rtt_info.pRttDescription->MaxNumUpBuffers +
                       strtt->rtt_info.pRttDescription->MaxNumDownBuffers;

    // Allocate channel names array
    strtt->rtt_info_names = (char**)calloc(size, sizeof(char*));
    if (!strtt->rtt_info_names) {
        strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
        return -1;
    }
    strtt->rtt_info_names_count = size;

    for (size_t i = 0; i < size; i++) {
        char str_channel_name[MAX_STR_LENGTH];
        if (strtt->rtt_info.pRttDescription->buffDesc[i].sName) {
            // Read it from flash
            stlink_usb_layout_api.read_mem(strtt->handle,
                                          strtt->rtt_info.pRttDescription->buffDesc[i].sName,
                                          -1, MAX_STR_LENGTH, (uint8_t *)str_channel_name);

            strtt->rtt_info_names[i] = strdup(str_channel_name);

            LOG_INFO("%d. Channel name: %s\tsize: %d\tmode: %d", (int)i, str_channel_name,
                     strtt->rtt_info.pRttDescription->buffDesc[i].SizeOfBuffer,
                     strtt->rtt_info.pRttDescription->buffDesc[i].Flags);
        } else {
            LOG_INFO("%d. Channel name: -------\tsize: %d\tmode: %d", (int)i,
                     strtt->rtt_info.pRttDescription->buffDesc[i].SizeOfBuffer,
                     strtt->rtt_info.pRttDescription->buffDesc[i].Flags);
        }
    }

    strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
    return ERROR_OK;
}

int strtt_get_rtt_buff_size(strtt_t *strtt, uint32_t buff_index,
                           uint32_t *size_read, uint32_t *size_write) {
    if (!strtt || !size_read || !size_write) return -1;

    *size_read = strtt->rtt_info.pRttDescription->buffDesc[buff_index].SizeOfBuffer;
    *size_write = strtt->rtt_info.pRttDescription->buffDesc[
        buff_index + strtt->rtt_info.pRttDescription->MaxNumUpBuffers].SizeOfBuffer;

    return ERROR_OK;
}

int strtt_read_rtt(strtt_t *strtt) {
    if (!strtt) return -1;

    clock_t start = clock();

    // 1. Read RTT descriptor
    uint32_t rtt_start = strtt->rtt_info.offset;
    unsigned int buffers_cnt = strtt->rtt_info.pRttDescription->MaxNumUpBuffers +
                               strtt->rtt_info.pRttDescription->MaxNumDownBuffers;
    uint32_t size = sizeof(segger_rtt_cb_t) + sizeof(segger_rtt_buffer_t) * buffers_cnt;

    int ret = stlink_usb_layout_api.read_mem(strtt->handle, rtt_start + strtt->ram_start,
                                             -1, size, &strtt->memory.data[rtt_start]);
    if (ret < 0) {
        strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
        return ret;
    }

    // 2. Find memory range to read - simplified approach
    uint32_t min_start = 0xFFFFFFFF;
    uint32_t max_end = 0;
    bool has_data = false;

    for (size_t i = 0; i < buffers_cnt; i++) {
        segger_rtt_buffer_t buffer_desc = strtt->rtt_info.pRttDescription->buffDesc[i];
        if ((buffer_desc.SizeOfBuffer) && (buffer_desc.RdOff != buffer_desc.WrOff)) {
            uint32_t buff_start = buffer_desc.pBuffer - strtt->ram_start;
            uint32_t buff_end = buff_start + buffer_desc.SizeOfBuffer;

            if (buff_start < min_start) min_start = buff_start;
            if (buff_end > max_end) max_end = buff_end;
            has_data = true;
        }
    }

    // 3. If nothing to be read, return
    if (!has_data) {
        strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
        return ERROR_OK;
    }

    // 4. Read memory range
    size = max_end - min_start;

    if (size > SANE_SIZE_MAX) {
        LOG_ERROR("Read rtt memory size is insane: %d", size);
        strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
        return ERROR_OK;
    }

    ret = stlink_usb_layout_api.read_mem(strtt->handle, min_start + strtt->ram_start,
                                        -1, ((size / 4) * 4) + 4, &strtt->memory.data[min_start]);
    if (ret < 0) {
        strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
        return ret;
    }

    // 5. Read RTT channels
    buffers_cnt = strtt->rtt_info.pRttDescription->MaxNumUpBuffers;
    for (size_t i = 0; i < buffers_cnt; i++) {
        segger_rtt_buffer_t buffer_desc = strtt->rtt_info.pRttDescription->buffDesc[i];
        if ((buffer_desc.SizeOfBuffer) && (buffer_desc.RdOff != buffer_desc.WrOff)) {
            buffer_t *buffer = buffer_create(buffer_desc.SizeOfBuffer);
            if (!buffer) continue;

            int amount = strtt_read_rtt_from_buff(strtt, i, buffer);
            if (amount > 0) {
                LOG_DEBUG("Channel: %d read: %d", (int)i, amount);
                if (strtt->callback) {
                    strtt->callback((int)i, buffer, strtt->callback_user_data);
                }
            }
            buffer_free(buffer);
        }
    }

    strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
    return ERROR_OK;
}

int strtt_read_rtt_from_buff(strtt_t *strtt, int buff_index, buffer_t *buffer) {
    if (!strtt || !buffer) return -1;

    segger_rtt_buffer_t *pRing = &strtt->rtt_info.pRttDescription->buffDesc[buff_index];
    unsigned int WrOff = pRing->WrOff;
    unsigned int RdOff = pRing->RdOff;

    size_t memory_index = pRing->pBuffer - strtt->ram_start;

    // Read from RdOff until we reach WrOff
    while (RdOff != WrOff) {
        buffer_push_back(buffer, strtt->memory.data[memory_index + RdOff]);
        RdOff++;

        // Handle wrap-around
        if (RdOff >= pRing->SizeOfBuffer) {
            RdOff = 0;
        }
    }

    if (buffer->size > 0) {
        // Update RdOff
        uint32_t addr_rd_off = (uint8_t *)&pRing->RdOff - strtt->memory.data + strtt->ram_start;

        int ret = stlink_usb_layout_api.write_mem(strtt->handle, addr_rd_off, -1, 4,
                                                  (uint8_t *)&WrOff);
        if (ret < 0) {
            return ret;
        }
    }

    return buffer->size;
}

int strtt_write_rtt(strtt_t *strtt, int buff_index, buffer_t *buffer) {
    if (!strtt || !buffer) return -1;

    clock_t start = clock();

    segger_rtt_buffer_t *pRing = &strtt->rtt_info.pRttDescription->buffDesc[
        buff_index + strtt->rtt_info.pRttDescription->MaxNumUpBuffers];
    unsigned int WrOff = pRing->WrOff;

    // How much we can write in non-blocking mode
    unsigned available = strtt_get_avail_write_space(pRing);
    unsigned num_written = (available < buffer->size) ? available : buffer->size;

    if (num_written == 0) {
        return 0;
    }

    // Read memory from target to shadow memory (once)
    if (strtt->wr_memory.size == 0) {
        if (buffer_resize(&strtt->wr_memory, pRing->SizeOfBuffer) != 0) {
            strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
            return -1;
        }

        int ret = stlink_usb_layout_api.read_mem(strtt->handle, pRing->pBuffer, -1,
                                                pRing->SizeOfBuffer, strtt->wr_memory.data);
        if (ret != ERROR_OK) {
            strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
            return ret;
        }
    }

    // Write buffer to shadow memory
    for (size_t i = 0; i < num_written; i++) {
        strtt->wr_memory.data[WrOff++] = buffer->data[i];

        // Handle wrap-around
        if (WrOff >= pRing->SizeOfBuffer) {
            WrOff = 0;
        }
    }

    // Remove written bytes from buffer
    if (num_written < buffer->size) {
        memmove(buffer->data, buffer->data + num_written, buffer->size - num_written);
    }
    buffer->size -= num_written;

    // Write shadow memory to target
    int ret = stlink_usb_layout_api.write_mem(strtt->handle, pRing->pBuffer, -1,
                                             pRing->SizeOfBuffer, strtt->wr_memory.data);
    if (ret != ERROR_OK) {
        strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
        return ret;
    }

    // Update WrOff
    uint32_t addr_wr_off = (uint8_t *)&pRing->WrOff - strtt->memory.data + strtt->ram_start;
    ret = stlink_usb_layout_api.write_mem(strtt->handle, addr_wr_off, -1, 4, (uint8_t *)&WrOff);
    if (ret < 0) {
        strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
        return ret;
    }

    strtt->duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000000.0;
    return num_written;
}

void strtt_add_channel_handler(strtt_t *strtt, channel_callback_fn callback, void *user_data) {
    if (!strtt) return;

    strtt->callback = callback;
    strtt->callback_user_data = user_data;
}
