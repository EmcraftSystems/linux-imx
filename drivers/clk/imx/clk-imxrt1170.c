/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright(C) 2023 Emcraft Systems
 * Author(s): Vladimir Skvortsov <vskvortsov@emcraft.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/imxrt1170-clock.h>

#include "clk.h"

#define IMXRT1170_CLK_SRC_COMMON "rcosc48M_div2", "osc", "rcosc400M", "rcosc16M"

static const char * const m7_sels[] = {IMXRT1170_CLK_SRC_COMMON,
"pll_arm_out", "pll1_sys", "pll3_sys", "video_pll"};
static const char * const bus_sels[] = {IMXRT1170_CLK_SRC_COMMON,
"pll3_sys", "pll1_div5", "pll2_sys", "pll2_pfd3"};
static const char * const lpuart1_sels[] = {IMXRT1170_CLK_SRC_COMMON,
"pll3_div2", "pll1_div5", "pll2_sys", "pll2_pfd3"};
static const char * const gpt1_sels[] = {IMXRT1170_CLK_SRC_COMMON,
"pll3_div2", "pll1_div5", "pll3_pfd2", "pll3_pfd3"};
static const char * const usdhc1_sels[] = {IMXRT1170_CLK_SRC_COMMON,
"pll2_pfd2", "pll2_pfd0", "pll1_div5", "pll_arm"};
static const char * const semc_sels[] = {IMXRT1170_CLK_SRC_COMMON,
"pll1_div5", "pll2_sys", "pll2_pfd1", "pll3_pfd0"};
static const char * const enet1_sels[] = {IMXRT1170_CLK_SRC_COMMON,
"pll1_div2", "audio_pll", "pll1_div5", "pll2_pfd1"};


struct clk_hw *imxrt1170_clk_pll_div_out_composite(const char *name, const char *parent_name,
						void __iomem *reg, int div_factor, int gate_bit, unsigned long flags)
{
	struct clk_hw *hw = ERR_PTR(-ENOMEM);
	struct clk_fixed_factor *div = NULL;
	struct clk_gate *gate = NULL;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		goto fail;

	div->mult = 1;
	div->div = div_factor;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		goto fail;

	gate->reg = reg;
	gate->bit_idx = gate_bit;
	gate->flags = flags;

	hw = clk_hw_register_composite(NULL, name, &parent_name, 1,
					NULL, NULL,
					&div->hw, &clk_fixed_factor_ops,
					&gate->hw, &clk_gate_ops, flags);
	if (IS_ERR(hw))
		goto fail;

	return hw;

fail:
	kfree(gate);
	kfree(div);
	return ERR_CAST(hw);
}


struct imxrt1170_clk_root {
	u32 clk_id;
	char *name;
	const char * const *parent_names;
	u32 off;
	unsigned long flags;
};

static struct imxrt1170_clk_root clk_roots[] = {
	{ IMXRT1170_CLK_ROOT_M7, "m7_root", m7_sels, 0, CLK_IS_CRITICAL },
	{ IMXRT1170_CLK_ROOT_BUS, "bus_root", bus_sels, (2 * 0x80), CLK_IS_CRITICAL },
        { IMXRT1170_CLK_ROOT_SEMC, "semc_root", semc_sels, (4 * 0x80), CLK_IS_CRITICAL },
	{ IMXRT1170_CLK_ROOT_GPT1, "gpt1_root", gpt1_sels, (14 * 0x80), },
	{ IMXRT1170_CLK_ROOT_LPUART1, "lpuart1_root", lpuart1_sels, (25 * 0x80), },
	{ IMXRT1170_CLK_ROOT_ENET1, "enet1_root", enet1_sels, (51 * 0x80), },
	{ IMXRT1170_CLK_ROOT_USDHC1, "usdhc1_root", usdhc1_sels, (58 * 0x80), },
};

struct imxrt1170_clk_ccgr {
	u32 clk_id;
	char *name;
	char *parent_names;
	u32 off;
	unsigned long flags;
};

static struct imxrt1170_clk_ccgr clk_ccgrs[] = {
	{ IMXRT1170_CLK_M7, "m7", "m7_root", 0x6000, CLK_IS_CRITICAL },
        { IMXRT1170_CLK_SEMC, "semc", "semc_root", (0x6000 + (33 * 0x20)), CLK_IS_CRITICAL },
        { IMXRT1170_CLK_GPT1, "gpt1", "gpt1_root", (0x6000 + (64 * 0x20)), },
	{ IMXRT1170_CLK_LPUART1, "lpuart1", "lpuart1_root", (0x6000 + (86 * 0x20)), },
	{ IMXRT1170_CLK_ENET1, "enet1", "enet1_root", (0x6000 + (112 * 0x20)), },
	{ IMXRT1170_CLK_USDHC1, "usdhc1", "usdhc1_root", (0x6000 + (117 * 0x20)), },
};

static struct clk_hw **hws;
static struct clk_hw_onecell_data *clk_hw_data;

static void __init imxrt1170_clocks_init(struct device_node *ccm_node)
{
	void __iomem *base;
	struct device_node *np;
	struct device_node *anp;
	struct imxrt1170_clk_root *root;
	struct imxrt1170_clk_ccgr *ccgr;
	int ret, i;

	clk_hw_data = kzalloc(struct_size(clk_hw_data, hws,
					  IMXRT1170_CLK_END), GFP_KERNEL);

	WARN_ON(!clk_hw_data);

	clk_hw_data->num = IMXRT1170_CLK_END;
	hws = clk_hw_data->hws;

	hws[IMXRT1170_CLK_DUMMY] = imx_clk_hw_fixed("dummy", 0);
	hws[IMXRT1170_CLK_OSC] = imx_obtain_fixed_clk_hw(ccm_node, "osc");
	hws[IMXRT1170_CLK_RCOSC_16M] = imx_obtain_fixed_clk_hw(ccm_node, "rcosc16M");

	/* Anatop clocks */
	anp = of_find_compatible_node(NULL, NULL, "fsl,imxrt-anatop");
	base = of_iomap(anp, 0);
	of_node_put(anp);
	WARN_ON(!base);

	hws[IMXRT1170_CLK_RCOSC_48M] =
	       imx_clk_hw_fixed_factor("rcosc48M", "rcosc16M", 3, 1);
	hws[IMXRT1170_CLK_RCOSC_400M] =
	       imx_clk_hw_fixed_factor("rcosc400M",  "rcosc16M", 25, 1);
	hws[IMXRT1170_CLK_RCOSC_48M_DIV2] =
	       imx_clk_hw_fixed_factor("rcosc48M_div2",  "rcosc48M", 1, 2);

	hws[IMXRT1170_CLK_PLL_ARM] =
	       imx_clk_hw_pll_arm_rt1170("pll_arm", "osc", base + 0x200);
	hws[IMXRT1170_CLK_PLL_ARM_OUT] =
	       imx_clk_hw_gate_dis("pll_arm_out", "pll_arm", base + 0x200, 30);

	hws[IMXRT1170_CLK_PLL3] =
	       imx_clk_hw_pllv3(IMX_PLLV3_SYS_RT1170, "pll3_sys", "osc",
			     base + 0x210, 1);
	hws[IMXRT1170_CLK_PLL2] =
	       imx_clk_hw_pllv3(IMX_PLLV3_SYS_RT1170, "pll2_sys", "osc",
			     base + 0x240, 1);
	hws[IMXRT1170_CLK_PLL1] =
	       imx_clk_hw_pllv3(IMX_PLLV3_ENET_1G, "pll1_sys", "osc",
			     base + 0x2c0, 1);
	hws[IMXRT1170_CLK_PLL1_OUT] =
	       imx_clk_hw_gate_dis("pll1_out", "pll1_sys", base + 0x2c0, 14);

	hws[IMXRT1170_CLK_PLL3_PFD0] =
	       imx_clk_hw_pfd("pll3_pfd0", "pll3_sys", base + 0x230, 0);
	hws[IMXRT1170_CLK_PLL3_PFD1] =
	       imx_clk_hw_pfd("pll3_pfd1", "pll3_sys", base + 0x230, 1);
	hws[IMXRT1170_CLK_PLL3_PFD2] =
	       imx_clk_hw_pfd("pll3_pfd2", "pll3_sys", base + 0x230, 2);
	hws[IMXRT1170_CLK_PLL3_PFD3] =
	       imx_clk_hw_pfd("pll3_pfd3", "pll3_sys", base + 0x230, 3);

	hws[IMXRT1170_CLK_PLL2_PFD0] =
	       imx_clk_hw_pfd("pll2_pfd0", "pll2_sys", base + 0x270, 0);
	hws[IMXRT1170_CLK_PLL2_PFD1] =
	       imx_clk_hw_pfd("pll2_pfd1", "pll2_sys", base + 0x270, 1);
	hws[IMXRT1170_CLK_PLL2_PFD2] =
	       imx_clk_hw_pfd("pll2_pfd2", "pll2_sys", base + 0x270, 2);
	hws[IMXRT1170_CLK_PLL2_PFD3] =
	       imx_clk_hw_pfd("pll2_pfd3", "pll2_sys", base + 0x270, 3);

	hws[IMXRT1170_CLK_PLL3_DIV2] =
	       imxrt1170_clk_pll_div_out_composite("pll3_div2", "pll3_sys", base + 0x210, 2, 3, 0);

	hws[IMXRT1170_CLK_PLL1_DIV2] =
	       imxrt1170_clk_pll_div_out_composite("pll1_div2", "pll1_out", base + 0x2c0, 2, 25, 0);
	hws[IMXRT1170_CLK_PLL1_DIV5] =
	       imxrt1170_clk_pll_div_out_composite("pll1_div5", "pll1_out", base + 0x2c0, 5, 26, 0);

	/* CCM clocks */
	np = ccm_node;
	base = of_iomap(np, 0);
	WARN_ON(!base);

	for (i = 0; i < ARRAY_SIZE(clk_roots); i++) {
		root = &clk_roots[i];
		hws[root->clk_id] = imx93_clk_composite_flags(root->name,
							      root->parent_names,
							      8, base + root->off, 3,
							      root->flags);
	}

	for (i = 0; i < ARRAY_SIZE(clk_ccgrs); i++) {
		ccgr = &clk_ccgrs[i];
		hws[ccgr->clk_id] = imx_clk_hw_gate_flags(ccgr->name, ccgr->parent_names,
							  base + ccgr->off, 0, ccgr->flags);
	}

	imx_check_clk_hws(hws, IMXRT1170_CLK_END);

	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_hw_data);
	if (ret < 0) {
		imx_unregister_hw_clocks(hws, IMXRT1170_CLK_END);
	}
}

CLK_OF_DECLARE(imxrt1170, "fsl,imxrt1170-ccm", imxrt1170_clocks_init);
