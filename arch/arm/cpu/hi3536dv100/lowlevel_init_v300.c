/*
 *
 * Copyright (c) 2016-2017 HiSilicon Technologies Co., Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <asm/arch/platform.h>
#include <config.h>
#ifdef CONFIG_DDR_TRAINING_V2
#include <ddr_interface.h>
#endif

#define HI_SYS_CTL_REG	SYS_CTRL_REG_BASE

#define REG_SC_GEN32		0xa0
#define DDR_DMC_BASE_REG0	0x12118000
#define DDR0_PLL_REG		0x1211c000

#define REG_DDR_DMC0_CFG_TIMING2 (DDR_DMC_BASE_REG0 + 0x108)
#define REG_DDR_PHY0_INITCTRL    (DDR0_PLL_REG + 0x4)
#define REG_DDR_DMC0_CTRL_SREF   DDR_DMC_BASE_REG0
#define REG_DDR_DMC0_IN_SREF     (DDR_DMC_BASE_REG0 + 0x294)

extern void reset_cpu(unsigned long addr);

static inline void DWB(void) /* drain write buffer */
{
}

static inline unsigned int readl(unsigned addr)
{
	unsigned int val;

	val = (*(volatile unsigned int *)(addr));
	return val;
}

static inline void writel(unsigned val, unsigned addr)
{
	DWB();
	(*(volatile unsigned *) (addr)) = (val);
	DWB();
}

static void ddr_set_sref(unsigned int val, unsigned int addr_sref,
		unsigned int st_reg)
{
	int cnt = 0;
	/* if write val == 1, wait 1; if write val == 2, wait 0 */
	unsigned int wait_val = val & 0x1;
	unsigned int read_val;

	writel(val, addr_sref);
	while (cnt < 10000) {
		read_val = readl(st_reg) & 0x1;
		if (wait_val == read_val)
			break;

		cnt++;
	}

	if (cnt >= 10000)
		asm ("b .");
}

static void ddr_hw_training(void)
{
	unsigned int initctrl_phy0;

	initctrl_phy0 = readl(HI_SYS_CTL_REG + REG_SC_GEN32);
	if (initctrl_phy0) {
		unsigned int timing;
		unsigned int timing_backup;

		/* Save the Auto-self refresh timing */
		timing = readl(REG_DDR_DMC0_CFG_TIMING2);
		timing_backup = timing & 0x7FF;
		/* Set Auto-self refresh timing to 0 */
		writel((timing & 0xFFFFF800), REG_DDR_DMC0_CFG_TIMING2);
		/* Start HW DDR training */
		writel(initctrl_phy0, REG_DDR_PHY0_INITCTRL);

		/* Exit Auto-self refresh */
		ddr_set_sref(0x2, REG_DDR_DMC0_CTRL_SREF, REG_DDR_DMC0_IN_SREF);

		do {
			initctrl_phy0 = readl(REG_DDR_PHY0_INITCTRL) & 0x7FFFF;
		} while (initctrl_phy0);
		/* Restore the Auto-self refresh timing */
		timing = readl(REG_DDR_DMC0_CFG_TIMING2);
		timing = (timing & 0xFFFFF800) | timing_backup;
		writel(timing, REG_DDR_DMC0_CFG_TIMING2);
	}

	/* Clear fifo */
	initctrl_phy0 = readl(REG_DDR_PHY0_INITCTRL);
	initctrl_phy0 = initctrl_phy0 | (1<<15);
	writel(initctrl_phy0, REG_DDR_PHY0_INITCTRL);
	initctrl_phy0 = readl(REG_DDR_PHY0_INITCTRL);
	initctrl_phy0 = initctrl_phy0 & ~(1<<15);
	writel(initctrl_phy0, REG_DDR_PHY0_INITCTRL);

}
static inline void delay(unsigned int num)
{
	unsigned int i;

	for (i = 0; i < (100 * num); i++)
		__asm__ __volatile__("nop");
}

#ifdef	CONFIG_SVB_ENABLE
#define PWM_REG_BASE	0x120e0000
#define CPU_VOL_REG		(0x04 + PWM_REG_BASE)
#define CORE_VOL_REG	PWM_REG_BASE

#define HPM_CORE_DATA	0x12122000
#define HPM_CPU_DATA	0x1212200c

static void get_hpm_iddq_value(unsigned int addr,
		unsigned int *hpm, unsigned int *iddq)
{
	unsigned int val;

	val = readl(addr);

	if (addr == HPM_CPU_DATA) {
		/* get the HPM && IDDQ of cpu */
		*hpm = val & 0x3ff;
		*iddq = (val & 0x3f0000) >> 16;
	} else if (addr == HPM_CORE_DATA) {
		/* get the HPM && IDDQ of core */
		*hpm = val & 0x3ff;
		*iddq = (val & 0x3f0000) >> 16;
	} else
		asm ("b .");
}

static void set_hpm_iddq_volt(unsigned int val_1,
		unsigned int val_2, unsigned int sel)
{
	if (sel == 0) {
		/* val_1 is cpu hpm */
		if (val_1 <= 395)
			writel(0x3800c7, CPU_VOL_REG);
		else
			writel(0x4f00c7, CPU_VOL_REG);
	} else if (sel == 1) {
		/* val_1 is core hpm */
		if (val_1 <= 250)
			writel(0x1e00c7, CORE_VOL_REG);
		else
			writel(0x3b00c7, CORE_VOL_REG);
	} else if (sel == 2){
		/* val_1 is cpu hpm, val_2 is core hpm.
		 * but core pwm only */
		if ((val_1 > 395) && (val_2 > 250))
			writel(0x2500c7, CORE_VOL_REG);
		else {
			/* CPU HPM -> (0, 395] or CORE HPM -> (0, 250] */
			writel(0x0100c7, CORE_VOL_REG);
		}
	} else
		asm ("b .");
}

void start_svb(void)
{
	unsigned int cpu_hpm;
	unsigned int core_hpm;
	unsigned int cpu_iddq;
	unsigned int core_iddq;
	unsigned int power_src_sel;

	/*
	 * Read and judge the value get from HPM and IDDQ
	 */
	get_hpm_iddq_value(HPM_CPU_DATA, &cpu_hpm, &cpu_iddq);
	get_hpm_iddq_value(HPM_CORE_DATA, &core_hpm, &core_iddq);

	/*
	 * Power source selection [0x12050148]
	 * 0: dual power supply, cpu pwm and core pwm isolation
	 * 1: combine power supply, core pwm only
	 */
	power_src_sel = readl(SYS_CTRL_REG_BASE + REG_SC_GEN4);

	/*
	 * Set the voltage about HPM and IDDQ
	 * 0: CPU
	 * 1: CORE
	 * 2: CPU and CORE combination
	 */
	if ((power_src_sel & 0x1) == 1) {
		set_hpm_iddq_volt(cpu_hpm, core_hpm, 2);
	} else {
		set_hpm_iddq_volt(cpu_hpm, cpu_iddq, 0);
		set_hpm_iddq_volt(core_hpm, core_iddq, 1);
	}
}
#endif

void ddrc_enable_scramble(unsigned int reg)
{
	unsigned int val;

	/* ddr enable SCRAMBLE */
	val = readl(reg + 0x50);
	val |= (1 << 14);
	writel(val, reg + 0x50);
}

/*
 * Hi3536E: a Hi3536DV100 die that reads back 0x02000000 at SYSCTRL + 0xEE0
 * (plain DV100 reads 0). Its DDR needs other PLL, DMC timing and PHY
 * settings than the ones reg_info writes for the DV100 demo board, and with
 * those the software training fails and resets the chip. The values here
 * replace them before hardware training; they come from the SPL of a
 * Xiongmai NBD8010S-KL-V2 NVR, which makes the same check.
 *
 * Plain writel() only: this runs from SRAM, where .rodata is not loaded.
 */
#define REG_SC_SYSID0		0xee0
#define HI3536E_SYSID0		0x02000000

static void hi3536e_ddr_config(void)
{
	/* APLL, DDR PLL and the PLL at 0x18 */
	writel(0x010061f4, CRG_REG_BASE + 0x04);
	writel(0x0000209b, CRG_REG_BASE + 0x2c);
	writel(0x24000000, CRG_REG_BASE + 0x18);
	writel(0x0100106c, CRG_REG_BASE + 0x1c);

	/* DMC timing */
	writel(0x34421a12, DDR_DMC_BASE_REG0 + 0x100);
	writel(0x4144b48d, DDR_DMC_BASE_REG0 + 0x104);
	writel(0x54011070, DDR_DMC_BASE_REG0 + 0x108);
	writel(0xffe32884, DDR_DMC_BASE_REG0 + 0x10c);
	writel(0x00800000, DDR_DMC_BASE_REG0 + 0x110);
	writel(0x00041308, DDR_DMC_BASE_REG0 + 0x114);

	/* PHY timing */
	writel(0xc228ffca, DDR0_PLL_REG + 0x30);
	writel(0x15405148, DDR0_PLL_REG + 0x34);
	writel(0x240a4148, DDR0_PLL_REG + 0x38);
	writel(0xf91002b2, DDR0_PLL_REG + 0x3c);
	writel(0x000067bc, DDR0_PLL_REG + 0x40);
	writel(0x00061e14, DDR0_PLL_REG + 0x64);
	writel(0x00000020, DDR0_PLL_REG + 0x68);
	writel(0x4bddb001, DDR0_PLL_REG + 0x88);
	writel(0x00000009, DDR0_PLL_REG + 0xb4);
	writel(0x10c00588, DDR0_PLL_REG + 0x70);
	writel(0xff00ae45, DDR0_PLL_REG + 0x8c);
	writel(0x00000080, DDR0_PLL_REG + 0x18c);
	writel(0x00000008, DDR0_PLL_REG + 0x120);
	writel(0x00000008, DDR0_PLL_REG + 0x128);
	writel(0x00000008, DDR0_PLL_REG + 0x130);

	/* PHY per-lane delays */
	writel(0x0f100f0e, DDR0_PLL_REG + 0x210);
	writel(0x0d0d100e, DDR0_PLL_REG + 0x214);
	writel(0x0000000e, DDR0_PLL_REG + 0x218);
	writel(0x10100f10, DDR0_PLL_REG + 0x290);
	writel(0x0e0f1010, DDR0_PLL_REG + 0x294);
	writel(0x0000000f, DDR0_PLL_REG + 0x298);
	writel(0x00000000, DDR0_PLL_REG + 0x230);
	writel(0x00000000, DDR0_PLL_REG + 0x2b0);
	writel(0x0e0e120f, DDR0_PLL_REG + 0x21c);
	writel(0x10111011, DDR0_PLL_REG + 0x220);
	writel(0x00000000, DDR0_PLL_REG + 0x224);
	writel(0x0e0e0e10, DDR0_PLL_REG + 0x29c);
	writel(0x0d0f1010, DDR0_PLL_REG + 0x2a0);
	writel(0x00000000, DDR0_PLL_REG + 0x2a4);
	writel(0x00000020, DDR0_PLL_REG + 0x22c);
	writel(0x00000020, DDR0_PLL_REG + 0x2ac);
	writel(0x00000900, DDR0_PLL_REG + 0x234);
	writel(0x00000900, DDR0_PLL_REG + 0x2b4);
}

void start_ddr_training(unsigned int base)
{
#ifdef CONFIG_SVB_ENABLE
	/* add SVB function */
	start_svb();
	delay(10000);
#endif

	if (readl(SYS_CTRL_REG_BASE + REG_SC_SYSID0) == HI3536E_SYSID0)
		hi3536e_ddr_config();

	ddr_hw_training();

#ifdef CONFIG_DDR_TRAINING_V2
	int ret = 0;

	/* ddr training function */
	ret = ddr_sw_training_if(0);
	if (ret)
		reset_cpu(0);
#endif

	ddrc_enable_scramble(DDR_DMC_BASE_REG0);

	/*the value should config after trainning, or
	  it will cause chip compatibility problems*/
	writel(0x401, DDR_DMC_BASE_REG0 + 0x28);
}

