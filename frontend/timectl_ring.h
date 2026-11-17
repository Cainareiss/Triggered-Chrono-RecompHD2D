/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the rewind ring (timectl.h): a run of save state blobs, newest
 * first, kept as the newest in full and each older one as a delta against
 * the one after it (XOR, then zero runs squeezed out), so a second of
 * play costs a few kilobytes, not a state's 600 KB each. Pure: no engine,
 * no SDL (test/test_timectl.c). A push costs one pass over the blob; the
 * newest is always at hand, and dropping it rebuilds the one before in
 * place. Capped by entries and by bytes (the newest in full counts). */
#ifndef CT_TIMECTL_RING_H
#define CT_TIMECTL_RING_H

#include <stddef.h>
#include <stdint.h>

typedef struct tcr tcr;

/* max_entries: states kept (>= 1); max_bytes: memory cap (0: none). */
tcr *tcr_new(int max_entries, size_t max_bytes);
void tcr_free(tcr *r);
void tcr_clear(tcr *r);
/* New caps (the setting changed): the oldest go at once if over. */
void tcr_limits(tcr *r, int max_entries, size_t max_bytes);

/* Add the newest state (copied). 0, or -1 (no memory). The oldest
   entries drop to stay within the caps (the newest always stays). */
int tcr_push(tcr *r, const uint8_t *blob, size_t len, long frame);
/* States held (0 when empty). */
int tcr_count(const tcr *r);
/* Bytes in use (the newest in full plus every delta). */
size_t tcr_bytes(const tcr *r);
/* The newest state: valid until the next push, drop or clear. NULL when
   empty. */
const uint8_t *tcr_newest(const tcr *r, size_t *len, long *frame);
/* Forget the newest: the one before it becomes the newest (rebuilt in
   place). 0, or -1 when there is no other to take its place (the ring is
   left as it was). */
int tcr_drop_newest(tcr *r);

/* The delta itself, for tests: encode what turns newer into older, and
   apply it. enc writes into out (cap bytes) and returns its length, or 0
   when it does not fit; dec rebuilds into buf (which holds newer's bytes,
   room for max(newer_len, older_len)), returns older_len, or 0 if the
   delta is damaged. */
size_t tcr_delta_enc(const uint8_t *newer, size_t newer_len, const uint8_t *older,
                     size_t older_len, uint8_t *out, size_t cap);
size_t tcr_delta_dec(uint8_t *buf, size_t newer_len, size_t older_len, const uint8_t *delta,
                     size_t delta_len);
/* Upper bound of tcr_delta_enc's output for the sizes. */
size_t tcr_delta_max(size_t newer_len, size_t older_len);

#endif
