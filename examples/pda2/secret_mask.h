/**
 * @file      secret_mask.h
 * @brief     Display-time masking for secrets shown in config UIs
 *            (PenPal key, AI key, WiFi pass): the MIDDLE of the value is
 *            replaced with '*' so shoulder-surfers cannot read the secret,
 *            while the head/tail stay visible so the user can still
 *            recognise which value is stored.
 *
 *            The masked string is DISPLAY ONLY. Callers keep the real value
 *            in a shadow buffer and must treat an untouched mask as
 *            "unchanged" - never persist the mask itself.
 */
#pragma once

#include <string.h>

/**
 * @brief Copy src to dst with its middle replaced by '*'.
 *        Mask length = max(strlen(src)/3, min_mask), capped at strlen(src);
 *        the remaining head/tail chars stay visible (head first when the
 *        split is uneven).
 * @param src   Secret to mask (may be NULL/empty -> empty dst).
 * @param dst   Output buffer, needs strlen(src)+1 bytes.
 * @param dst_size Size of dst.
 * @param min_mask Minimum number of '*' (0 = plain middle-third).
 */
static inline void secret_mask_middle(const char *src, char *dst, int dst_size,
                                      int min_mask)
{
    int n = 0, m, head, tail, i;
    if (!dst || dst_size <= 0) return;
    if (src) n = (int)strlen(src);
    if (n == 0) { dst[0] = '\0'; return; }

    m = n / 3;
    if (m < min_mask) m = min_mask;
    if (m > n) m = n;
    head = (n - m) / 2;
    tail = n - m - head;

    i = 0;
    for (int j = 0; j < head && i < dst_size - 1; j++, i++) dst[i] = src[j];
    for (int j = 0; j < m && i < dst_size - 1; j++, i++)    dst[i] = '*';
    for (int j = 0; j < tail && i < dst_size - 1; j++, i++) dst[i] = src[head + m + j];
    dst[i] = '\0';
}
