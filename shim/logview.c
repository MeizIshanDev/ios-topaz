/*
 * Log viewer: the screen is the only output while iOS is on userdata (no
 * recovery has adb then), so show the useful lines of the persistent console
 * log. Layout as in tools/sweet-log.py. Read-only.
 */
#include "console.h"
#include "logview.h"

#define BLOCK		4096u
#define LINE_MAX	240u
#define TAIL_LINES	8u
#define TAIL_COLS	84u
#define MAX_SEEN	96u
#define KEEP_ROWS	4	/* rows left free for the summary */

static char line[LINE_MAX];
static uint32_t line_len;
static char tail[TAIL_LINES][TAIL_COLS];
static uint32_t tail_next;
static uint32_t seen[MAX_SEEN];
static uint32_t n_seen, hits, dropped;

static uint32_t rd32le(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t rd64le(const uint8_t *p)
{
	return (uint64_t)rd32le(p) | (uint64_t)rd32le(p + 4) << 32;
}

static int contains(const char *s, uint32_t len, const char *word)
{
	uint32_t wl = 0;
	while (word[wl])
		wl++;
	for (uint32_t i = 0; i + wl <= len; i++) {
		uint32_t j = 0;
		while (j < wl && s[i + j] == word[j])
			j++;
		if (j == wl)
			return 1;
	}
	return 0;
}

/* The report repeats for photos: show each distinct line once. */
static int seen_before(const char *s, uint32_t len)
{
	uint32_t h = 2166136261u;
	for (uint32_t i = 0; i < len; i++)
		h = (h ^ (uint8_t)s[i]) * 16777619u;
	for (uint32_t i = 0; i < n_seen; i++)
		if (seen[i] == h)
			return 1;
	if (n_seen < MAX_SEEN)
		seen[n_seen++] = h;
	return 0;
}

static void end_line(void)
{
	static const char prefix[] = "SWEET TOUCH: ";
	uint32_t len = line_len, keep = len < TAIL_COLS - 1 ? len : TAIL_COLS - 1;

	line_len = 0;
	if (len == 0)
		return;
	for (uint32_t i = 0; i < keep; i++)
		tail[tail_next][i] = line[i];
	tail[tail_next][keep] = 0;
	tail_next = (tail_next + 1) % TAIL_LINES;

	if (!contains(line, len, "TOUCH") && !contains(line, len, "panic"))
		return;
	hits++;
	if (seen_before(line, len))
		return;
	if (con_rows_left() <= KEEP_ROWS) {
		dropped++;
		return;
	}
	line[len] = 0;
	uint32_t skip = contains(line, sizeof(prefix) - 1 < len ? sizeof(prefix) - 1 : len, prefix)
	    ? sizeof(prefix) - 1 : 0;
	con_text(line + skip, contains(line, len, ">>>") ? GREEN : WHITE);
}

static void feed(uint8_t c)
{
	if (c == '\n' || c == '\r') {
		end_line();
	} else if (line_len < LINE_MAX - 1) {
		line[line_len++] = (char)c;
	}
}

void logview_scan(logview_read_fn rd, uint64_t area_lba, uint8_t *buf)
{
	static const char magic[] = "SWEETLOG";

	if (rd(area_lba, 1, buf)) {
		con_str("LOG", "header read error");
		return;
	}
	for (int i = 0; i < 8; i++) {
		if (buf[i] != (uint8_t)magic[i]) {
			con_str("LOG", "no SWEETLOG header: never flushed");
			return;
		}
	}
	uint32_t version = rd32le(buf + 8), size = rd32le(buf + 12);
	uint64_t total = rd64le(buf + 16), seq = rd64le(buf + 24);
	con_hex2("LOG", "total=", (uint32_t)total, "flush=", (uint32_t)seq);
	if (version != 1 || size == 0 || size % BLOCK || size > (4u << 20)) {
		con_hex2("LOG", "bad ver=", version, "size=", size);
		return;
	}
	con_skip();

	uint64_t pos = total > size ? total - size : 0;
	uint32_t have = 0xffffffffu;
	while (pos < total) {
		uint32_t off = (uint32_t)(pos % size), blk = off / BLOCK, in = off % BLOCK;
		uint64_t left = total - pos;
		uint32_t n = BLOCK - in;

		if (n > left)
			n = (uint32_t)left;
		if (blk != have) {
			if (rd(area_lba + 1 + blk, 1, buf)) {
				con_hex("LOG read err", blk);
				return;
			}
			have = blk;
		}
		for (uint32_t i = 0; i < n; i++)
			feed(buf[in + i]);
		pos += n;
	}
	end_line();

	if (hits == 0) {
		con_str("LOG", "no TOUCH or panic lines; last lines:");
		for (uint32_t i = 0; i < TAIL_LINES; i++) {
			const char *t = tail[(tail_next + i) % TAIL_LINES];
			if (t[0] && con_rows_left() > 2)
				con_text(t, WHITE);
		}
	}
	con_skip();
	con_hex2("LOG", "lines=", hits, "hidden=", dropped);
}
