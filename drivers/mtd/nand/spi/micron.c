// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2017 Micron Technology, Inc.
 *
 * Authors:
 *	Peter Pan <peterpandong@micron.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#include "micron.h"

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 2, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int ooblayout_ecc(struct mtd_info *mtd, int section,
			 struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = mtd->oobsize / 2;
	region->length = mtd->oobsize / 2;

	return 0;
}

static int ooblayout_free(struct mtd_info *mtd, int section,
			  struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	/* Reserve 2 bytes for the BBM. */
	region->offset = 2;
	region->length = (mtd->oobsize / 2) - 2;

	return 0;
}

static const struct mtd_ooblayout_ops ooblayout = {
	.ecc = ooblayout_ecc,
	.free = ooblayout_free,
};

static int ecc_get_status(struct spinand_device *spinand,
			  u8 status)
{
	switch (status & MICRON_STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case MICRON_STATUS_ECC_1TO3_BITFLIPS:
		return 3;

	case MICRON_STATUS_ECC_4TO6_BITFLIPS:
		return 6;

	case MICRON_STATUS_ECC_7TO8_BITFLIPS:
		return 8;

	default:
		break;
	}

	return -EINVAL;
}

static u16 spinand_crc16(u16 crc, u8 const *p, size_t len)
{
	int i;

	while (len--) {
		crc ^= *p++ << 8;
		for (i = 0; i < 8; i++)
			crc = (crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0);
	}

	return crc;
}

static void bit_wise_majority(const void **srcbufs,
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

static int micron_spinand_detect(struct spinand_device *spinand)
{
	struct spinand_info deviceinfo;
	struct micron_spinand_params *params;
	u8 *id = spinand->id.data;
	int ret, i;

	/*
	 * Micron SPI NAND read ID need a dummy byte,
	 * so the first byte in raw_id is dummy.
	 */
	if (id[1] != SPINAND_MFR_MICRON)
		return 0;

	params = kzalloc(sizeof(*params) * 3, GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	ret = spinand_parameter_page_read(spinand, PARAMETER_PAGE, params,
					  sizeof(*params) * 3);
	if (ret)
		goto free_params;

	for (i = 0; i < 3; i++) {
		if (spinand_crc16(0x4F4E, (u8 *)&params[i], 254) ==
				le16_to_cpu(params->crc)) {
			if (i)
				memcpy(params, &params[i], sizeof(*params));
			break;
		}
	}

	if (i == 3) {
		const void *srcbufs[3] = {params, params + 1, params + 2};

		pr_warn("No valid parameter page, trying bit-wise majority to recover it\n");
		bit_wise_majority(srcbufs, ARRAY_SIZE(srcbufs), params,
				  sizeof(*params));

		if (spinand_crc16(0x4F4E, (u8 *)params, 254) !=
				le16_to_cpu(params->crc)) {
			pr_err("Parameter page recovery failed, aborting\n");
			goto free_params;
		}
	}

	params->model[sizeof(params->model) - 1] = 0;
	strim(params->model);

	deviceinfo.model = kstrdup(params->model, GFP_KERNEL);
	if (!deviceinfo.model) {
		ret = -ENOMEM;
		goto free_params;
	}

	deviceinfo.devid = id[2];
	deviceinfo.flags = 0;
	deviceinfo.memorg.bits_per_cell = params->bits_per_cell;
	deviceinfo.memorg.pagesize = params->byte_per_page;
	deviceinfo.memorg.oobsize = params->spare_bytes_per_page;
	deviceinfo.memorg.pages_per_eraseblock = params->pages_per_block;
	deviceinfo.memorg.eraseblocks_per_lun =
		params->blocks_per_lun * params->lun_count;
	deviceinfo.memorg.planes_per_lun = params->lun_count;
	deviceinfo.memorg.luns_per_target = 1;
	deviceinfo.memorg.ntargets = 1;
	deviceinfo.eccreq.strength = params->ecc_max_correct_ability;
	deviceinfo.eccreq.step_size = 512;
	deviceinfo.eccinfo.get_status = ecc_get_status;
	deviceinfo.eccinfo.ooblayout = &ooblayout;
	deviceinfo.op_variants.read_cache = &read_cache_variants;
	deviceinfo.op_variants.write_cache = &write_cache_variants;
	deviceinfo.op_variants.update_cache = &update_cache_variants;

	ret = spinand_match_and_init(spinand, &deviceinfo,
				     1, id[2]);
	if (ret)
		goto free_model;

	kfree(params);

	return 1;

free_model:
	kfree(deviceinfo.model);
free_params:
	kfree(params);

	return ret;
}

static int micron_spinand_init(struct spinand_device *spinand)
{
	/*
	 * Some of the Micron flashes enable this BIT by default,
	 * and there is a chance of read failure due to this.
	 */
	return spinand_upd_cfg(spinand, CFG_QUAD_ENABLE, 0);
}

static const struct spinand_manufacturer_ops micron_spinand_manuf_ops = {
	.detect = micron_spinand_detect,
	.init = micron_spinand_init,
};

const struct spinand_manufacturer micron_spinand_manufacturer = {
	.id = SPINAND_MFR_MICRON,
	.name = "Micron",
	.ops = &micron_spinand_manuf_ops,
};
