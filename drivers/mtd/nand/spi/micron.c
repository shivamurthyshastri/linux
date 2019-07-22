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

#define SPINAND_MFR_MICRON		0x2c

#define MICRON_STATUS_ECC_MASK		GENMASK(7, 4)
#define MICRON_STATUS_ECC_NO_BITFLIPS	(0 << 4)
#define MICRON_STATUS_ECC_1TO3_BITFLIPS	(1 << 4)
#define MICRON_STATUS_ECC_4TO6_BITFLIPS	(3 << 4)
#define MICRON_STATUS_ECC_7TO8_BITFLIPS	(5 << 4)

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

static int micron_ooblayout_ecc(struct mtd_info *mtd, int section,
				struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = mtd->oobsize / 2;
	region->length = mtd->oobsize / 2;

	return 0;
}

static int micron_ooblayout_free(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	/* Reserve 2 bytes for the BBM. */
	region->offset = 2;
	region->length = (mtd->oobsize / 2) - 2;

	return 0;
}

static const struct mtd_ooblayout_ops micron_ooblayout_ops = {
	.ecc = micron_ooblayout_ecc,
	.free = micron_ooblayout_free,
};

static int micron_ecc_get_status(struct spinand_device *spinand,
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

static int micron_select_target(struct spinand_device *spinand,
				unsigned int target)
{
	struct spi_mem_op op = SPINAND_SET_FEATURE_OP(0xd0,
						      spinand->scratchbuf);

	if (target == 1)
		target = 0x40;

	*spinand->scratchbuf = target;
	return spi_mem_exec_op(spinand->spimem, &op);
}

static int micron_spinand_detect(struct spinand_device *spinand)
{
	const struct spi_mem_op *op;
	u8 *id = spinand->id.data;

	/*
	 * Micron SPI NAND read ID need a dummy byte,
	 * so the first byte in raw_id is dummy.
	 */
	if (id[1] != SPINAND_MFR_MICRON)
		return 0;

	spinand->flags = 0;
	spinand->eccinfo.get_status = micron_ecc_get_status;
	spinand->eccinfo.ooblayout = &micron_ooblayout_ops;
	spinand->select_target = micron_select_target;

	op = spinand_select_op_variant(spinand,
				       &read_cache_variants);
	if (!op)
		return -ENOTSUPP;

	spinand->op_templates.read_cache = op;

	op = spinand_select_op_variant(spinand,
				       &write_cache_variants);
	if (!op)
		return -ENOTSUPP;

	spinand->op_templates.write_cache = op;

	op = spinand_select_op_variant(spinand,
				       &update_cache_variants);
	spinand->op_templates.update_cache = op;

	return 1;
}

static int micron_spinand_init(struct spinand_device *spinand)
{
	/*
	 * Some of the Micron flashes enable this BIT by default,
	 * and there is a chance of read failure due to this.
	 */
	return spinand_upd_cfg(spinand, CFG_QUAD_ENABLE, 0);
}

static void micron_fixup_param_page(struct spinand_device *spinand,
				    struct nand_onfi_params *p)
{
	/*
	 * As per Micron datasheets vendor[83] is defined as
	 * die_select_feature
	 */
	if (p->vendor[83] && !p->interleaved_bits)
		spinand->base.memorg.planes_per_lun = 1 << p->vendor[0];

	spinand->base.memorg.ntargets = p->lun_count;
	spinand->base.memorg.luns_per_target = 1;

	/*
	 * As per Micron datasheets,
	 * vendor[82] is ECC maximum correctability
	 */
	spinand->base.eccreq.strength = p->vendor[82];
	spinand->base.eccreq.step_size = 512;
}

static const struct spinand_manufacturer_ops micron_spinand_manuf_ops = {
	.detect = micron_spinand_detect,
	.init = micron_spinand_init,
	.fixup_param_page = micron_fixup_param_page,
};

const struct spinand_manufacturer micron_spinand_manufacturer = {
	.id = SPINAND_MFR_MICRON,
	.name = "Micron",
	.ops = &micron_spinand_manuf_ops,
};
