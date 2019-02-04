/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 Micron Technology, Inc.
 *
 * Authors:
 *	Shivamurthy Shastri <sshivamurthy@micron.com>
 */

#ifndef __MICRON_H
#define __MICRON_H

#define SPINAND_MFR_MICRON		0x2c

#define MICRON_STATUS_ECC_MASK		GENMASK(7, 4)
#define MICRON_STATUS_ECC_NO_BITFLIPS	(0 << 4)
#define MICRON_STATUS_ECC_1TO3_BITFLIPS	BIT(4)
#define MICRON_STATUS_ECC_4TO6_BITFLIPS	(3 << 4)
#define MICRON_STATUS_ECC_7TO8_BITFLIPS	(5 << 4)

#define UNIQUE_ID_PAGE	0x00
#define PARAMETER_PAGE	0x01

/*
 * Micron SPI NAND has parameter table similar to ONFI
 */
struct micron_spinand_params {
	/* rev info and features block */
	u8 sig[4];
	__le16 revision;
	__le16 features;
	__le16 opt_cmd;
	u8 reserved0[22];

	/* manufacturer information block */
	char manufacturer[12];
	char model[20];
	u8 manufact_id;
	__le16 date_code;
	u8 reserved1[13];

	/* memory organization block */
	__le32 byte_per_page;
	__le16 spare_bytes_per_page;
	__le32 data_bytes_per_ppage;
	__le16 spare_bytes_per_ppage;
	__le32 pages_per_block;
	__le32 blocks_per_lun;
	u8 lun_count;
	u8 addr_cycles;
	u8 bits_per_cell;
	__le16 bb_per_lun;
	__le16 block_endurance;
	u8 guaranteed_good_blocks;
	__le16 guaranteed_block_endurance;
	u8 programs_per_page;
	u8 ppage_attr;
	u8 ecc_bits;
	u8 interleaved_bits;
	u8 interleaved_ops;
	u8 reserved2[13];

	/* electrical parameter block */
	u8 io_pin_capacitance_max;
	__le16 async_timing_mode;
	__le16 program_cache_timing_mode;
	__le16 t_prog;
	__le16 t_bers;
	__le16 t_r;
	__le16 t_ccs;
	u8 reserved3[23];

	/* vendor */
	__le16 vendor_revision;
	u8 vendor_specific[14];
	u8 reserved4[68];
	u8 ecc_max_correct_ability;
	u8 die_select_feature;
	u8 reserved5[4];

	__le16 crc;
} __packed;

#endif /* __MICRON_H */
