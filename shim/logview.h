#pragma once
#include <stdint.h>

/* Reads count 4 KiB blocks at lba into buf; returns 0 on success. */
typedef uint32_t (*logview_read_fn)(uint64_t lba, uint32_t count, void *buf);

/*
 * Show the persistent console log (xnu patch 0047) stored at area_lba: one
 * header block, then the ring. buf must hold one block.
 */
void logview_scan(logview_read_fn rd, uint64_t area_lba, uint8_t *buf);
