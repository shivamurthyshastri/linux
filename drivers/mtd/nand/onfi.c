// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt)     "nand-onfi: " fmt

#include <linux/mtd/onfi.h>
#include <linux/mtd/nand.h>

/**
 * onfi_crc16() - Check CRC of ONFI table
 * @crc: base CRC
 * @p: buffer pointing to ONFI table
 * @len: length of ONFI table
 *
 * Return: CRC of the ONFI table
 */
u16 onfi_crc16(u16 crc, u8 const *p, size_t len)
{
	int i;

	while (len--) {
		crc ^= *p++ << 8;
		for (i = 0; i < 8; i++)
			crc = (crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0);
	}

	return crc;
}
EXPORT_SYMBOL_GPL(onfi_crc16);

/**
 * nand_bit_wise_majority() - Recover data with bit-wise majority
 * @srcbufs: buffer pointing to ONFI table
 * @nsrcbufs: length of ONFI table
 * @dstbuf: valid ONFI table to be returned
 * @bufsize: length og valid ONFI table
 *
 */
void nand_bit_wise_majority(const void **srcbufs,
			    unsigned int nsrcbufs,
			    void *dstbuf,
			    unsigned int bufsize)
{
	int i, j, k;

	for (i = 0; i < bufsize; i++) {
		u8 val = 0;

		for (j = 0; j < 8; j++) {
			unsigned int cnt = 0;

			for (k = 0; k < nsrcbufs; k++) {
				const u8 *srcbuf = srcbufs[k];

				if (srcbuf[i] & BIT(j))
					cnt++;
			}

			if (cnt > nsrcbufs / 2)
				val |= BIT(j);
		}

		((u8 *)dstbuf)[i] = val;
	}
}
EXPORT_SYMBOL_GPL(nand_bit_wise_majority);

/**
 * sanitize_string() - Sanitize ONFI strings so we can safely print them
 * @s: string to be sanitized
 * @len: length of the string
 *
 */
void sanitize_string(u8 *s, size_t len)
{
	ssize_t i;

	/* Null terminate */
	s[len - 1] = 0;

	/* Remove non printable chars */
	for (i = 0; i < len - 1; i++) {
		if (s[i] < ' ' || s[i] > 127)
			s[i] = '?';
	}

	/* Remove trailing spaces */
	strim(s);
}
EXPORT_SYMBOL_GPL(sanitize_string);
