/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

#include <linux/autoconf.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/dma-mapping.h>

/* +++ by chhung */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/pm.h>
#include <asm/rt2880/surfboardint.h>

#define MSDC_SMPL_FALLING   (1)
#define MSDC_CD_PIN_EN      (1 << 0)  /* card detection pin is wired   */
#define MSDC_WP_PIN_EN      (1 << 1)  /* write protection pin is wired */
#define MSDC_REMOVABLE      (1 << 5)  /* removable slot                */
#define MSDC_SYS_SUSPEND    (1 << 6)  /* suspended by system           */
#define MSDC_HIGHSPEED      (1 << 7)

//#define IRQ_SDC 14	//MT7620 /*FIXME*/
#define IRQ_SDC SURFBOARDINT_SDXC	/*FIXME*/

#include <asm/dma.h>
#include <asm/rt2880/rt_mmap.h>
/* end of +++ */

#if 0 /* --- by chhung */
#include <mach/board.h>
#include <mach/mt6575_devs.h>
#include <mach/mt6575_typedefs.h>
#include <mach/mt6575_clock_manager.h>
#include <mach/mt6575_pm_ldo.h>
//#include <mach/mt6575_pll.h>
//#include <mach/mt6575_gpio.h>
//#include <mach/mt6575_gpt_sw.h>
#include <asm/tcm.h>
// #include <mach/mt6575_gpt.h>
#endif /* end of --- */

#include "mt6575_sd.h"
#include "dbg.h"

/* +++ by chhung */
#include "board.h"
/* end of +++ */

#if 0 /* --- by chhung */
#define isb() __asm__ __volatile__ ("" : : : "memory")
#define dsb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#define dmb() __asm__ __volatile__ ("" : : : "memory")
#endif /* end of --- */

#define DRV_NAME            "mtk-sd"

#define HOST_MAX_NUM        (1) /* +/- by chhung */

#if defined (CONFIG_RALINK_MT7620) || defined (CONFIG_RALINK_MT7628)
#define HOST_MAX_MCLK       (48000000) /* +/- by chhung */
#elif defined (CONFIG_RALINK_MT7621)
#define HOST_MAX_MCLK       (50000000) /* +/- by chhung */
#endif
#define HOST_MIN_MCLK       (260000)

#define HOST_MAX_BLKSZ      (2048)

#define MSDC_OCR_AVAIL      (MMC_VDD_28_29 | MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33)

#define GPIO_PULL_DOWN      (0)
#define GPIO_PULL_UP        (1)

#if 0 /* --- by chhung */
#define MSDC_CLKSRC_REG     (0xf100000C)
#define PDN_REG           (0xF1000010) 
#endif /* end of --- */

#define DEFAULT_DEBOUNCE    (8)       /* 8 cycles */
#define DEFAULT_DTOC        (40)      /* data timeout counter. 65536x40 sclk. */

#define CMD_TIMEOUT         (HZ/10)     /* 100ms */
#define DAT_TIMEOUT         (HZ/2 * 5)  /* 500ms x5 */

#define MAX_DMA_CNT         (64 * 1024 - 512)   /* a single transaction for WIFI may be 50K*/

#define MAX_GPD_NUM         (1 + 1)  /* one null gpd */
#define MAX_BD_NUM          (1024)
#define MAX_BD_PER_GPD      (MAX_BD_NUM)

#define MAX_HW_SGMTS        (MAX_BD_NUM)
#define MAX_PHY_SGMTS       (MAX_BD_NUM)
#define MAX_SGMT_SZ         (MAX_DMA_CNT)
#define MAX_REQ_SZ          (MAX_SGMT_SZ * 8)  

#ifdef MT6575_SD_DEBUG
static struct msdc_regs *msdc_reg[HOST_MAX_NUM];
#endif 

//=================================
#define PERI_MSDC0_PDN      (15)
//#define PERI_MSDC1_PDN    (16)
//#define PERI_MSDC2_PDN    (17)
//#define PERI_MSDC3_PDN    (18)

struct msdc_host *msdc_6575_host[] = {NULL,NULL,NULL,NULL};
#if 0 /* --- by chhung */
/* gate means clock power down */
static int g_clk_gate = 0; 
#define msdc_gate_clock(id) \
    do { \
        g_clk_gate &= ~(1 << ((id) + PERI_MSDC0_PDN)); \
    } while(0)
/* not like power down register. 1 means clock on. */
#define msdc_ungate_clock(id) \
    do { \
        g_clk_gate |= 1 << ((id) + PERI_MSDC0_PDN); \
    } while(0)

// do we need sync object or not 
void msdc_clk_status(int * status)
{
    *status = g_clk_gate;    	
}
#endif /* end of --- */

/* +++ by chhung */
struct msdc_hw msdc0_hw = {
	.clk_src        = 0,
	.cmd_edge       = MSDC_SMPL_FALLING,
	.data_edge      = MSDC_SMPL_FALLING,
	.clk_drv        = 4,
	.cmd_drv        = 4,
	.dat_drv        = 4,
	.data_pins      = 4,
	.data_offset    = 0,
	.flags          = MSDC_SYS_SUSPEND | MSDC_WP_PIN_EN | MSDC_CD_PIN_EN | MSDC_REMOVABLE | MSDC_HIGHSPEED,
//	.flags          = MSDC_SYS_SUSPEND | MSDC_WP_PIN_EN | MSDC_CD_PIN_EN | MSDC_REMOVABLE,
};

static struct resource mtk_sd_resources[] = {
	[0] = {
		.start  = RALINK_MSDC_BASE,
		.end    = RALINK_MSDC_BASE+0x3fff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_SDC,	/*FIXME*/
		.end    = IRQ_SDC,	/*FIXME*/
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device mtk_sd_device = {
	.name           = "mtk-sd",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(mtk_sd_resources),
	.resource       = mtk_sd_resources,
};
/* end of +++ */

static int msdc_rsp[] = {
    0,  /* RESP_NONE */
    1,  /* RESP_R1 */
    2,  /* RESP_R2 */
    3,  /* RESP_R3 */
    4,  /* RESP_R4 */
    1,  /* RESP_R5 */
    1,  /* RESP_R6 */
    1,  /* RESP_R7 */
    7,  /* RESP_R1b */
};

/* For Inhanced DMA */
#define msdc_init_gpd_ex(gpd,extlen,cmd,arg,blknum) \
    do { \
        ((gpd_t*)gpd)->extlen = extlen; \
        ((gpd_t*)gpd)->cmd    = cmd; \
        ((gpd_t*)gpd)->arg    = arg; \
        ((gpd_t*)gpd)->blknum = blknum; \
    }while(0)
    
#define msdc_init_bd(bd, blkpad, dwpad, dptr, dlen) \
    do { \
        BUG_ON(dlen > 0xFFFFUL); \
        ((bd_t*)bd)->blkpad = blkpad; \
        ((bd_t*)bd)->dwpad  = dwpad; \
        ((bd_t*)bd)->ptr    = (void*)dptr; \
        ((bd_t*)bd)->buflen = dlen; \
    }while(0)

#define msdc_txfifocnt()   ((sdr_read32(MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT) >> 16)
#define msdc_rxfifocnt()   ((sdr_read32(MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT) >> 0)
#define msdc_fifo_write32(v)   sdr_write32(MSDC_TXDATA, (v))
#define msdc_fifo_write8(v)    sdr_write8(MSDC_TXDATA, (v))
#define msdc_fifo_read32()   sdr_read32(MSDC_RXDATA)
#define msdc_fifo_read8()    sdr_read8(MSDC_RXDATA)	


#define msdc_dma_on()        sdr_clr_bits(MSDC_CFG, MSDC_CFG_PIO)
#define msdc_dma_off()       sdr_set_bits(MSDC_CFG, MSDC_CFG_PIO)

#define msdc_retry(expr,retry,cnt) \
    do { \
        int backup = cnt; \
        while (retry) { \
            if (!(expr)) break; \
            if (cnt-- == 0) { \
                retry--; mdelay(1); cnt = backup; \
            } \
        } \
        WARN_ON(retry == 0); \
    } while(0)

#if 0 /* --- by chhung */
#define msdc_reset() \
    do { \
        int retry = 3, cnt = 1000; \
        sdr_set_bits(MSDC_CFG, MSDC_CFG_RST); \
        dsb(); \
        msdc_retry(sdr_read32(MSDC_CFG) & MSDC_CFG_RST, retry, cnt); \
    } while(0)
#else
#define msdc_reset() \
    do { \
        int retry = 3, cnt = 1000; \
        sdr_set_bits(MSDC_CFG, MSDC_CFG_RST); \
        msdc_retry(sdr_read32(MSDC_CFG) & MSDC_CFG_RST, retry, cnt); \
    } while(0)
#endif /* end of +/- */

#define msdc_clr_int() \
    do { \
        volatile u32 val = sdr_read32(MSDC_INT); \
        sdr_write32(MSDC_INT, val); \
    } while(0)

#define msdc_clr_fifo() \
    do { \
        int retry = 3, cnt = 1000; \
        sdr_set_bits(MSDC_FIFOCS, MSDC_FIFOCS_CLR); \
        msdc_retry(sdr_read32(MSDC_FIFOCS) & MSDC_FIFOCS_CLR, retry, cnt); \
    } while(0)

#define msdc_irq_save(val) \
    do { \
        val = sdr_read32(MSDC_INTEN); \
        sdr_clr_bits(MSDC_INTEN, val); \
    } while(0)
	
#define msdc_irq_restore(val) \
    do { \
        sdr_set_bits(MSDC_INTEN, val); \
    } while(0)

/* clock source for host: global */
#if defined (CONFIG_RALINK_MT7620) || defined (CONFIG_RALINK_MT7628)
static u32 hclks[] = {48000000}; /* +/- by chhung */
#elif defined (CONFIG_RALINK_MT7621)
static u32 hclks[] = {50000000}; /* +/- by chhung */
#endif

//============================================
// the power for msdc host controller: global
//    always keep the VMC on. 
//============================================
#define msdc_vcore_on(host) \
    do { \
        INIT_MSG("[+]VMC ref. count<%d>", ++host->pwr_ref); \
        (void)hwPowerOn(MT65XX_POWER_LDO_VMC, VOL_3300, "SD"); \
    } while (0)
#define msdc_vcore_off(host) \
    do { \
        INIT_MSG("[-]VMC ref. count<%d>", --host->pwr_ref); \
        (void)hwPowerDown(MT65XX_POWER_LDO_VMC, "SD"); \
    } while (0)

//====================================
// the vdd output for card: global 
//   always keep the VMCH on. 
//==================================== 
#define msdc_vdd_on(host) \
    do { \
        (void)hwPowerOn(MT65XX_POWER_LDO_VMCH, VOL_3300, "SD"); \
    } while (0)   
#define msdc_vdd_off(host) \
    do { \
        (void)hwPowerDown(MT65XX_POWER_LDO_VMCH, "SD"); \
    } while (0)      

#define sdc_is_busy()          (sdr_read32(SDC_STS) & SDC_STS_SDCBUSY)
#define sdc_is_cmd_busy()      (sdr_read32(SDC_STS) & SDC_STS_CMDBUSY)

#define sdc_send_cmd(cmd,arg) \
    do { \
        sdr_write32(SDC_ARG, (arg)); \
        sdr_write32(SDC_CMD, (cmd)); \
    } while(0)

// can modify to read h/w register.
//#define is_card_present(h)   ((sdr_read32(MSDC_PS) & MSDC_PS_CDSTS) ? 0 : 1);
#define is_card_present(h)     (((struct msdc_host*)(h))->card_inserted)

/* +++ by chhung */
#ifndef __ASSEMBLY__
#define PHYSADDR(a)             (((unsigned long)(a)) & 0x1fffffff)
#else
#define PHYSADDR(a)             ((a) & 0x1fffffff)
#endif
/* end of +++ */
static unsigned int msdc_do_command(struct msdc_host   *host, 
                                      struct mmc_command *cmd,
                                      int                 tune,
                                      unsigned long       timeout);  
                                     
static int msdc_tune_cmdrsp(struct msdc_host*host,struct mmc_command *cmd);

#ifdef MT6575_SD_DEBUG
static void msdc_dump_card_status(struct msdc_host *host, u32 status)
{
    static char *state[] = {
        "Idle",			/* 0 */
        "Ready",		/* 1 */
        "Ident",		/* 2 */
        "Stby",			/* 3 */
        "Tran",			/* 4 */
        "Data",			/* 5 */
        "Rcv",			/* 6 */
        "Prg",			/* 7 */
        "Dis",			/* 8 */
        "Reserved",		/* 9 */
        "Reserved",		/* 10 */
        "Reserved",		/* 11 */
        "Reserved",		/* 12 */
        "Reserved",		/* 13 */
        "Reserved",		/* 14 */
        "I/O mode",		/* 15 */
    };
    if (status & R1_OUT_OF_RANGE)
        N_MSG(RSP, "[CARD_STATUS] Out of Range");
    if (status & R1_ADDRESS_ERROR)
        N_MSG(RSP, "[CARD_STATUS] Address Error");
    if (status & R1_BLOCK_LEN_ERROR)
        N_MSG(RSP, "[CARD_STATUS] Block Len Error");
    if (status & R1_ERASE_SEQ_ERROR)
        N_MSG(RSP, "[CARD_STATUS] Erase Seq Error");
    if (status & R1_ERASE_PARAM)
        N_MSG(RSP, "[CARD_STATUS] Erase Param");
    if (status & R1_WP_VIOLATION)
        N_MSG(RSP, "[CARD_STATUS] WP Violation");
    if (status & R1_CARD_IS_LOCKED)
        N_MSG(RSP, "[CARD_STATUS] Card is Locked");
    if (status & R1_LOCK_UNLOCK_FAILED)
        N_MSG(RSP, "[CARD_STATUS] Lock/Unlock Failed");
    if (status & R1_COM_CRC_ERROR)
        N_MSG(RSP, "[CARD_STATUS] Command CRC Error");
    if (status & R1_ILLEGAL_COMMAND)
        N_MSG(RSP, "[CARD_STATUS] Illegal Command");
    if (status & R1_CARD_ECC_FAILED)
        N_MSG(RSP, "[CARD_STATUS] Card ECC Failed");
    if (status & R1_CC_ERROR)
        N_MSG(RSP, "[CARD_STATUS] CC Error");
    if (status & R1_ERROR)
        N_MSG(RSP, "[CARD_STATUS] Error");
    if (status & R1_UNDERRUN)
        N_MSG(RSP, "[CARD_STATUS] Underrun");
    if (status & R1_OVERRUN)
        N_MSG(RSP, "[CARD_STATUS] Overrun");
    if (status & R1_CID_CSD_OVERWRITE)
        N_MSG(RSP, "[CARD_STATUS] CID/CSD Overwrite");
    if (status & R1_WP_ERASE_SKIP)
        N_MSG(RSP, "[CARD_STATUS] WP Eraser Skip");
    if (status & R1_CARD_ECC_DISABLED)
        N_MSG(RSP, "[CARD_STATUS] Card ECC Disabled");
    if (status & R1_ERASE_RESET)
        N_MSG(RSP, "[CARD_STATUS] Erase Reset");
    if (status & R1_READY_FOR_DATA)
        N_MSG(RSP, "[CARD_STATUS] Ready for Data");
    if (status & R1_SWITCH_ERROR)
        N_MSG(RSP, "[CARD_STATUS] Switch error");
    if (status & R1_APP_CMD)
        N_MSG(RSP, "[CARD_STATUS] App Command");
    
    N_MSG(RSP, "[CARD_STATUS] '%s' State", state[R1_CURRENT_STATE(status)]);
}

static void msdc_dump_ocr_reg(struct msdc_host *host, u32 resp)
{
    if (resp & (1 << 7))
        N_MSG(RSP, "[OCR] Low Voltage Range");
    if (resp & (1 << 15))
        N_MSG(RSP, "[OCR] 2.7-2.8 volt");
    if (resp & (1 << 16))
        N_MSG(RSP, "[OCR] 2.8-2.9 volt");
    if (resp & (1 << 17))
        N_MSG(RSP, "[OCR] 2.9-3.0 volt");
    if (resp & (1 << 18))
        N_MSG(RSP, "[OCR] 3.0-3.1 volt");
    if (resp & (1 << 19))
        N_MSG(RSP, "[OCR] 3.1-3.2 volt");
    if (resp & (1 << 20))
        N_MSG(RSP, "[OCR] 3.2-3.3 volt");
    if (resp & (1 << 21))
        N_MSG(RSP, "[OCR] 3.3-3.4 volt");
    if (resp & (1 << 22))
        N_MSG(RSP, "[OCR] 3.4-3.5 volt");
    if (resp & (1 << 23))
        N_MSG(RSP, "[OCR] 3.5-3.6 volt");
    if (resp & (1 << 24))
        N_MSG(RSP, "[OCR] Switching to 1.8V Accepted (S18A)");
    if (resp & (1 << 30))
        N_MSG(RSP, "[OCR] Card Capacity Status (CCS)");
    if (resp & (1 << 31))
        N_MSG(RSP, "[OCR] Card Power Up Status (Idle)");
    else
        N_MSG(RSP, "[OCR] Card Power Up Status (Busy)");
}

static void msdc_dump_rca_resp(struct msdc_host *host, u32 resp)
{
    u32 status = (((resp >> 15) & 0x1) << 23) |
                 (((resp >> 14) & 0x1) << 22) |
                 (((resp >> 13) & 0x1) << 19) |
                   (resp & 0x1fff);
    
    N_MSG(RSP, "[RCA] 0x%.4x", resp >> 16);
    msdc_dump_card_status(host, status);	
}

static void msdc_dump_io_resp(struct msdc_host *host, u32 resp)
{
    u32 flags = (resp >> 8) & 0xFF;
    char *state[] = {"DIS", "CMD", "TRN", "RFU"};
    
    if (flags & (1 << 7))
        N_MSG(RSP, "[IO] COM_CRC_ERR");
    if (flags & (1 << 6))
        N_MSG(RSP, "[IO] Illgal command");   
    if (flags & (1 << 3))
        N_MSG(RSP, "[IO] Error");
    if (flags & (1 << 2))
        N_MSG(RSP, "[IO] RFU");
    if (flags & (1 << 1))
        N_MSG(RSP, "[IO] Function number error");
    if (flags & (1 << 0))
        N_MSG(RSP, "[IO] Out of range");

    N_MSG(RSP, "[IO] State: %s, Data:0x%x", state[(resp >> 12) & 0x3], resp & 0xFF);
}
#endif

static void msdc_set_timeout(struct msdc_host *host, u32 ns, u32 clks)
{
    u32 base = host->base;
    u32 timeout, clk_ns;

    host->timeout_ns   = ns;
    host->timeout_clks = clks;

    clk_ns  = 1000000000UL / host->sclk;
    timeout = ns / clk_ns + clks;
    timeout = timeout >> 16; /* in 65536 sclk cycle unit */
    timeout = timeout > 1 ? timeout - 1 : 0;
    timeout = timeout > 255 ? 255 : timeout;

    sdr_set_field(SDC_CFG, SDC_CFG_DTOC, timeout);

    N_MSG(OPS, "Set read data timeout: %dns %dclks -> %d x 65536 cycles",
        ns, clks, timeout + 1);
}

/* msdc_eirq_sdio() will be called when EIRQ(for WIFI) */
static void msdc_eirq_sdio(void *data)
{
    struct msdc_host *host = (struct msdc_host *)data;

    N_MSG(INT, "SDIO EINT");

    mmc_signal_sdio_irq(host->mmc);
}

/* msdc_eirq_cd will not be used!  We not using EINT for card detection. */
static void msdc_eirq_cd(void *data)
{
    struct msdc_host *host = (struct msdc_host *)data;

    N_MSG(INT, "CD EINT");

#if 0
    tasklet_hi_schedule(&host->card_tasklet);
#else
    schedule_delayed_work(&host->card_delaywork, HZ);
#endif
}

#if 0
static void msdc_tasklet_card(unsigned long arg)
{
    struct msdc_host *host = (struct msdc_host *)arg;
#else
static void msdc_tasklet_card(struct work_struct *work)
{
    struct msdc_host *host = (struct msdc_host *)container_of(work, 
		    		struct msdc_host, card_delaywork.work);
#endif
    struct msdc_hw *hw = host->hw;
    u32 base = host->base;
    u32 inserted;	
    u32 status = 0;
    //u32 change = 0;

    spin_lock(&host->lock);

    if (hw->get_cd_status) { // NULL
        inserted = hw->get_cd_status();
    } else {
        status = sdr_read32(MSDC_PS);
        inserted = (status & MSDC_PS_CDSTS) ? 0 : 1;
    }

#if 0
    change = host->card_inserted ^ inserted;
    host->card_inserted = inserted;
	
    if (change && !host->suspend) {
        if (inserted) {
            host->mmc->f_max = HOST_MAX_MCLK;  // work around          	
        }     	
        mmc_detect_change(host->mmc, msecs_to_jiffies(20));
    }
#else  /* Make sure: handle the last interrupt */
    host->card_inserted = inserted;    
    
    if (!host->suspend) {
        host->mmc->f_max = HOST_MAX_MCLK;    	
        mmc_detect_change(host->mmc, msecs_to_jiffies(20));
    }   
    
    IRQ_MSG("card found<%s>", inserted ? "inserted" : "removed");  	
#endif

    spin_unlock(&host->lock);
}

#if 0 /* --- by chhung */
/* For E2 only */
static u8 clk_src_bit[4] = {
   0, 3, 5, 7    	
};

static void msdc_select_clksrc(struct msdc_host* host, unsigned char clksrc)
{
    u32 val; 
    u32 base = host->base;
        
    BUG_ON(clksrc > 3);	
    INIT_MSG("set clock source to <%d>", clksrc);    	

    val = sdr_read32(MSDC_CLKSRC_REG);      
    if (sdr_read32(MSDC_ECO_VER) >= 4) {
        val &= ~(0x3  << clk_src_bit[host->id]); 
        val |= clksrc << clk_src_bit[host->id];                   	
    } else {        
        val &= ~0x3; val |= clksrc;
    }    
    sdr_write32(MSDC_CLKSRC_REG, val);
            
    host->hclk = hclks[clksrc];     
    host->hw->clk_src = clksrc;
}
#endif /* end of --- */

static void msdc_set_mclk(struct msdc_host *host, int ddr, unsigned int hz)
{
    //struct msdc_hw *hw = host->hw;
    u32 base = host->base;
    u32 mode;
    u32 flags;
    u32 div;
    u32 sclk;
    u32 hclk = host->hclk;
    //u8  clksrc = hw->clk_src;

    if (!hz) { // set mmc system clock to 0 ?
        ERR_MSG("set mclk to 0!!!");
        msdc_reset();
        return;
    }

    msdc_irq_save(flags);
    
#if defined (CONFIG_MT7621_FPGA) || defined (CONFIG_MT7628_FPGA)
    mode = 0x0; /* use divisor */
    if (hz >= (hclk >> 1)) {
	    div  = 0;         /* mean div = 1/2 */
	    sclk = hclk >> 1; /* sclk = clk / 2 */
    } else {
	    div  = (hclk + ((hz << 2) - 1)) / (hz << 2);
	    sclk = (hclk >> 2) / div;
    }
#else
    if (ddr) {
        mode = 0x2; /* ddr mode and use divisor */
        if (hz >= (hclk >> 2)) {
        	div  = 1;         /* mean div = 1/4 */
        	sclk = hclk >> 2; /* sclk = clk / 4 */
        } else {
        	div  = (hclk + ((hz << 2) - 1)) / (hz << 2);
        	sclk = (hclk >> 2) / div;
        }
    } else if (hz >= hclk) { /* bug fix */
        mode = 0x1; /* no divisor and divisor is ignored */
        div  = 0;
        sclk = hclk; 
    } else {
        mode = 0x0; /* use divisor */
        if (hz >= (hclk >> 1)) {
        	div  = 0;         /* mean div = 1/2 */
        	sclk = hclk >> 1; /* sclk = clk / 2 */
        } else {
        	div  = (hclk + ((hz << 2) - 1)) / (hz << 2);
        	sclk = (hclk >> 2) / div;
        }
    }    
#endif
    /* set clock mode and divisor */
    sdr_set_field(MSDC_CFG, MSDC_CFG_CKMOD, mode);
    sdr_set_field(MSDC_CFG, MSDC_CFG_CKDIV, div);
   
    /* wait clock stable */
    while (!(sdr_read32(MSDC_CFG) & MSDC_CFG_CKSTB));

    host->sclk = sclk;
    host->mclk = hz;
    msdc_set_timeout(host, host->timeout_ns, host->timeout_clks); // need?
     
    INIT_MSG("================");  
    INIT_MSG("!!! Set<%dKHz> Source<%dKHz> -> sclk<%dKHz>", hz/1000, hclk/1000, sclk/1000); 
    INIT_MSG("================");

    msdc_irq_restore(flags);
}

/* Fix me. when need to abort */
static void msdc_abort_data(struct msdc_host *host)
{
	  u32 base = host->base;
	  struct mmc_command *stop = host->mrq->stop;

    ERR_MSG("Need to Abort. dma<%d>", host->dma_xfer);
    
    msdc_reset();
    msdc_clr_fifo();        
    msdc_clr_int();

    // need to check FIFO count 0 ?
    
    if (stop) {  /* try to stop, but may not success */
        ERR_MSG("stop when abort CMD<%d>", stop->opcode);     	
        (void)msdc_do_command(host, stop, 0, CMD_TIMEOUT);
    }
    
    //if (host->mclk >= 25000000) {
    //      msdc_set_mclk(host, 0, host->mclk >> 1);
    //}
}

#if 0 /* --- by chhung */
static void msdc_pin_config(struct msdc_host *host, int mode)
{
    struct msdc_hw *hw = host->hw;
    u32 base = host->base;
    int pull = (mode == MSDC_PIN_PULL_UP) ? GPIO_PULL_UP : GPIO_PULL_DOWN;

    /* Config WP pin */
    if (hw->flags & MSDC_WP_PIN_EN) {
        if (hw->config_gpio_pin) /* NULL */
            hw->config_gpio_pin(MSDC_WP_PIN, pull);
    }

    switch (mode) {
    case MSDC_PIN_PULL_UP:
        //sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKPU, 1); /* Check & FIXME */
        //sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKPD, 0); /* Check & FIXME */
        sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDPU, 1);
        sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDPD, 0);
        sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATPU, 1);
        sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATPD, 0);
        break;
    case MSDC_PIN_PULL_DOWN:
        //sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKPU, 0); /* Check & FIXME */
        //sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKPD, 1); /* Check & FIXME */
        sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDPU, 0);
        sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDPD, 1);
        sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATPU, 0);
        sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATPD, 1);
        break;
    case MSDC_PIN_PULL_NONE:
    default:
        //sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKPU, 0); /* Check & FIXME */
        //sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKPD, 0); /* Check & FIXME */
        sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDPU, 0);
        sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDPD, 0);
        sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATPU, 0);
        sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATPD, 0);
        break;
    }
    
    N_MSG(CFG, "Pins mode(%d), down(%d), up(%d)", 
        mode, MSDC_PIN_PULL_DOWN, MSDC_PIN_PULL_UP);
}

void msdc_pin_reset(struct msdc_host *host, int mode)
{
    struct msdc_hw *hw = (struct msdc_hw *)host->hw;
    u32 base = host->base;
    int pull = (mode == MSDC_PIN_PULL_UP) ? GPIO_PULL_UP : GPIO_PULL_DOWN;

    /* Config reset pin */
    if (hw->flags & MSDC_RST_PIN_EN) {
        if (hw->config_gpio_pin) /* NULL */
            hw->config_gpio_pin(MSDC_RST_PIN, pull);

        if (mode == MSDC_PIN_PULL_UP) {
            sdr_clr_bits(EMMC_IOCON, EMMC_IOCON_BOOTRST);
        } else {
            sdr_set_bits(EMMC_IOCON, EMMC_IOCON_BOOTRST);
        }
    }
}

static void msdc_core_power(struct msdc_host *host, int on)
{
    N_MSG(CFG, "Turn %s %s power (copower: %d -> %d)", 
        on ? "on" : "off", "core", host->core_power, on);

    if (on && host->core_power == 0) {
        msdc_vcore_on(host);
        host->core_power = 1;
        msleep(1);
    } else if (!on && host->core_power == 1) {
        msdc_vcore_off(host);
        host->core_power = 0;
        msleep(1);
    }
}

static void msdc_host_power(struct msdc_host *host, int on)
{
    N_MSG(CFG, "Turn %s %s power ", on ? "on" : "off", "host");

    if (on) {
        //msdc_core_power(host, 1); // need do card detection. 
        msdc_pin_reset(host, MSDC_PIN_PULL_UP);
    } else {
        msdc_pin_reset(host, MSDC_PIN_PULL_DOWN);
        //msdc_core_power(host, 0);
    }
}

static void msdc_card_power(struct msdc_host *host, int on)
{
    N_MSG(CFG, "Turn %s %s power ", on ? "on" : "off", "card");

    if (on) {
        msdc_pin_config(host, MSDC_PIN_PULL_UP);    
        if (host->hw->ext_power_on) {
            host->hw->ext_power_on();
        } else {
            //msdc_vdd_on(host);  // need todo card detection.
        }
        msleep(1);
    } else {
        if (host->hw->ext_power_off) {
            host->hw->ext_power_off();
        } else {
            //msdc_vdd_off(host);
        }
        msdc_pin_config(host, MSDC_PIN_PULL_DOWN);
        msleep(1);
    }
}

static void msdc_set_power_mode(struct msdc_host *host, u8 mode)
{
    N_MSG(CFG, "Set power mode(%d)", mode);

    if (host->power_mode == MMC_POWER_OFF && mode != MMC_POWER_OFF) {
        msdc_host_power(host, 1);
        msdc_card_power(host, 1);
    } else if (host->power_mode != MMC_POWER_OFF && mode == MMC_POWER_OFF) {
        msdc_card_power(host, 0);
        msdc_host_power(host, 0);
    }
    host->power_mode = mode;
}
#endif /* end of --- */

#ifdef CONFIG_PM
/*
   register as callback function of WIFI(combo_sdio_register_pm) .    
   can called by msdc_drv_suspend/resume too. 
*/
static void msdc_pm(pm_message_t state, void *data)
{
    struct msdc_host *host = (struct msdc_host *)data;
    int evt = state.event;

    if (evt == PM_EVENT_USER_RESUME || evt == PM_EVENT_USER_SUSPEND) {
        INIT_MSG("USR_%s: suspend<%d> power<%d>", 
                   evt == PM_EVENT_USER_RESUME ? "EVENT_USER_RESUME" : "EVENT_USER_SUSPEND", 
                   host->suspend, host->power_mode);    	
    }

    if (evt == PM_EVENT_SUSPEND || evt == PM_EVENT_USER_SUSPEND) {
        if (host->suspend) /* already suspend */  /* default 0*/
            return;

        /* for memory card. already power off by mmc */
        if (evt == PM_EVENT_SUSPEND && host->power_mode == MMC_POWER_OFF)  
            return;

        host->suspend = 1;
        host->pm_state = state;  /* default PMSG_RESUME */
        
        INIT_MSG("%s Suspend", evt == PM_EVENT_SUSPEND ? "PM" : "USR");                  	
        if(host->hw->flags & MSDC_SYS_SUSPEND) /* set for card */
            (void)mmc_suspend_host(host->mmc);
        else { 
            // host->mmc->pm_flags |= MMC_PM_IGNORE_PM_NOTIFY;  /* just for double confirm */ /* --- by chhung */
            mmc_remove_host(host->mmc);
        }
    } else if (evt == PM_EVENT_RESUME || evt == PM_EVENT_USER_RESUME) {
        if (!host->suspend){
            //ERR_MSG("warning: already resume");   	
            return;
        }

        /* No PM resume when USR suspend */
        if (evt == PM_EVENT_RESUME && host->pm_state.event == PM_EVENT_USER_SUSPEND) {
            ERR_MSG("PM Resume when in USR Suspend");   	/* won't happen. */
            return;
        }
        
        host->suspend = 0;
        host->pm_state = state;
        
        INIT_MSG("%s Resume", evt == PM_EVENT_RESUME ? "PM" : "USR");                
        if(host->hw->flags & MSDC_SYS_SUSPEND) { /* will not set for WIFI */
            (void)mmc_resume_host(host->mmc);
        }
        else { 
            // host->mmc->pm_flags |= MMC_PM_IGNORE_PM_NOTIFY; /* --- by chhung */
            mmc_add_host(host->mmc);
        }
    }
}
#endif

/*--------------------------------------------------------------------------*/
/* mmc_host_ops members                                                      */
/*--------------------------------------------------------------------------*/
static unsigned int msdc_command_start(struct msdc_host   *host, 
                                      struct mmc_command *cmd,
                                      int                 tune,   /* not used */
                                      unsigned long       timeout)
{
    u32 base = host->base;
    u32 opcode = cmd->opcode;
    u32 rawcmd;
    u32 wints = MSDC_INT_CMDRDY  | MSDC_INT_RSPCRCERR  | MSDC_INT_CMDTMO  |  
                MSDC_INT_ACMDRDY | MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO | 
                MSDC_INT_ACMD19_DONE;  
                   
    u32 resp;  
    unsigned long tmo;

    /* Protocol layer does not provide response type, but our hardware needs 
     * to know exact type, not just size!
     */
    if (opcode == MMC_SEND_OP_COND || opcode == SD_APP_OP_COND)
        resp = RESP_R3;
    else if (opcode == MMC_SET_RELATIVE_ADDR || opcode == SD_SEND_RELATIVE_ADDR)
        resp = (mmc_cmd_type(cmd) == MMC_CMD_BCR) ? RESP_R6 : RESP_R1;
    else if (opcode == MMC_FAST_IO)
        resp = RESP_R4;
    else if (opcode == MMC_GO_IRQ_STATE)
        resp = RESP_R5;
    else if (opcode == MMC_SELECT_CARD)
        resp = (cmd->arg != 0) ? RESP_R1B : RESP_NONE;
    else if (opcode == SD_IO_RW_DIRECT || opcode == SD_IO_RW_EXTENDED)
        resp = RESP_R1; /* SDIO workaround. */
    else if (opcode == SD_SEND_IF_COND && (mmc_cmd_type(cmd) == MMC_CMD_BCR))
        resp = RESP_R1;
    else {
        switch (mmc_resp_type(cmd)) {
        case MMC_RSP_R1:
            resp = RESP_R1;
            break;
        case MMC_RSP_R1B:
            resp = RESP_R1B;
            break;
        case MMC_RSP_R2:
            resp = RESP_R2;
            break;
        case MMC_RSP_R3:
            resp = RESP_R3;
            break;
        case MMC_RSP_NONE:
        default:
            resp = RESP_NONE;              
            break;
        }
    }

    cmd->error = 0;
    /* rawcmd :
     * vol_swt << 30 | auto_cmd << 28 | blklen << 16 | go_irq << 15 | 
     * stop << 14 | rw << 13 | dtype << 11 | rsptyp << 7 | brk << 6 | opcode
     */    
    rawcmd = opcode | msdc_rsp[resp] << 7 | host->blksz << 16;
    
    if (opcode == MMC_READ_MULTIPLE_BLOCK) {
        rawcmd |= (2 << 11);
    } else if (opcode == MMC_READ_SINGLE_BLOCK) {
        rawcmd |= (1 << 11);
    } else if (opcode == MMC_WRITE_MULTIPLE_BLOCK) {
        rawcmd |= ((2 << 11) | (1 << 13));
    } else if (opcode == MMC_WRITE_BLOCK) {
        rawcmd |= ((1 << 11) | (1 << 13));
    } else if (opcode == SD_IO_RW_EXTENDED) {
        if (cmd->data->flags & MMC_DATA_WRITE)
            rawcmd |= (1 << 13);
        if (cmd->data->blocks > 1)
            rawcmd |= (2 << 11);
        else
            rawcmd |= (1 << 11);
    } else if (opcode == SD_IO_RW_DIRECT && cmd->flags == (unsigned int)-1) {
        rawcmd |= (1 << 14);
    } else if ((opcode == SD_APP_SEND_SCR) || 
        (opcode == SD_APP_SEND_NUM_WR_BLKS) ||
        (opcode == SD_SWITCH && (mmc_cmd_type(cmd) == MMC_CMD_ADTC)) ||
        (opcode == SD_APP_SD_STATUS && (mmc_cmd_type(cmd) == MMC_CMD_ADTC)) ||
        (opcode == MMC_SEND_EXT_CSD && (mmc_cmd_type(cmd) == MMC_CMD_ADTC))) {
        rawcmd |= (1 << 11);
    } else if (opcode == MMC_STOP_TRANSMISSION) {
        rawcmd |= (1 << 14);
        rawcmd &= ~(0x0FFF << 16);
    }

    N_MSG(CMD, "CMD<%d><0x%.8x> Arg<0x%.8x>", opcode , rawcmd, cmd->arg);

    tmo = jiffies + timeout;

    if (opcode == MMC_SEND_STATUS) {
        for (;;) {
            if (!sdc_is_cmd_busy())
                break;
                
            if (time_after(jiffies, tmo)) {
                ERR_MSG("XXX cmd_busy timeout: before CMD<%d>", opcode);	
                cmd->error = (unsigned int)-ETIMEDOUT;
                msdc_reset();
                goto end;
            } 
        }
    }else {
        for (;;) {	 
            if (!sdc_is_busy())
                break;
            if (time_after(jiffies, tmo)) {
                ERR_MSG("XXX sdc_busy timeout: before CMD<%d>", opcode);	
                cmd->error = (unsigned int)-ETIMEDOUT;
                msdc_reset();
                goto end;      
            }   
        }    
    }   
    
    //BUG_ON(in_interrupt());
    host->cmd     = cmd;
    host->cmd_rsp = resp;		
    
    init_completion(&host->cmd_done);     

    sdr_set_bits(MSDC_INTEN, wints);          
    sdc_send_cmd(rawcmd, cmd->arg);        
      
end:    	
    return cmd->error;
}

static unsigned int msdc_command_resp(struct msdc_host   *host, 
                                      struct mmc_command *cmd,
                                      int                 tune,
                                      unsigned long       timeout)
{
    u32 base = host->base;
    u32 opcode = cmd->opcode;
    //u32 rawcmd;
    u32 resp;
    u32 wints = MSDC_INT_CMDRDY  | MSDC_INT_RSPCRCERR  | MSDC_INT_CMDTMO  |  
                MSDC_INT_ACMDRDY | MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO | 
                MSDC_INT_ACMD19_DONE;     
    
    resp = host->cmd_rsp;

    BUG_ON(in_interrupt());
    //init_completion(&host->cmd_done);
    //sdr_set_bits(MSDC_INTEN, wints);
        
    spin_unlock(&host->lock);   
    if(!wait_for_completion_timeout(&host->cmd_done, 10*timeout)){       
        ERR_MSG("XXX CMD<%d> wait_for_completion timeout ARG<0x%.8x>", opcode, cmd->arg);
        cmd->error = (unsigned int)-ETIMEDOUT;
        msdc_reset();
    }    
    spin_lock(&host->lock);

    sdr_clr_bits(MSDC_INTEN, wints);
    host->cmd = NULL;

//end:
#ifdef MT6575_SD_DEBUG
    switch (resp) {
    case RESP_NONE:
        N_MSG(RSP, "CMD_RSP(%d): %d RSP(%d)", opcode, cmd->error, resp);
        break;
    case RESP_R2:
        N_MSG(RSP, "CMD_RSP(%d): %d RSP(%d)= %.8x %.8x %.8x %.8x", 
            opcode, cmd->error, resp, cmd->resp[0], cmd->resp[1], 
            cmd->resp[2], cmd->resp[3]);          
        break;
    default: /* Response types 1, 3, 4, 5, 6, 7(1b) */
        N_MSG(RSP, "CMD_RSP(%d): %d RSP(%d)= 0x%.8x", 
            opcode, cmd->error, resp, cmd->resp[0]);
        if (cmd->error == 0) {
            switch (resp) {
            case RESP_R1:
            case RESP_R1B:
                msdc_dump_card_status(host, cmd->resp[0]);
                break;
            case RESP_R3:
                msdc_dump_ocr_reg(host, cmd->resp[0]);
                break;
            case RESP_R5:
                msdc_dump_io_resp(host, cmd->resp[0]);
                break;
            case RESP_R6:
                msdc_dump_rca_resp(host, cmd->resp[0]);
                break;
            }
        }
        break;
    }
#endif

    /* do we need to save card's RCA when SD_SEND_RELATIVE_ADDR */   

    if (!tune) {
        return cmd->error;    	
    }

    /* memory card CRC */     
    if(host->hw->flags & MSDC_REMOVABLE && cmd->error == (unsigned int)(-EIO) ) {          
        if (sdr_read32(SDC_CMD) & 0x1800) { /* check if has data phase */ 
            msdc_abort_data(host);
        } else {
            /* do basic: reset*/	
            msdc_reset();
            msdc_clr_fifo();        
            msdc_clr_int();        	
        } 
        cmd->error = msdc_tune_cmdrsp(host,cmd); 
    }

    //  check DAT0 
    /* if (resp == RESP_R1B) {
        while ((sdr_read32(MSDC_PS) & 0x10000) != 0x10000);       
    } */ 
    /* CMD12 Error Handle */
                	
    return cmd->error;
}                                   

static unsigned int msdc_do_command(struct msdc_host   *host, 
                                      struct mmc_command *cmd,
                                      int                 tune,
                                      unsigned long       timeout)
{
    if (msdc_command_start(host, cmd, tune, timeout)) 
        goto end;      

    if (msdc_command_resp(host, cmd, tune, timeout)) 
        goto end;          
           	    
end:	

    N_MSG(CMD, "        return<%d> resp<0x%.8x>", cmd->error, cmd->resp[0]); 	
    return cmd->error;
}
    
/* The abort condition when PIO read/write 
   tmo: 
*/
static int msdc_pio_abort(struct msdc_host *host, struct mmc_data *data, unsigned long tmo)
{
    int  ret = 0; 	
    u32  base = host->base;
    
    if (atomic_read(&host->abort)) {	
        ret = 1;
    }    

    if (time_after(jiffies, tmo)) {
        data->error = (unsigned int)-ETIMEDOUT;
        ERR_MSG("XXX PIO Data Timeout: CMD<%d>", host->mrq->cmd->opcode);
        ret = 1;		
    }      
    
    if(ret) {
        msdc_reset();
        msdc_clr_fifo();        
        msdc_clr_int();     	
        ERR_MSG("msdc pio find abort");      
    }
    return ret; 
}

/*
   Need to add a timeout, or WDT timeout, system reboot.      
*/
// pio mode data read/write
static int msdc_pio_read(struct msdc_host *host, struct mmc_data *data)
{
    struct scatterlist *sg = data->sg;
    u32  base = host->base;
    u32  num = data->sg_len;
    u32 *ptr;
    u8  *u8ptr;
    u32  left = 0;
    u32  count, size = 0;
    u32  wints = MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR ;     
    unsigned long tmo = jiffies + DAT_TIMEOUT;  
          
    sdr_set_bits(MSDC_INTEN, wints);
    while (num) {
        left = sg_dma_len(sg);
        ptr = sg_virt(sg);
        while (left) {
            if ((left >=  MSDC_FIFO_THD) && (msdc_rxfifocnt() >= MSDC_FIFO_THD)) {
                count = MSDC_FIFO_THD >> 2;
                do {
                    *ptr++ = msdc_fifo_read32();
                } while (--count);
                left -= MSDC_FIFO_THD;
            } else if ((left < MSDC_FIFO_THD) && msdc_rxfifocnt() >= left) {
                while (left > 3) {
                    *ptr++ = msdc_fifo_read32();
                    left -= 4;
                }
                 
                u8ptr = (u8 *)ptr; 
                while(left) {
                    * u8ptr++ = msdc_fifo_read8();
                    left--; 	  
                }
            }
            
            if (msdc_pio_abort(host, data, tmo)) {
                goto end; 	
            }
        }
        size += sg_dma_len(sg);
        sg = sg_next(sg); num--;
    }
end:
    data->bytes_xfered += size;
    N_MSG(FIO, "        PIO Read<%d>bytes", size);
        
    sdr_clr_bits(MSDC_INTEN, wints);    
    if(data->error) ERR_MSG("read pio data->error<%d> left<%d> size<%d>", data->error, left, size);
    return data->error;
}

/* please make sure won't using PIO when size >= 512 
   which means, memory card block read/write won't using pio
   then don't need to handle the CMD12 when data error. 
*/
static int msdc_pio_write(struct msdc_host* host, struct mmc_data *data)
{
    u32  base = host->base;
    struct scatterlist *sg = data->sg;
    u32  num = data->sg_len;
    u32 *ptr;
    u8  *u8ptr;
    u32  left;
    u32  count, size = 0;
    u32  wints = MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR ;      
    unsigned long tmo = jiffies + DAT_TIMEOUT;  
    
    sdr_set_bits(MSDC_INTEN, wints);    
    while (num) {
        left = sg_dma_len(sg);
        ptr = sg_virt(sg);

        while (left) {
            if (left >= MSDC_FIFO_SZ && msdc_txfifocnt() == 0) {
                count = MSDC_FIFO_SZ >> 2;
                do {
                    msdc_fifo_write32(*ptr); ptr++;
                } while (--count);
                left -= MSDC_FIFO_SZ;
            } else if (left < MSDC_FIFO_SZ && msdc_txfifocnt() == 0) {
                while (left > 3) {
                    msdc_fifo_write32(*ptr); ptr++;
                    left -= 4;
                } 
                
                u8ptr = (u8*)ptr; 
                while(left){
                    msdc_fifo_write8(*u8ptr);	u8ptr++;
                    left--;
                }
            }
            
            if (msdc_pio_abort(host, data, tmo)) {
                goto end; 	
            }                   
        }
        size += sg_dma_len(sg);
        sg = sg_next(sg); num--;
    }
end:    
    data->bytes_xfered += size;
    N_MSG(FIO, "        PIO Write<%d>bytes", size);
    if(data->error) ERR_MSG("write pio data->error<%d>", data->error);
    	
    sdr_clr_bits(MSDC_INTEN, wints);  
    return data->error;	
}

#if 0 /* --- by chhung */
// DMA resume / start / stop 
static void msdc_dma_resume(struct msdc_host *host)
{
    u32 base = host->base;

    sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_RESUME, 1);

    N_MSG(DMA, "DMA resume");
}
#endif /* end of --- */

static void msdc_dma_start(struct msdc_host *host)
{
    u32 base = host->base;
    u32 wints = MSDC_INTEN_XFER_COMPL | MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR ; 
           
    sdr_set_bits(MSDC_INTEN, wints);
    //dsb(); /* --- by chhung */
    sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_START, 1);

    N_MSG(DMA, "DMA start");
}

static void msdc_dma_stop(struct msdc_host *host)
{
    u32 base = host->base;
    //u32 retries=500;
    u32 wints = MSDC_INTEN_XFER_COMPL | MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR ; 
    
    N_MSG(DMA, "DMA status: 0x%.8x",sdr_read32(MSDC_DMA_CFG));
    //while (sdr_read32(MSDC_DMA_CFG) & MSDC_DMA_CFG_STS);

    sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_STOP, 1);
    while (sdr_read32(MSDC_DMA_CFG) & MSDC_DMA_CFG_STS);

    //dsb(); /* --- by chhung */
    sdr_clr_bits(MSDC_INTEN, wints); /* Not just xfer_comp */

    N_MSG(DMA, "DMA stop");
}

#if 0 /* --- by chhung */
/* dump a gpd list */
static void msdc_dma_dump(struct msdc_host *host, struct msdc_dma *dma)
{
    gpd_t *gpd = dma->gpd; 
    bd_t   *bd = dma->bd; 	 	
    bd_t   *ptr; 
    int i = 0; 
    int p_to_v; 
    
    if (dma->mode != MSDC_MODE_DMA_DESC) {
        return; 	
    }    

    ERR_MSG("try to dump gpd and bd");

    /* dump gpd */
    ERR_MSG(".gpd<0x%.8x> gpd_phy<0x%.8x>", (int)gpd, (int)dma->gpd_addr);
    ERR_MSG("...hwo   <%d>", gpd->hwo );
    ERR_MSG("...bdp   <%d>", gpd->bdp );
    ERR_MSG("...chksum<0x%.8x>", gpd->chksum );
    //ERR_MSG("...intr  <0x%.8x>", gpd->intr );
    ERR_MSG("...next  <0x%.8x>", (int)gpd->next );
    ERR_MSG("...ptr   <0x%.8x>", (int)gpd->ptr );
    ERR_MSG("...buflen<0x%.8x>", gpd->buflen );
    //ERR_MSG("...extlen<0x%.8x>", gpd->extlen );
    //ERR_MSG("...arg   <0x%.8x>", gpd->arg );
    //ERR_MSG("...blknum<0x%.8x>", gpd->blknum );    
    //ERR_MSG("...cmd   <0x%.8x>", gpd->cmd );      

    /* dump bd */
    ERR_MSG(".bd<0x%.8x> bd_phy<0x%.8x> gpd_ptr<0x%.8x>", (int)bd, (int)dma->bd_addr, (int)gpd->ptr);  
    ptr = bd; 
    p_to_v = ((u32)bd - (u32)dma->bd_addr);
    while (1) {
        ERR_MSG(".bd[%d]", i); i++;          	
        ERR_MSG("...eol   <%d>", ptr->eol );
        ERR_MSG("...chksum<0x%.8x>", ptr->chksum );
        //ERR_MSG("...blkpad<0x%.8x>", ptr->blkpad );
        //ERR_MSG("...dwpad <0x%.8x>", ptr->dwpad );
        ERR_MSG("...next  <0x%.8x>", (int)ptr->next );
        ERR_MSG("...ptr   <0x%.8x>", (int)ptr->ptr );
        ERR_MSG("...buflen<0x%.8x>", (int)ptr->buflen );
        
        if (ptr->eol == 1) {
            break; 	
        }
        	             
        /* find the next bd, virtual address of ptr->next */
        /* don't need to enable when use malloc */
        //BUG_ON( (ptr->next + p_to_v)!=(ptr+1) );     	
        //ERR_MSG(".next bd<0x%.8x><0x%.8x>", (ptr->next + p_to_v), (ptr+1));
        ptr++;               
    }    
    
    ERR_MSG("dump gpd and bd finished");
}
#endif /* end of --- */

/* calc checksum */
static u8 msdc_dma_calcs(u8 *buf, u32 len)
{
    u32 i, sum = 0;
    for (i = 0; i < len; i++) {
        sum += buf[i];
    }
    return 0xFF - (u8)sum;
}

/* gpd bd setup + dma registers */
static int msdc_dma_config(struct msdc_host *host, struct msdc_dma *dma)
{
    u32 base = host->base;
    u32 sglen = dma->sglen;
    //u32 i, j, num, bdlen, arg, xfersz;
    u32 j, num, bdlen;
    u8  blkpad, dwpad, chksum;
    struct scatterlist *sg = dma->sg;
    gpd_t *gpd;
    bd_t *bd;

    switch (dma->mode) {
    case MSDC_MODE_DMA_BASIC:
        BUG_ON(dma->xfersz > 65535);
        BUG_ON(dma->sglen != 1);
        sdr_write32(MSDC_DMA_SA, PHYSADDR(sg_dma_address(sg)));
        sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_LASTBUF, 1);
#if defined (CONFIG_RALINK_MT7620)
        sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_XFERSZ, sg_dma_len(sg));
#elif defined (CONFIG_RALINK_MT7621) || defined (CONFIG_RALINK_MT7628)
        sdr_write32((volatile u32*)(RALINK_MSDC_BASE+0xa8), sg_dma_len(sg));
#endif
        sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_BRUSTSZ, dma->burstsz);
        sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_MODE, 0);
        break;
    case MSDC_MODE_DMA_DESC:
        blkpad = (dma->flags & DMA_FLAG_PAD_BLOCK) ? 1 : 0;
        dwpad  = (dma->flags & DMA_FLAG_PAD_DWORD) ? 1 : 0;
        chksum = (dma->flags & DMA_FLAG_EN_CHKSUM) ? 1 : 0;

        /* calculate the required number of gpd */
        num = (sglen + MAX_BD_PER_GPD - 1) / MAX_BD_PER_GPD;        
        BUG_ON(num !=1 );        
        
        gpd = dma->gpd; 
        bd  = dma->bd; 
        bdlen = sglen; 

        /* modify gpd*/
        //gpd->intr = 0; 
        gpd->hwo = 1;  /* hw will clear it */
        gpd->bdp = 1;     
        gpd->chksum = 0;  /* need to clear first. */   
        gpd->chksum = (chksum ? msdc_dma_calcs((u8 *)gpd, 16) : 0);
        
        /* modify bd*/          
        for (j = 0; j < bdlen; j++) {
            msdc_init_bd(&bd[j], blkpad, dwpad, sg_dma_address(sg), sg_dma_len(sg));            
            if(j == bdlen - 1) {
            bd[j].eol = 1;     	/* the last bd */
            } else {
                bd[j].eol = 0; 	
            }
            bd[j].chksum = 0; /* checksume need to clear first */
            bd[j].chksum = (chksum ? msdc_dma_calcs((u8 *)(&bd[j]), 16) : 0);         
            sg++;
        }
                
        dma->used_gpd += 2;
        dma->used_bd += bdlen;  

        sdr_set_field(MSDC_DMA_CFG, MSDC_DMA_CFG_DECSEN, chksum);
        sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_BRUSTSZ, dma->burstsz);
        sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_MODE, 1);

        sdr_write32(MSDC_DMA_SA, PHYSADDR((u32)dma->gpd_addr));               
        break;

    default:
        break;
    }
    
    N_MSG(DMA, "DMA_CTRL = 0x%x", sdr_read32(MSDC_DMA_CTRL));
    N_MSG(DMA, "DMA_CFG  = 0x%x", sdr_read32(MSDC_DMA_CFG));
    N_MSG(DMA, "DMA_SA   = 0x%x", sdr_read32(MSDC_DMA_SA));

    return 0;
} 

static void msdc_dma_setup(struct msdc_host *host, struct msdc_dma *dma, 
    struct scatterlist *sg, unsigned int sglen)
{ 
    BUG_ON(sglen > MAX_BD_NUM); /* not support currently */

    dma->sg = sg;
    dma->flags = DMA_FLAG_EN_CHKSUM;
    //dma->flags = DMA_FLAG_NONE; /* CHECKME */
    dma->sglen = sglen;
    dma->xfersz = host->xfer_size;
    dma->burstsz = MSDC_BRUST_64B;
    
    if (sglen == 1 && sg_dma_len(sg) <= MAX_DMA_CNT)
        dma->mode = MSDC_MODE_DMA_BASIC;
    else
        dma->mode = MSDC_MODE_DMA_DESC;

    N_MSG(DMA, "DMA mode<%d> sglen<%d> xfersz<%d>", dma->mode, dma->sglen, dma->xfersz);

    msdc_dma_config(host, dma);
    
    /*if (dma->mode == MSDC_MODE_DMA_DESC) {
        //msdc_dma_dump(host, dma);
    } */
}

/* set block number before send command */
static void msdc_set_blknum(struct msdc_host *host, u32 blknum)
{
    u32 base = host->base;

    sdr_write32(SDC_BLK_NUM, blknum);
}

static int msdc_do_request(struct mmc_host*mmc, struct mmc_request*mrq)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct mmc_command *cmd;
    struct mmc_data *data;
    u32 base = host->base;
    //u32 intsts = 0;     
	  unsigned int left=0;
    int dma = 0, read = 1, dir = DMA_FROM_DEVICE, send_type=0;
    
    #define SND_DAT 0
    #define SND_CMD 1

    BUG_ON(mmc == NULL);
    BUG_ON(mrq == NULL);    

    host->error = 0;
    atomic_set(&host->abort, 0);
    
    cmd  = mrq->cmd;
    data = mrq->cmd->data;
   
#if 0 /* --- by chhung */
    //if(host->id ==1){
    N_MSG(OPS, "enable clock!");
    msdc_ungate_clock(host->id);       
		//}
#endif /* end of --- */
		
    if (!data) {
        send_type=SND_CMD;	
        if (msdc_do_command(host, cmd, 1, CMD_TIMEOUT) != 0) {
            goto done;         
        }
    } else {
        BUG_ON(data->blksz > HOST_MAX_BLKSZ);
        send_type=SND_DAT;

        data->error = 0;
        read = data->flags & MMC_DATA_READ ? 1 : 0;
        host->data = data;
        host->xfer_size = data->blocks * data->blksz;
        host->blksz = data->blksz;

        /* deside the transfer mode */
        if (drv_mode[host->id] == MODE_PIO) {
            host->dma_xfer = dma = 0;
        } else if (drv_mode[host->id] == MODE_DMA) {
            host->dma_xfer = dma = 1;        	
        } else if (drv_mode[host->id] == MODE_SIZE_DEP) {
            host->dma_xfer = dma = ((host->xfer_size >= dma_size[host->id]) ? 1 : 0);	
        }      

        if (read) {
            if ((host->timeout_ns != data->timeout_ns) ||
                (host->timeout_clks != data->timeout_clks)) {
                msdc_set_timeout(host, data->timeout_ns, data->timeout_clks);
            }
        }
        
        msdc_set_blknum(host, data->blocks);
        //msdc_clr_fifo();  /* no need */

        if (dma) {
            msdc_dma_on();  /* enable DMA mode first!! */
            init_completion(&host->xfer_done);
            
            /* start the command first*/        	
            if (msdc_command_start(host, cmd, 1, CMD_TIMEOUT) != 0)
                goto done;            

            dir = read ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
            (void)dma_map_sg(mmc_dev(mmc), data->sg, data->sg_len, dir);
            msdc_dma_setup(host, &host->dma, data->sg, data->sg_len);            
                        
            /* then wait command done */
            if (msdc_command_resp(host, cmd, 1, CMD_TIMEOUT) != 0)
                goto done;            

            /* for read, the data coming too fast, then CRC error 
               start DMA no business with CRC. */
            //init_completion(&host->xfer_done);           
            msdc_dma_start(host);
                       
            spin_unlock(&host->lock);
            if(!wait_for_completion_timeout(&host->xfer_done, DAT_TIMEOUT)){
                ERR_MSG("XXX CMD<%d> wait xfer_done<%d> timeout!!", cmd->opcode, data->blocks * data->blksz);
                ERR_MSG("    DMA_SA   = 0x%x", sdr_read32(MSDC_DMA_SA));
                ERR_MSG("    DMA_CA   = 0x%x", sdr_read32(MSDC_DMA_CA));	 
                ERR_MSG("    DMA_CTRL = 0x%x", sdr_read32(MSDC_DMA_CTRL));
                ERR_MSG("    DMA_CFG  = 0x%x", sdr_read32(MSDC_DMA_CFG));           
                data->error = (unsigned int)-ETIMEDOUT;
                
                msdc_reset();
                msdc_clr_fifo();        
                msdc_clr_int(); 
            }
            spin_lock(&host->lock);
            msdc_dma_stop(host);             
        } else {
            /* Firstly: send command */
            if (msdc_do_command(host, cmd, 1, CMD_TIMEOUT) != 0) {
                goto done;
            }
                                             
            /* Secondly: pio data phase */           
            if (read) {
                if (msdc_pio_read(host, data)){
                    goto done; 	
                }
            } else {
                if (msdc_pio_write(host, data)) {
                    goto done; 		
                }
            }

            /* For write case: make sure contents in fifo flushed to device */           
            if (!read) {           	
                while (1) {
                    left=msdc_txfifocnt();                    
                    if (left == 0) {
                        break;	
                    }  
                    if (msdc_pio_abort(host, data, jiffies + DAT_TIMEOUT)) {
                        break;
                        /* Fix me: what about if data error, when stop ? how to? */
                    }                                    
                }
            } else {
                /* Fix me: read case: need to check CRC error */	
            }

            /* For write case: SDCBUSY and Xfer_Comp will assert when DAT0 not busy. 
               For read case : SDCBUSY and Xfer_Comp will assert when last byte read out from FIFO.
            */                             
            
            /* try not to wait xfer_comp interrupt. 
               the next command will check SDC_BUSY. 
               SDC_BUSY means xfer_comp assert 
            */ 
                      
        } // PIO mode 
        
        /* Last: stop transfer */
        if (data->stop){ 
            if (msdc_do_command(host, data->stop, 0, CMD_TIMEOUT) != 0) {
                goto done; 
            }
        } 
    }

done:
    if (data != NULL) {
        host->data = NULL;
        host->dma_xfer = 0;    
        if (dma != 0) {
            msdc_dma_off();     
            host->dma.used_bd  = 0;
            host->dma.used_gpd = 0;
            dma_unmap_sg(mmc_dev(mmc), data->sg, data->sg_len, dir);
        }
        host->blksz = 0;  
                
#if 0 // don't stop twice!
        if(host->hw->flags & MSDC_REMOVABLE && data->error) {          
            msdc_abort_data(host);
            /* reset in IRQ, stop command has issued. -> No need */
        } 
#endif  

        N_MSG(OPS, "CMD<%d> data<%s %s> blksz<%d> block<%d> error<%d>",cmd->opcode, (dma? "dma":"pio"), 
                (read ? "read ":"write") ,data->blksz, data->blocks, data->error);                
    }

#if 0 /* --- by chhung */
#if 1    
    //if(host->id==1) {
    if(send_type==SND_CMD) {
        if(cmd->opcode == MMC_SEND_STATUS) {
            if((cmd->resp[0] & CARD_READY_FOR_DATA) ||(CARD_CURRENT_STATE(cmd->resp[0]) != 7)){
                N_MSG(OPS,"disable clock, CMD13 IDLE");
                msdc_gate_clock(host->id); 
            } 
        } else {
            N_MSG(OPS,"disable clock, CMD<%d>", cmd->opcode);	
            msdc_gate_clock(host->id);     	
        }
    } else {
        if(read) {
    				N_MSG(OPS,"disable clock!!! Read CMD<%d>",cmd->opcode);
            msdc_gate_clock(host->id); 
        }
    }
    //}
#else
    msdc_gate_clock(host->id); 
#endif
#endif /* end of --- */
        
    if (mrq->cmd->error) host->error = 0x001;
    if (mrq->data && mrq->data->error) host->error |= 0x010;     
    if (mrq->stop && mrq->stop->error) host->error |= 0x100; 

    //if (host->error) ERR_MSG("host->error<%d>", host->error);     

    return host->error;
}

static int msdc_app_cmd(struct mmc_host *mmc, struct msdc_host *host)
{
    struct mmc_command cmd;    
    struct mmc_request mrq;
    u32 err; 

    memset(&cmd, 0, sizeof(struct mmc_command));    
    cmd.opcode = MMC_APP_CMD;    
#if 0   /* bug: we meet mmc->card is null when ACMD6 */   
    cmd.arg = mmc->card->rca << 16;
#else 
    cmd.arg = host->app_cmd_arg;     
#endif    
    cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC;

    memset(&mrq, 0, sizeof(struct mmc_request));
    mrq.cmd = &cmd; cmd.mrq = &mrq;
    cmd.data = NULL;        

    err = msdc_do_command(host, &cmd, 0, CMD_TIMEOUT);     
    return err;      	
}

static int msdc_tune_cmdrsp(struct msdc_host*host, struct mmc_command *cmd)
{
    int result = -1;
    u32 base = host->base;
    u32 rsmpl, cur_rsmpl, orig_rsmpl;
    u32 rrdly, cur_rrdly = 0xffffffff, orig_rrdly;
    u32 skip = 1;
    
    /* ==== don't support 3.0 now ====
           1: R_SMPL[1] 
           2: PAD_CMD_RESP_RXDLY[26:22] 
	    ==========================*/

    // save the previous tune result 
    sdr_get_field(MSDC_IOCON,    MSDC_IOCON_RSPL,        orig_rsmpl);
    sdr_get_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRRDLY, orig_rrdly);

    rrdly = 0; 
    do {
        for (rsmpl = 0; rsmpl < 2; rsmpl++) {
            /* Lv1: R_SMPL[1] */    	
            cur_rsmpl = (orig_rsmpl + rsmpl) % 2;         
            if (skip == 1) {
                skip = 0; 	
                continue;	
            }
            sdr_set_field(MSDC_IOCON, MSDC_IOCON_RSPL, cur_rsmpl); 

            if (host->app_cmd) {
                result = msdc_app_cmd(host->mmc, host);	
                if (result) {
                    ERR_MSG("TUNE_CMD app_cmd<%d> failed: RESP_RXDLY<%d>,R_SMPL<%d>", 
                         host->mrq->cmd->opcode, cur_rrdly, cur_rsmpl);
                    continue;
                } 
            }          
            result = msdc_do_command(host, cmd, 0, CMD_TIMEOUT); // not tune.             
            ERR_MSG("TUNE_CMD<%d> %s PAD_CMD_RESP_RXDLY[26:22]<%d> R_SMPL[1]<%d>", cmd->opcode,
                       (result == 0) ? "PASS" : "FAIL", cur_rrdly, cur_rsmpl);
                       	
            if (result == 0) {
                return 0; 	
            }                        	
            if (result != (unsigned int)(-EIO)) { 
                ERR_MSG("TUNE_CMD<%d> Error<%d> not -EIO", cmd->opcode, result);	
                return result;	 
            }

            /* should be EIO */
            if (sdr_read32(SDC_CMD) & 0x1800) { /* check if has data phase */ 
                msdc_abort_data(host);
            }
        }
		
        /* Lv2: PAD_CMD_RESP_RXDLY[26:22] */              	
        cur_rrdly = (orig_rrdly + rrdly + 1) % 32;
        sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRRDLY, cur_rrdly);		
    }while (++rrdly < 32);
	
    return result;
}

/* Support SD2.0 Only */
static int msdc_tune_bread(struct mmc_host *mmc, struct mmc_request *mrq)
{
    struct msdc_host *host = mmc_priv(mmc);
    u32 base = host->base;
    u32 ddr=0;	
    u32 dcrc=0;
    u32 rxdly, cur_rxdly0, cur_rxdly1;
    u32 dsmpl, cur_dsmpl,  orig_dsmpl;
    u32 cur_dat0,  cur_dat1,  cur_dat2,  cur_dat3;
    u32 cur_dat4,  cur_dat5,  cur_dat6,  cur_dat7;
    u32 orig_dat0, orig_dat1, orig_dat2, orig_dat3;
    u32 orig_dat4, orig_dat5, orig_dat6, orig_dat7;
    int result = -1;
    u32 skip = 1;

    sdr_get_field(MSDC_IOCON, MSDC_IOCON_DSPL, orig_dsmpl);
	
    /* Tune Method 2. */
    sdr_set_field(MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);

    rxdly = 0; 
    do {
        for (dsmpl = 0; dsmpl < 2; dsmpl++) {
            cur_dsmpl = (orig_dsmpl + dsmpl) % 2;
            if (skip == 1) {
                skip = 0; 	
                continue;	
            }             
            sdr_set_field(MSDC_IOCON, MSDC_IOCON_DSPL, cur_dsmpl);

            if (host->app_cmd) {
                result = msdc_app_cmd(host->mmc, host);	
                if (result) {
                    ERR_MSG("TUNE_BREAD app_cmd<%d> failed", host->mrq->cmd->opcode);	
                    continue;
                } 
            } 
            result = msdc_do_request(mmc,mrq);
            
            sdr_get_field(SDC_DCRC_STS, SDC_DCRC_STS_POS|SDC_DCRC_STS_NEG, dcrc); /* RO */
            if (!ddr) dcrc &= ~SDC_DCRC_STS_NEG;
            ERR_MSG("TUNE_BREAD<%s> dcrc<0x%x> DATRDDLY0/1<0x%x><0x%x> dsmpl<0x%x>",
                        (result == 0 && dcrc == 0) ? "PASS" : "FAIL", dcrc,
                        sdr_read32(MSDC_DAT_RDDLY0), sdr_read32(MSDC_DAT_RDDLY1), cur_dsmpl);

            /* Fix me: result is 0, but dcrc is still exist */
            if (result == 0 && dcrc == 0) {
                goto done;
            } else {
                /* there is a case: command timeout, and data phase not processed */
                if (mrq->data->error != 0 && mrq->data->error != (unsigned int)(-EIO)) {
                    ERR_MSG("TUNE_READ: result<0x%x> cmd_error<%d> data_error<%d>", 
                               result, mrq->cmd->error, mrq->data->error);	
                    goto done;     	
                }
            }
        }    

        cur_rxdly0 = sdr_read32(MSDC_DAT_RDDLY0);
        cur_rxdly1 = sdr_read32(MSDC_DAT_RDDLY1);

        /* E1 ECO. YD: Reverse */
        if (sdr_read32(MSDC_ECO_VER) >= 4) {
            orig_dat0 = (cur_rxdly0 >> 24) & 0x1F;
            orig_dat1 = (cur_rxdly0 >> 16) & 0x1F;
            orig_dat2 = (cur_rxdly0 >>  8) & 0x1F;
            orig_dat3 = (cur_rxdly0 >>  0) & 0x1F;
            orig_dat4 = (cur_rxdly1 >> 24) & 0x1F;
            orig_dat5 = (cur_rxdly1 >> 16) & 0x1F;
            orig_dat6 = (cur_rxdly1 >>  8) & 0x1F;
            orig_dat7 = (cur_rxdly1 >>  0) & 0x1F;
        } else {   
            orig_dat0 = (cur_rxdly0 >>  0) & 0x1F;
            orig_dat1 = (cur_rxdly0 >>  8) & 0x1F;
            orig_dat2 = (cur_rxdly0 >> 16) & 0x1F;
            orig_dat3 = (cur_rxdly0 >> 24) & 0x1F;
            orig_dat4 = (cur_rxdly1 >>  0) & 0x1F;
            orig_dat5 = (cur_rxdly1 >>  8) & 0x1F;
            orig_dat6 = (cur_rxdly1 >> 16) & 0x1F;
            orig_dat7 = (cur_rxdly1 >> 24) & 0x1F;
        }
                
        if (ddr) {
            cur_dat0 = (dcrc & (1 << 0) || dcrc & (1 << 8))  ? ((orig_dat0 + 1) % 32) : orig_dat0;
            cur_dat1 = (dcrc & (1 << 1) || dcrc & (1 << 9))  ? ((orig_dat1 + 1) % 32) : orig_dat1;
            cur_dat2 = (dcrc & (1 << 2) || dcrc & (1 << 10)) ? ((orig_dat2 + 1) % 32) : orig_dat2;
            cur_dat3 = (dcrc & (1 << 3) || dcrc & (1 << 11)) ? ((orig_dat3 + 1) % 32) : orig_dat3;
        } else {
            cur_dat0 = (dcrc & (1 << 0)) ? ((orig_dat0 + 1) % 32) : orig_dat0;
            cur_dat1 = (dcrc & (1 << 1)) ? ((orig_dat1 + 1) % 32) : orig_dat1;
            cur_dat2 = (dcrc & (1 << 2)) ? ((orig_dat2 + 1) % 32) : orig_dat2;
            cur_dat3 = (dcrc & (1 << 3)) ? ((orig_dat3 + 1) % 32) : orig_dat3;
        }
        cur_dat4 = (dcrc & (1 << 4)) ? ((orig_dat4 + 1) % 32) : orig_dat4;
        cur_dat5 = (dcrc & (1 << 5)) ? ((orig_dat5 + 1) % 32) : orig_dat5;
        cur_dat6 = (dcrc & (1 << 6)) ? ((orig_dat6 + 1) % 32) : orig_dat6;
        cur_dat7 = (dcrc & (1 << 7)) ? ((orig_dat7 + 1) % 32) : orig_dat7;

        cur_rxdly0 = (cur_dat0 << 24) | (cur_dat1 << 16) | (cur_dat2 << 8) | (cur_dat3 << 0);
        cur_rxdly1 = (cur_dat4 << 24) | (cur_dat5 << 16) | (cur_dat6 << 8) | (cur_dat7 << 0);

        sdr_write32(MSDC_DAT_RDDLY0, cur_rxdly0);
        sdr_write32(MSDC_DAT_RDDLY1, cur_rxdly1);

    } while (++rxdly < 32);   
          
done:
    return result;
}

static int msdc_tune_bwrite(struct mmc_host *mmc,struct mmc_request *mrq)
{
	  struct msdc_host *host = mmc_priv(mmc);
    u32 base = host->base;

    u32 wrrdly, cur_wrrdly = 0xffffffff, orig_wrrdly;
    u32 dsmpl,  cur_dsmpl,  orig_dsmpl;
    u32 rxdly,  cur_rxdly0;
    u32 orig_dat0, orig_dat1, orig_dat2, orig_dat3;
    u32 cur_dat0,  cur_dat1,  cur_dat2,  cur_dat3;
    int result = -1;
    u32 skip = 1;

    // MSDC_IOCON_DDR50CKD need to check. [Fix me] 
    
    sdr_get_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY, orig_wrrdly);
    sdr_get_field(MSDC_IOCON,    MSDC_IOCON_DSPL,        orig_dsmpl );

    /* Tune Method 2. just DAT0 */  
    sdr_set_field(MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);
    cur_rxdly0 = sdr_read32(MSDC_DAT_RDDLY0);
    
    /* E1 ECO. YD: Reverse */
    if (sdr_read32(MSDC_ECO_VER) >= 4) {
        orig_dat0 = (cur_rxdly0 >> 24) & 0x1F;
        orig_dat1 = (cur_rxdly0 >> 16) & 0x1F;
        orig_dat2 = (cur_rxdly0 >>  8) & 0x1F;
        orig_dat3 = (cur_rxdly0 >>  0) & 0x1F;
    } else {
        orig_dat0 = (cur_rxdly0 >>  0) & 0x1F;
        orig_dat1 = (cur_rxdly0 >>  8) & 0x1F;
        orig_dat2 = (cur_rxdly0 >> 16) & 0x1F;
        orig_dat3 = (cur_rxdly0 >> 24) & 0x1F;
    }

    rxdly = 0;
    do {
        wrrdly = 0;
        do {    
            for (dsmpl = 0; dsmpl < 2; dsmpl++) {
                cur_dsmpl = (orig_dsmpl + dsmpl) % 2;
                if (skip == 1) {
                    skip = 0;
                    continue; 	
                }    
                sdr_set_field(MSDC_IOCON, MSDC_IOCON_DSPL, cur_dsmpl);
                
                if (host->app_cmd) {
                    result = msdc_app_cmd(host->mmc, host);	
                    if (result) {
                        ERR_MSG("TUNE_BWRITE app_cmd<%d> failed", host->mrq->cmd->opcode);	
                        continue;
                    } 
                }             
                result = msdc_do_request(mmc,mrq);
            
                ERR_MSG("TUNE_BWRITE<%s> DSPL<%d> DATWRDLY<%d> MSDC_DAT_RDDLY0<0x%x>", 
                          result == 0 ? "PASS" : "FAIL", 
                          cur_dsmpl, cur_wrrdly, cur_rxdly0);
            
                if (result == 0) {
                    goto done;
                }
                else {
                    /* there is a case: command timeout, and data phase not processed */
                    if (mrq->data->error != (unsigned int)(-EIO)) {
                        ERR_MSG("TUNE_READ: result<0x%x> cmd_error<%d> data_error<%d>", 
                                   result, mrq->cmd->error, mrq->data->error);	
                        goto done;     	
                    }
                }       
            }
            cur_wrrdly = (orig_wrrdly + wrrdly + 1) % 32;
            sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY, cur_wrrdly);             
        } while (++wrrdly < 32); 
        
        cur_dat0 = (orig_dat0 + rxdly) % 32; /* only adjust bit-1 for crc */
        cur_dat1 = orig_dat1;
        cur_dat2 = orig_dat2;
        cur_dat3 = orig_dat3;                    
    
        cur_rxdly0 = (cur_dat0 << 24) | (cur_dat1 << 16) | (cur_dat2 << 8) | (cur_dat3 << 0);       
        sdr_write32(MSDC_DAT_RDDLY0, cur_rxdly0);    
    } while (++rxdly < 32); 

done:
    return result;
}

static int msdc_get_card_status(struct mmc_host *mmc, struct msdc_host *host, u32 *status)
{
    struct mmc_command cmd;    
    struct mmc_request mrq;
    u32 err; 

    memset(&cmd, 0, sizeof(struct mmc_command));    
    cmd.opcode = MMC_SEND_STATUS;    
    if (mmc->card) {
        cmd.arg = mmc->card->rca << 16;
    } else {
        ERR_MSG("cmd13 mmc card is null");	   	
        cmd.arg = host->app_cmd_arg;    	
    }
    cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;

    memset(&mrq, 0, sizeof(struct mmc_request));
    mrq.cmd = &cmd; cmd.mrq = &mrq;
    cmd.data = NULL;        

    err = msdc_do_command(host, &cmd, 1, CMD_TIMEOUT);        
    
    if (status) {
        *status = cmd.resp[0];
    }    
    
    return err;                	
}

static int msdc_check_busy(struct mmc_host *mmc, struct msdc_host *host)
{
    u32 err = 0; 
    u32 status = 0;
    
    do {
        err = msdc_get_card_status(mmc, host, &status);
        if (err) return err;
        /* need cmd12? */    	 
        ERR_MSG("cmd<13> resp<0x%x>", status);
    } while (R1_CURRENT_STATE(status) == 7);   
    
    return err; 	
}

/* failed when msdc_do_request */
static int msdc_tune_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct mmc_command *cmd;
    struct mmc_data *data;
    //u32 base = host->base;
	  int ret=0, read; 
	  
    cmd  = mrq->cmd;
    data = mrq->cmd->data;
    
    read = data->flags & MMC_DATA_READ ? 1 : 0;

    if (read) {
        if (data->error == (unsigned int)(-EIO)) {	       	
            ret = msdc_tune_bread(mmc,mrq);    	
        }
    } else {
        ret = msdc_check_busy(mmc, host);    	
        if (ret){
            ERR_MSG("XXX cmd13 wait program done failed");
            return ret;
        }
        /* CRC and TO */   	
        /* Fix me: don't care card status? */
        ret = msdc_tune_bwrite(mmc,mrq);    	
    }

    return ret;
}

/* ops.request */
static void msdc_ops_request(struct mmc_host *mmc,struct mmc_request *mrq)
{   
    struct msdc_host *host = mmc_priv(mmc);

    //=== for sdio profile ===
#if 0 /* --- by chhung */
    u32 old_H32, old_L32, new_H32, new_L32;
    u32 ticks = 0, opcode = 0, sizes = 0, bRx = 0; 
#endif /* end of --- */
      
    if(host->mrq){
        ERR_MSG("XXX host->mrq<0x%.8x>", (int)host->mrq);   
        BUG();	
    }       	 
      
    if (!is_card_present(host) || host->power_mode == MMC_POWER_OFF) {
        ERR_MSG("cmd<%d> card<%d> power<%d>", mrq->cmd->opcode, is_card_present(host), host->power_mode);
        mrq->cmd->error = (unsigned int)-ENOMEDIUM; 
        
#if 1        
        mrq->done(mrq);         // call done directly.
#else
        mrq->cmd->retries = 0;  // please don't retry.
        mmc_request_done(mmc, mrq);
#endif

        return;
    }
    
    /* start to process */
    spin_lock(&host->lock); 
#if 0 /* --- by chhung */
    if (sdio_pro_enable) {  //=== for sdio profile ===  
        if (mrq->cmd->opcode == 52 || mrq->cmd->opcode == 53) {    
            GPT_GetCounter64(&old_L32, &old_H32); 
        }
    }
#endif /* end of --- */
    
    host->mrq = mrq;    

    if (msdc_do_request(mmc,mrq)) {  	
        if(host->hw->flags & MSDC_REMOVABLE && mrq->data && mrq->data->error) {
            msdc_tune_request(mmc,mrq);                                    	
        }        	
    }

    /* ==== when request done, check if app_cmd ==== */
    if (mrq->cmd->opcode == MMC_APP_CMD) {
        host->app_cmd = 1; 	  
        host->app_cmd_arg = mrq->cmd->arg;  /* save the RCA */
    } else {
        host->app_cmd = 0; 	 
        //host->app_cmd_arg = 0;    	
    }
        
    host->mrq = NULL; 

#if 0 /* --- by chhung */
    //=== for sdio profile ===
    if (sdio_pro_enable) {  
        if (mrq->cmd->opcode == 52 || mrq->cmd->opcode == 53) {     
            GPT_GetCounter64(&new_L32, &new_H32);
            ticks = msdc_time_calc(old_L32, old_H32, new_L32, new_H32);
            
            opcode = mrq->cmd->opcode;    
            if (mrq->cmd->data) {
                sizes = mrq->cmd->data->blocks * mrq->cmd->data->blksz; 	
                bRx = mrq->cmd->data->flags & MMC_DATA_READ ? 1 : 0 ;
            } else {
                bRx = mrq->cmd->arg	& 0x80000000 ? 1 : 0;  
            }
            
            if (!mrq->cmd->error) {
                msdc_performance(opcode, sizes, bRx, ticks);
            }
        }    
    } 
#endif /* end of --- */
    spin_unlock(&host->lock);
        
    mmc_request_done(mmc, mrq);
     
   return;
}

/* called by ops.set_ios */
static void msdc_set_buswidth(struct msdc_host *host, u32 width)
{
    u32 base = host->base;
    u32 val = sdr_read32(SDC_CFG);
    
    val &= ~SDC_CFG_BUSWIDTH;
    
    switch (width) {
    default:
    case MMC_BUS_WIDTH_1:
        width = 1;
        val |= (MSDC_BUS_1BITS << 16);
        break;
    case MMC_BUS_WIDTH_4:
        val |= (MSDC_BUS_4BITS << 16);
        break;
    case MMC_BUS_WIDTH_8:
        val |= (MSDC_BUS_8BITS << 16);
        break;
    }
    
    sdr_write32(SDC_CFG, val);

    N_MSG(CFG, "Bus Width = %d", width);
}

/* ops.set_ios */
static void msdc_ops_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct msdc_hw *hw=host->hw;
    u32 base = host->base;
    u32 ddr = 0;

#ifdef MT6575_SD_DEBUG
    static char *vdd[] = {
        "1.50v", "1.55v", "1.60v", "1.65v", "1.70v", "1.80v", "1.90v",
        "2.00v", "2.10v", "2.20v", "2.30v", "2.40v", "2.50v", "2.60v",
        "2.70v", "2.80v", "2.90v", "3.00v", "3.10v", "3.20v", "3.30v",
        "3.40v", "3.50v", "3.60v"		
    };
    static char *power_mode[] = {
        "OFF", "UP", "ON"
    };
    static char *bus_mode[] = {
        "UNKNOWN", "OPENDRAIN", "PUSHPULL"
    };
    static char *timing[] = {
        "LEGACY", "MMC_HS", "SD_HS"
    };

    N_MSG(CFG, "SET_IOS: CLK(%dkHz), BUS(%s), BW(%u), PWR(%s), VDD(%s), TIMING(%s)",
        ios->clock / 1000, bus_mode[ios->bus_mode],
        (ios->bus_width == MMC_BUS_WIDTH_4) ? 4 : 1,
        power_mode[ios->power_mode], vdd[ios->vdd], timing[ios->timing]);
#endif

    msdc_set_buswidth(host, ios->bus_width);
    
    /* Power control ??? */
    switch (ios->power_mode) {
    case MMC_POWER_OFF:
    case MMC_POWER_UP:
    // msdc_set_power_mode(host, ios->power_mode); /* --- by chhung */
        break;
    case MMC_POWER_ON:
        host->power_mode = MMC_POWER_ON;
        break;
    default:
        break;
    }

    /* Clock control */
    if (host->mclk != ios->clock) {
        if(ios->clock > 25000000) {	
            //if (!(host->hw->flags & MSDC_REMOVABLE)) {       	
            INIT_MSG("SD data latch edge<%d>", hw->data_edge);            
            sdr_set_field(MSDC_IOCON, MSDC_IOCON_RSPL, hw->cmd_edge);
            sdr_set_field(MSDC_IOCON, MSDC_IOCON_DSPL, hw->data_edge);
            //} /* for tuning debug */
        } else { /* default value */
            sdr_write32(MSDC_IOCON,      0x00000000);
            // sdr_write32(MSDC_DAT_RDDLY0, 0x00000000);
            sdr_write32(MSDC_DAT_RDDLY0, 0x10101010);		// for MT7620 E2 and afterward
            sdr_write32(MSDC_DAT_RDDLY1, 0x00000000);            
            // sdr_write32(MSDC_PAD_TUNE,   0x00000000);
            sdr_write32(MSDC_PAD_TUNE,   0x84101010);		// for MT7620 E2 and afterward
        }
        msdc_set_mclk(host, ddr, ios->clock);
    }
}

/* ops.get_ro */
static int msdc_ops_get_ro(struct mmc_host *mmc)
{
    struct msdc_host *host = mmc_priv(mmc);
    u32 base = host->base;
    unsigned long flags;
    int ro = 0;

    if (host->hw->flags & MSDC_WP_PIN_EN) { /* set for card */
        spin_lock_irqsave(&host->lock, flags);
        ro = ((~sdr_read32(MSDC_PS)) >> 31);
        spin_unlock_irqrestore(&host->lock, flags);
    }
    return ro;
}

/* ops.get_cd */
static int msdc_ops_get_cd(struct mmc_host *mmc)
{
    struct msdc_host *host = mmc_priv(mmc);
    u32 base = host->base;    
    unsigned long flags;
    int present = 1;

    /* for sdio, MSDC_REMOVABLE not set, always return 1 */
    if (!(host->hw->flags & MSDC_REMOVABLE)) {
        /* For sdio, read H/W always get<1>, but may timeout some times */       	    	
#if 1
        host->card_inserted = 1;       
        return 1;
#else
        host->card_inserted = (host->pm_state.event == PM_EVENT_USER_RESUME) ? 1 : 0; 
        INIT_MSG("sdio ops_get_cd<%d>", host->card_inserted);
        return host->card_inserted; 
#endif
    }

    /* MSDC_CD_PIN_EN set for card */
    if (host->hw->flags & MSDC_CD_PIN_EN) {
        spin_lock_irqsave(&host->lock, flags);
#if 0        
        present = host->card_inserted;  /* why not read from H/W: Fix me*/
#else
        present = (sdr_read32(MSDC_PS) & MSDC_PS_CDSTS) ? 0 : 1; 
        host->card_inserted = present;  
#endif        
        spin_unlock_irqrestore(&host->lock, flags);
    } else {
        present = 0; /* TODO? Check DAT3 pins for card detection */
    }

    INIT_MSG("ops_get_cd return<%d>", present);
    return present;
}

/* ops.enable_sdio_irq */
static void msdc_ops_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct msdc_hw *hw = host->hw;
    u32 base = host->base;
    u32 tmp;

    if (hw->flags & MSDC_EXT_SDIO_IRQ) { /* yes for sdio */
        if (enable) {
            hw->enable_sdio_eirq();  /* combo_sdio_enable_eirq */
        } else {
            hw->disable_sdio_eirq(); /* combo_sdio_disable_eirq */
        }
    } else { 
    	  ERR_MSG("XXX ");  /* so never enter here */
        tmp = sdr_read32(SDC_CFG);
        /* FIXME. Need to interrupt gap detection */
        if (enable) {
            tmp |= (SDC_CFG_SDIOIDE | SDC_CFG_SDIOINTWKUP);           
        } else {
            tmp &= ~(SDC_CFG_SDIOIDE | SDC_CFG_SDIOINTWKUP);
        }
        sdr_write32(SDC_CFG, tmp);      
    }
}

static struct mmc_host_ops mt_msdc_ops = {
    .request         = msdc_ops_request,
    .set_ios         = msdc_ops_set_ios,
    .get_ro          = msdc_ops_get_ro,
    .get_cd          = msdc_ops_get_cd,
    .enable_sdio_irq = msdc_ops_enable_sdio_irq,
};

/*--------------------------------------------------------------------------*/
/* interrupt handler                                                    */
/*--------------------------------------------------------------------------*/
static irqreturn_t msdc_irq(int irq, void *dev_id)
{
    struct msdc_host  *host = (struct msdc_host *)dev_id;
    struct mmc_data   *data = host->data;
    struct mmc_command *cmd = host->cmd;
    u32 base = host->base;
        
    u32 cmdsts = MSDC_INT_RSPCRCERR  | MSDC_INT_CMDTMO  | MSDC_INT_CMDRDY  |
                 MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO | MSDC_INT_ACMDRDY |
                 MSDC_INT_ACMD19_DONE;                 
    u32 datsts = MSDC_INT_DATCRCERR  |MSDC_INT_DATTMO;

    u32 intsts = sdr_read32(MSDC_INT);
    u32 inten  = sdr_read32(MSDC_INTEN); inten &= intsts; 

    sdr_write32(MSDC_INT, intsts);  /* clear interrupts */
    /* MSG will cause fatal error */
        
    /* card change interrupt */
    if (intsts & MSDC_INT_CDSC){
#if defined CONFIG_MTK_MMC_CD_POLL        
	return IRQ_HANDLED;
#endif
        IRQ_MSG("MSDC_INT_CDSC irq<0x%.8x>", intsts); 
#if 0 /* ---/+++ by chhung: fix slot mechanical bounce issue */
        tasklet_hi_schedule(&host->card_tasklet);
#else
	schedule_delayed_work(&host->card_delaywork, HZ);
#endif
        /* tuning when plug card ? */
    }
    
    /* sdio interrupt */
    if (intsts & MSDC_INT_SDIOIRQ){
        IRQ_MSG("XXX MSDC_INT_SDIOIRQ");  /* seems not sdio irq */
        //mmc_signal_sdio_irq(host->mmc);
    }

    /* transfer complete interrupt */
    if (data != NULL) {
        if (inten & MSDC_INT_XFER_COMPL) {       	
            data->bytes_xfered = host->dma.xfersz;
            complete(&host->xfer_done);           
        } 
        
        if (intsts & datsts) {         
            /* do basic reset, or stop command will sdc_busy */
            msdc_reset();
            msdc_clr_fifo();        
            msdc_clr_int();             
            atomic_set(&host->abort, 1);  /* For PIO mode exit */
            
            if (intsts & MSDC_INT_DATTMO){
               	IRQ_MSG("XXX CMD<%d> MSDC_INT_DATTMO", host->mrq->cmd->opcode);
               	data->error = (unsigned int)-ETIMEDOUT;
            }
            else if (intsts & MSDC_INT_DATCRCERR){
                IRQ_MSG("XXX CMD<%d> MSDC_INT_DATCRCERR, SDC_DCRC_STS<0x%x>", host->mrq->cmd->opcode, sdr_read32(SDC_DCRC_STS));
                data->error = (unsigned int)-EIO;
            }
                                    
            //if(sdr_read32(MSDC_INTEN) & MSDC_INT_XFER_COMPL) {  
            if (host->dma_xfer) {
                complete(&host->xfer_done); /* Read CRC come fast, XFER_COMPL not enabled */
            } /* PIO mode can't do complete, because not init */
        }
    }

    /* command interrupts */
    if ((cmd != NULL) && (intsts & cmdsts)) {
        if ((intsts & MSDC_INT_CMDRDY) || (intsts & MSDC_INT_ACMDRDY) || 
            (intsts & MSDC_INT_ACMD19_DONE)) {
            u32 *rsp = &cmd->resp[0];
            
            switch (host->cmd_rsp) {
            case RESP_NONE:
                break;
            case RESP_R2:
                *rsp++ = sdr_read32(SDC_RESP3); *rsp++ = sdr_read32(SDC_RESP2);
                *rsp++ = sdr_read32(SDC_RESP1); *rsp++ = sdr_read32(SDC_RESP0);
                break;
            default: /* Response types 1, 3, 4, 5, 6, 7(1b) */
                if ((intsts & MSDC_INT_ACMDRDY) || (intsts & MSDC_INT_ACMD19_DONE)) {
                    *rsp = sdr_read32(SDC_ACMD_RESP);
                } else {
                    *rsp = sdr_read32(SDC_RESP0);    
                }
                break;
            }
        } else if ((intsts & MSDC_INT_RSPCRCERR) || (intsts & MSDC_INT_ACMDCRCERR)) {
            if(intsts & MSDC_INT_ACMDCRCERR){
                IRQ_MSG("XXX CMD<%d> MSDC_INT_ACMDCRCERR",cmd->opcode);
            } 
            else {
                IRQ_MSG("XXX CMD<%d> MSDC_INT_RSPCRCERR",cmd->opcode);
            }
            cmd->error = (unsigned int)-EIO;
        } else if ((intsts & MSDC_INT_CMDTMO) || (intsts & MSDC_INT_ACMDTMO)) {
            if(intsts & MSDC_INT_ACMDTMO){
                IRQ_MSG("XXX CMD<%d> MSDC_INT_ACMDTMO",cmd->opcode);
            }
            else {
                IRQ_MSG("XXX CMD<%d> MSDC_INT_CMDTMO",cmd->opcode);
            }
            cmd->error = (unsigned int)-ETIMEDOUT;
            msdc_reset();
            msdc_clr_fifo();        
            msdc_clr_int();            
        }
        complete(&host->cmd_done);
    }

    /* mmc irq interrupts */
    if (intsts & MSDC_INT_MMCIRQ) {
        printk(KERN_INFO "msdc[%d] MMCIRQ: SDC_CSTS=0x%.8x\r\n", host->id, sdr_read32(SDC_CSTS));    
    }
    
#ifdef MT6575_SD_DEBUG
    {
        msdc_int_reg *int_reg = (msdc_int_reg*)&intsts;
        N_MSG(INT, "IRQ_EVT(0x%x): MMCIRQ(%d) CDSC(%d), ACRDY(%d), ACTMO(%d), ACCRE(%d) AC19DN(%d)", 
            intsts,
            int_reg->mmcirq,
            int_reg->cdsc,
            int_reg->atocmdrdy,
            int_reg->atocmdtmo,
            int_reg->atocmdcrc,
            int_reg->atocmd19done);
        N_MSG(INT, "IRQ_EVT(0x%x): SDIO(%d) CMDRDY(%d), CMDTMO(%d), RSPCRC(%d), CSTA(%d)", 
            intsts,
            int_reg->sdioirq,
            int_reg->cmdrdy,
            int_reg->cmdtmo,
            int_reg->rspcrc,
            int_reg->csta);
        N_MSG(INT, "IRQ_EVT(0x%x): XFCMP(%d) DXDONE(%d), DATTMO(%d), DATCRC(%d), DMAEMP(%d)", 
            intsts,
            int_reg->xfercomp,
            int_reg->dxferdone,
            int_reg->dattmo,
            int_reg->datcrc,
            int_reg->dmaqempty);

    }
#endif
    
    return IRQ_HANDLED;
}

/*--------------------------------------------------------------------------*/
/* platform_driver members                                                      */
/*--------------------------------------------------------------------------*/
/* called by msdc_drv_probe/remove */
static void msdc_enable_cd_irq(struct msdc_host *host, int enable)
{
	struct msdc_hw *hw = host->hw;
	u32 base = host->base;

	/* for sdio, not set */
	if ((hw->flags & MSDC_CD_PIN_EN) == 0) {
		/* Pull down card detection pin since it is not avaiable */
		/*
		   if (hw->config_gpio_pin) 
		   hw->config_gpio_pin(MSDC_CD_PIN, GPIO_PULL_DOWN);
		   */
		sdr_clr_bits(MSDC_PS, MSDC_PS_CDEN);
		sdr_clr_bits(MSDC_INTEN, MSDC_INTEN_CDSC);
		sdr_clr_bits(SDC_CFG, SDC_CFG_INSWKUP);
		return;
	}

	N_MSG(CFG, "CD IRQ Eanable(%d)", enable);

	if (enable) {
	    if (hw->enable_cd_eirq) { /* not set, never enter */
		    hw->enable_cd_eirq();
	    } else {
		    /* card detection circuit relies on the core power so that the core power 
		     * shouldn't be turned off. Here adds a reference count to keep 
		     * the core power alive.
		     */
		    //msdc_vcore_on(host); //did in msdc_init_hw()

		    if (hw->config_gpio_pin) /* NULL */
			    hw->config_gpio_pin(MSDC_CD_PIN, GPIO_PULL_UP);

		    sdr_set_field(MSDC_PS, MSDC_PS_CDDEBOUNCE, DEFAULT_DEBOUNCE);
		    sdr_set_bits(MSDC_PS, MSDC_PS_CDEN);
		    sdr_set_bits(MSDC_INTEN, MSDC_INTEN_CDSC);
		    sdr_set_bits(SDC_CFG, SDC_CFG_INSWKUP);  /* not in document! Fix me */
	    }
    } else {
	    if (hw->disable_cd_eirq) {
		    hw->disable_cd_eirq();
	    } else {
		    if (hw->config_gpio_pin) /* NULL */
			    hw->config_gpio_pin(MSDC_CD_PIN, GPIO_PULL_DOWN);

		    sdr_clr_bits(SDC_CFG, SDC_CFG_INSWKUP);
		    sdr_clr_bits(MSDC_PS, MSDC_PS_CDEN);
		    sdr_clr_bits(MSDC_INTEN, MSDC_INTEN_CDSC);

		    /* Here decreases a reference count to core power since card 
		     * detection circuit is shutdown.
		     */
		    //msdc_vcore_off(host);
	    }
    }
}

/* called by msdc_drv_probe */
static void msdc_init_hw(struct msdc_host *host)
{
    u32 base = host->base;
    struct msdc_hw *hw = host->hw;

#ifdef MT6575_SD_DEBUG	
    msdc_reg[host->id] = (struct msdc_regs *)host->base;
#endif

    /* Power on */
#if 0 /* --- by chhung */
    msdc_vcore_on(host);
    msdc_pin_reset(host, MSDC_PIN_PULL_UP);
    msdc_select_clksrc(host, hw->clk_src);
    enable_clock(PERI_MSDC0_PDN + host->id, "SD");
    msdc_vdd_on(host);
#endif /* end of --- */
    /* Configure to MMC/SD mode */
    sdr_set_field(MSDC_CFG, MSDC_CFG_MODE, MSDC_SDMMC); 
       
    /* Reset */
    msdc_reset();
    msdc_clr_fifo();

    /* Disable card detection */
    sdr_clr_bits(MSDC_PS, MSDC_PS_CDEN);

    /* Disable and clear all interrupts */
    sdr_clr_bits(MSDC_INTEN, sdr_read32(MSDC_INTEN));
    sdr_write32(MSDC_INT, sdr_read32(MSDC_INT));
    
#if 1
	/* reset tuning parameter */
    sdr_write32(MSDC_PAD_CTL0,   0x00090000);
    sdr_write32(MSDC_PAD_CTL1,   0x000A0000);
    sdr_write32(MSDC_PAD_CTL2,   0x000A0000);
    // sdr_write32(MSDC_PAD_TUNE,   0x00000000);
    sdr_write32(MSDC_PAD_TUNE,   0x84101010);		// for MT7620 E2 and afterward
    // sdr_write32(MSDC_DAT_RDDLY0, 0x00000000);
    sdr_write32(MSDC_DAT_RDDLY0, 0x10101010);		// for MT7620 E2 and afterward
    sdr_write32(MSDC_DAT_RDDLY1, 0x00000000);
    sdr_write32(MSDC_IOCON,      0x00000000);
#if 0 // use MT7620 default value: 0x403c004f
    sdr_write32(MSDC_PATCH_BIT0, 0x003C000F); /* bit0 modified: Rx Data Clock Source: 1 -> 2.0*/
#endif

    if (sdr_read32(MSDC_ECO_VER) >= 4) { 
        if (host->id == 1) {	
            sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, 1); 
            sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP,    1);
            
            /* internal clock: latch read data */  
            sdr_set_bits(MSDC_PATCH_BIT0, MSDC_PATCH_BIT_CKGEN_CK);  
        }       	
    }   
#endif    

    /* for safety, should clear SDC_CFG.SDIO_INT_DET_EN & set SDC_CFG.SDIO in 
       pre-loader,uboot,kernel drivers. and SDC_CFG.SDIO_INT_DET_EN will be only
       set when kernel driver wants to use SDIO bus interrupt */
    /* Configure to enable SDIO mode. it's must otherwise sdio cmd5 failed */
    sdr_set_bits(SDC_CFG, SDC_CFG_SDIO);

    /* disable detect SDIO device interupt function */
    sdr_clr_bits(SDC_CFG, SDC_CFG_SDIOIDE);

    /* eneable SMT for glitch filter */
    sdr_set_bits(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKSMT);
    sdr_set_bits(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDSMT);
    sdr_set_bits(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATSMT);

#if 1
    /* set clk, cmd, dat pad driving */
    sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKDRVN, hw->clk_drv);
    sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKDRVP, hw->clk_drv);
    sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDDRVN, hw->cmd_drv);
    sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDDRVP, hw->cmd_drv);
    sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATDRVN, hw->dat_drv);
    sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATDRVP, hw->dat_drv);
#else 
    sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKDRVN, 0);
    sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKDRVP, 0);
    sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDDRVN, 0);
    sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDDRVP, 0);
    sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATDRVN, 0);
    sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATDRVP, 0);
#endif

    /* set sampling edge */

    /* write crc timeout detection */
    sdr_set_field(MSDC_PATCH_BIT0, 1 << 30, 1);

    /* Configure to default data timeout */
    sdr_set_field(SDC_CFG, SDC_CFG_DTOC, DEFAULT_DTOC);

    msdc_set_buswidth(host, MMC_BUS_WIDTH_1);

    N_MSG(FUC, "init hardware done!");
}

/* called by msdc_drv_remove */
static void msdc_deinit_hw(struct msdc_host *host)
{
    u32 base = host->base;

    /* Disable and clear all interrupts */
    sdr_clr_bits(MSDC_INTEN, sdr_read32(MSDC_INTEN));
    sdr_write32(MSDC_INT, sdr_read32(MSDC_INT));

    /* Disable card detection */
    msdc_enable_cd_irq(host, 0);
    // msdc_set_power_mode(host, MMC_POWER_OFF);   /* make sure power down */ /* --- by chhung */
}

/* init gpd and bd list in msdc_drv_probe */
static void msdc_init_gpd_bd(struct msdc_host *host, struct msdc_dma *dma)
{
    gpd_t *gpd = dma->gpd; 
    bd_t  *bd  = dma->bd; 	
    bd_t  *ptr, *prev;
    
    /* we just support one gpd */     
    int bdlen = MAX_BD_PER_GPD;   	

    /* init the 2 gpd */
    memset(gpd, 0, sizeof(gpd_t) * 2);
    //gpd->next = (void *)virt_to_phys(gpd + 1); /* pointer to a null gpd, bug! kmalloc <-> virt_to_phys */  
    //gpd->next = (dma->gpd_addr + 1);    /* bug */
    gpd->next = (void *)((u32)dma->gpd_addr + sizeof(gpd_t));    

    //gpd->intr = 0;
    gpd->bdp  = 1;   /* hwo, cs, bd pointer */      
    //gpd->ptr  = (void*)virt_to_phys(bd); 
    gpd->ptr = (void *)dma->bd_addr; /* physical address */
    
    memset(bd, 0, sizeof(bd_t) * bdlen);
    ptr = bd + bdlen - 1;
    //ptr->eol  = 1;  /* 0 or 1 [Fix me]*/
    //ptr->next = 0;    
    
    while (ptr != bd) {
        prev = ptr - 1;
        prev->next = (void *)(dma->bd_addr + sizeof(bd_t) *(ptr - bd));
        ptr = prev;
    }
}

static int msdc_drv_probe(struct platform_device *pdev)
{
    struct mmc_host *mmc;
    struct resource *mem;
    struct msdc_host *host;
    struct msdc_hw *hw;
    unsigned long base;
    int ret, irq;
  
    /* Allocate MMC host for this device */
    mmc = mmc_alloc_host(sizeof(struct msdc_host), &pdev->dev);
    if (!mmc) return -ENOMEM;

    hw   = (struct msdc_hw*)pdev->dev.platform_data;
    mem  = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    irq  = platform_get_irq(pdev, 0);
    base = mem->start;

    //BUG_ON((!hw) || (!mem) || (irq < 0)); /* --- by chhung */
    
    mem = request_mem_region(mem->start, mem->end - mem->start + 1, DRV_NAME);
    if (mem == NULL) {
        mmc_free_host(mmc);
        return -EBUSY;
    }

    /* Set host parameters to mmc */
    mmc->ops        = &mt_msdc_ops;
    mmc->f_min      = HOST_MIN_MCLK;
    mmc->f_max      = HOST_MAX_MCLK;
    mmc->ocr_avail  = MSDC_OCR_AVAIL;
    
    /* For sd card: MSDC_SYS_SUSPEND | MSDC_WP_PIN_EN | MSDC_CD_PIN_EN | MSDC_REMOVABLE | MSDC_HIGHSPEED, 
       For sdio   : MSDC_EXT_SDIO_IRQ | MSDC_HIGHSPEED */
    if (hw->flags & MSDC_HIGHSPEED) {
        mmc->caps   = MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED;
    }
    if (hw->data_pins == 4) { /* current data_pins are all 4*/
        mmc->caps  |= MMC_CAP_4_BIT_DATA;
    } else if (hw->data_pins == 8) {
        mmc->caps  |= MMC_CAP_8_BIT_DATA;
    }
    if ((hw->flags & MSDC_SDIO_IRQ) || (hw->flags & MSDC_EXT_SDIO_IRQ))
        mmc->caps |= MMC_CAP_SDIO_IRQ;  /* yes for sdio */

#if defined CONFIG_MTK_MMC_CD_POLL
    mmc->caps |= MMC_CAP_NEEDS_POLL;
#endif

    /* MMC core transfer sizes tunable parameters */
#if LINUX_VERSION_CODE > KERNEL_VERSION(3,10,0)
    mmc->max_segs      = MAX_HW_SGMTS;
#else
    mmc->max_hw_segs   = MAX_HW_SGMTS;
    mmc->max_phys_segs = MAX_PHY_SGMTS;
#endif
    mmc->max_seg_size  = MAX_SGMT_SZ;
    mmc->max_blk_size  = HOST_MAX_BLKSZ;
    mmc->max_req_size  = MAX_REQ_SZ; 
    mmc->max_blk_count = mmc->max_req_size;

    host = mmc_priv(mmc);
    host->hw        = hw;
    host->mmc       = mmc;
    host->id        = pdev->id;
    host->error     = 0;
    host->irq       = irq;    
    host->base      = base;
    host->mclk      = 0;                   /* mclk: the request clock of mmc sub-system */
    host->hclk      = hclks[hw->clk_src];  /* hclk: clock of clock source to msdc controller */
    host->sclk      = 0;                   /* sclk: the really clock after divition */
    host->pm_state  = PMSG_RESUME;
    host->suspend   = 0;
    host->core_clkon = 0;
    host->card_clkon = 0;    
    host->core_power = 0;
    host->power_mode = MMC_POWER_OFF;
//    host->card_inserted = hw->flags & MSDC_REMOVABLE ? 0 : 1;
    host->timeout_ns = 0;
    host->timeout_clks = DEFAULT_DTOC * 65536;
  
    host->mrq = NULL; 
    //init_MUTEX(&host->sem); /* we don't need to support multiple threads access */
   
    host->dma.used_gpd = 0;
    host->dma.used_bd = 0;

    /* using dma_alloc_coherent*/  /* todo: using 1, for all 4 slots */
    host->dma.gpd = dma_alloc_coherent(NULL, MAX_GPD_NUM * sizeof(gpd_t), &host->dma.gpd_addr, GFP_KERNEL); 
    host->dma.bd =  dma_alloc_coherent(NULL, MAX_BD_NUM  * sizeof(bd_t),  &host->dma.bd_addr,  GFP_KERNEL); 
    BUG_ON((!host->dma.gpd) || (!host->dma.bd));    
    msdc_init_gpd_bd(host, &host->dma);
    /*for emmc*/
    msdc_6575_host[pdev->id] = host;
    
#if 0
    tasklet_init(&host->card_tasklet, msdc_tasklet_card, (ulong)host);
#else
    INIT_DELAYED_WORK(&host->card_delaywork, msdc_tasklet_card);
#endif
    spin_lock_init(&host->lock);
    msdc_init_hw(host);

    ret = request_irq((unsigned int)irq, msdc_irq, IRQF_TRIGGER_LOW, DRV_NAME, host);
    if (ret) goto release;
    // mt65xx_irq_unmask(irq); /* --- by chhung */
    
    if (hw->flags & MSDC_CD_PIN_EN) { /* not set for sdio */
        if (hw->request_cd_eirq) { /* not set for MT6575 */
            hw->request_cd_eirq(msdc_eirq_cd, (void*)host); /* msdc_eirq_cd will not be used! */
        }
    }

    if (hw->request_sdio_eirq) /* set to combo_sdio_request_eirq() for WIFI */
        hw->request_sdio_eirq(msdc_eirq_sdio, (void*)host); /* msdc_eirq_sdio() will be called when EIRQ */

    if (hw->register_pm) {/* yes for sdio */
#ifdef CONFIG_PM
        hw->register_pm(msdc_pm, (void*)host);  /* combo_sdio_register_pm() */
#endif
        if(hw->flags & MSDC_SYS_SUSPEND) { /* will not set for WIFI */
            ERR_MSG("MSDC_SYS_SUSPEND and register_pm both set");
        }
        //mmc->pm_flags |= MMC_PM_IGNORE_PM_NOTIFY; /* pm not controlled by system but by client. */ /* --- by chhung */
    }
    
    platform_set_drvdata(pdev, mmc);

    ret = mmc_add_host(mmc);
    if (ret) goto free_irq;

    /* Config card detection pin and enable interrupts */
    if (hw->flags & MSDC_CD_PIN_EN) {  /* set for card */
        msdc_enable_cd_irq(host, 1);
    } else {
        msdc_enable_cd_irq(host, 0);
    }  

    return 0;

free_irq:
    free_irq(irq, host);
release:
    platform_set_drvdata(pdev, NULL);
    msdc_deinit_hw(host);

#if 0
    tasklet_kill(&host->card_tasklet);
#else
    cancel_delayed_work_sync(&host->card_delaywork);
#endif

    if (mem)
        release_mem_region(mem->start, mem->end - mem->start + 1);

    mmc_free_host(mmc);

    return ret;
}

/* 4 device share one driver, using "drvdata" to show difference */
static int msdc_drv_remove(struct platform_device *pdev)
{
    struct mmc_host *mmc;
    struct msdc_host *host;
    struct resource *mem;

    mmc  = platform_get_drvdata(pdev);
    BUG_ON(!mmc);
    
    host = mmc_priv(mmc);   
    BUG_ON(!host);

    ERR_MSG("removed !!!");

    platform_set_drvdata(pdev, NULL);
    mmc_remove_host(host->mmc);
    msdc_deinit_hw(host);

#if 0
    tasklet_kill(&host->card_tasklet);
#else
    cancel_delayed_work_sync(&host->card_delaywork);
#endif
    free_irq(host->irq, host);

    dma_free_coherent(NULL, MAX_GPD_NUM * sizeof(gpd_t), host->dma.gpd, host->dma.gpd_addr);
    dma_free_coherent(NULL, MAX_BD_NUM  * sizeof(bd_t),  host->dma.bd,  host->dma.bd_addr);

    mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    if (mem)
        release_mem_region(mem->start, mem->end - mem->start + 1);

    mmc_free_host(host->mmc);

    return 0;
}

/* Fix me: Power Flow */
#ifdef CONFIG_PM
static int msdc_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
    int ret = 0;
    struct mmc_host *mmc = platform_get_drvdata(pdev);
    struct msdc_host *host = mmc_priv(mmc);

    if (mmc && state.event == PM_EVENT_SUSPEND && (host->hw->flags & MSDC_SYS_SUSPEND)) { /* will set for card */
        msdc_pm(state, (void*)host);
    }
    
    return ret;
}

static int msdc_drv_resume(struct platform_device *pdev)
{
    int ret = 0;
    struct mmc_host *mmc = platform_get_drvdata(pdev);
    struct msdc_host *host = mmc_priv(mmc);
    struct pm_message state;

    state.event = PM_EVENT_RESUME;
    if (mmc && (host->hw->flags & MSDC_SYS_SUSPEND)) {/* will set for card */
        msdc_pm(state, (void*)host);
    }

    /* This mean WIFI not controller by PM */
    
    return ret;
}
#endif

static struct platform_driver mt_msdc_driver = {
    .probe   = msdc_drv_probe,
    .remove  = msdc_drv_remove,
#ifdef CONFIG_PM
    .suspend = msdc_drv_suspend,
    .resume  = msdc_drv_resume,
#endif
    .driver  = {
        .name  = DRV_NAME,
        .owner = THIS_MODULE,
    },
};

/*--------------------------------------------------------------------------*/
/* module init/exit                                                      */
/*--------------------------------------------------------------------------*/
static int __init mt_msdc_init(void)
{
    int ret;
/* +++ by chhung */
    u32 reg, reg1;

#if defined (CONFIG_MTD_ANY_RALINK)
    extern int ra_check_flash_type(void);
    if(ra_check_flash_type() == 2) { /* NAND */
	    printk("%s: !!!!! SDXC Module Initialize Fail !!!!!", __func__);
	    return 0;
    }
#endif
    printk("MTK MSDC device init.\n");
    mtk_sd_device.dev.platform_data = &msdc0_hw;
#if defined (CONFIG_RALINK_MT7620) || defined (CONFIG_RALINK_MT7621)
    reg = sdr_read32((volatile u32*)(RALINK_SYSCTL_BASE + 0x60)) & ~(0x3<<18);
#if defined (CONFIG_RALINK_MT7620)
    reg |= 0x1<<18;
#endif
#elif defined (CONFIG_RALINK_MT7628)

#if defined (CONFIG_ETH_ONE_PORT_ONLY)
    /* TODO: maybe omitted when RAether already toggle AGPIO_CFG */
    reg = sdr_read32((volatile u32*)(RALINK_SYSCTL_BASE + 0x3c));
    reg |= 0x1e << 16;
    sdr_write32((volatile u32*)(RALINK_SYSCTL_BASE + 0x3c), reg);
    reg = sdr_read32((volatile u32*)(RALINK_SYSCTL_BASE + 0x60)) & ~(0x3<<10);
    reg1 = sdr_read32((volatile u32*)(RALINK_SYSCTL_BASE + 0x1350)) | (0x1<<26); //IOT mode,SDXC CLK=PAD_MDI_RP_P4=GPIO26, driving = 8mA
    sdr_write32((volatile u32*)(RALINK_SYSCTL_BASE + 0x1350), reg1);
    reg1 = sdr_read32((volatile u32*)(RALINK_SYSCTL_BASE + 0x1360)) & ~(0x1<<26);
    sdr_write32((volatile u32*)(RALINK_SYSCTL_BASE + 0x1360), reg1);
#else  
    reg = sdr_read32((volatile u32*)(RALINK_SYSCTL_BASE + 0x60)) & ~(0x3<<0) & ~(0x3<<6) & ~(0x3<<10) & ~(0x1<<15) & ~(0x3<<20) & ~(0x3<<24) | (0x1<<0) | (0x1<<6) | (0x1<<10) | (0x1<<15) | (0x1<<20) | (0x1<<24);
   // reg = 0x55158448;
   
    reg1 = sdr_read32((volatile u32*)(RALINK_SYSCTL_BASE + 0x1340)) | (0x1<<11); //Normal mode(AP mode) , SDXC CLK=PAD_GPIO0=GPIO11, driving = 8mA
    sdr_write32((volatile u32*)(RALINK_SYSCTL_BASE + 0x1340), reg1);


#endif
#if defined (CONFIG_MTK_MMC_EMMC_8BIT)
    reg |= 0x3<<26 | 0x3<<28 | 0x3<<30;
    msdc0_hw.data_pins      = 8,
#endif
#endif
    sdr_write32((volatile u32*)(RALINK_SYSCTL_BASE + 0x60), reg);
    platform_device_register(&mtk_sd_device);
/* end of +++ */

    ret = platform_driver_register(&mt_msdc_driver);
    if (ret) {
        printk(KERN_ERR DRV_NAME ": Can't register driver");
        return ret;
    }
    printk(KERN_INFO DRV_NAME ": MediaTek MT6575 MSDC Driver\n");

#if defined (MT6575_SD_DEBUG)
    msdc_debug_proc_init();
#endif
    return 0;
}

static void __exit mt_msdc_exit(void)
{
    platform_device_unregister(&mtk_sd_device);
    platform_driver_unregister(&mt_msdc_driver);
}

module_init(mt_msdc_init);
module_exit(mt_msdc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek MT6575 SD/MMC Card Driver");
MODULE_AUTHOR("Infinity Chen <infinity.chen@mediatek.com>");

EXPORT_SYMBOL(msdc_6575_host);
