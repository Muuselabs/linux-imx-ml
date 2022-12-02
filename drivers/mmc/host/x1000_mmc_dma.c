// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  Copyright (C) 2013, Imagination Technologies
 *  Copyright (C) 2022, Pieter Palmers <pieter.palmers@pentasono.com>
 *
 *  Ingenic X1000(e) SD/MMC controller driver [DMA only]
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mmc/host.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>

#include <asm/cacheflush.h>

#define JZ_REG_MMC_STRPCL	0x00
#define JZ_REG_MMC_STATUS	0x04
#define JZ_REG_MMC_CLKRT	0x08
#define JZ_REG_MMC_CMDAT	0x0C
#define JZ_REG_MMC_RESTO	0x10
#define JZ_REG_MMC_RDTO		0x14
#define JZ_REG_MMC_BLKLEN	0x18
#define JZ_REG_MMC_NOB		0x1C
#define JZ_REG_MMC_SNOB		0x20
#define JZ_REG_MMC_IMASK	0x24
#define JZ_REG_MMC_IREG		0x28
#define JZ_REG_MMC_CMD		0x2C
#define JZ_REG_MMC_ARG		0x30
#define JZ_REG_MMC_RESP_FIFO	0x34
#define JZ_REG_MMC_RXFIFO	0x38
#define JZ_REG_MMC_TXFIFO	0x3C
#define JZ_REG_MMC_LPM		0x40
#define JZ_REG_MMC_DMAC		0x44

#define JZ_MMC_STRPCL_EXIT_MULTIPLE BIT(7)
#define JZ_MMC_STRPCL_EXIT_TRANSFER BIT(6)
#define JZ_MMC_STRPCL_START_READWAIT BIT(5)
#define JZ_MMC_STRPCL_STOP_READWAIT BIT(4)
#define JZ_MMC_STRPCL_RESET BIT(3)
#define JZ_MMC_STRPCL_START_OP BIT(2)
#define JZ_MMC_STRPCL_CLOCK_CONTROL (BIT(1) | BIT(0))
#define JZ_MMC_STRPCL_CLOCK_STOP BIT(0)
#define JZ_MMC_STRPCL_CLOCK_START BIT(1)


#define JZ_MMC_STATUS_IS_RESETTING BIT(15)
#define JZ_MMC_STATUS_SDIO_INT_ACTIVE BIT(14)
#define JZ_MMC_STATUS_PRG_DONE BIT(13)
#define JZ_MMC_STATUS_DATA_TRAN_DONE BIT(12)
#define JZ_MMC_STATUS_END_CMD_RES BIT(11)
#define JZ_MMC_STATUS_DATA_FIFO_AFULL BIT(10)
#define JZ_MMC_STATUS_IS_READWAIT BIT(9)
#define JZ_MMC_STATUS_CLK_EN BIT(8)
#define JZ_MMC_STATUS_DATA_FIFO_FULL BIT(7)
#define JZ_MMC_STATUS_DATA_FIFO_EMPTY BIT(6)
#define JZ_MMC_STATUS_CRC_RES_ERR BIT(5)
#define JZ_MMC_STATUS_CRC_READ_ERROR BIT(4)
#define JZ_MMC_STATUS_TIMEOUT_WRITE BIT(3)
#define JZ_MMC_STATUS_CRC_WRITE_ERROR BIT(2)
#define JZ_MMC_STATUS_TIMEOUT_RES BIT(1)
#define JZ_MMC_STATUS_TIMEOUT_READ BIT(0)

#define JZ_MMC_STATUS_READ_ERROR_MASK (BIT(4) | BIT(0))
#define JZ_MMC_STATUS_WRITE_ERROR_MASK (BIT(3) | BIT(2))


#define JZ_MMC_CMDAT_IO_ABORT BIT(11)
#define JZ_MMC_CMDAT_BUS_WIDTH_4BIT BIT(10)
#define JZ_MMC_CMDAT_BUS_WIDTH_8BIT (BIT(10) | BIT(9))
#define	JZ_MMC_CMDAT_BUS_WIDTH_MASK (BIT(10) | BIT(9))
#define JZ_MMC_CMDAT_DMA_EN BIT(8)
#define JZ_MMC_CMDAT_INIT BIT(7)
#define JZ_MMC_CMDAT_BUSY BIT(6)
#define JZ_MMC_CMDAT_STREAM BIT(5)
#define JZ_MMC_CMDAT_WRITE BIT(4)
#define JZ_MMC_CMDAT_DATA_EN BIT(3)
#define JZ_MMC_CMDAT_RESPONSE_FORMAT (BIT(2) | BIT(1) | BIT(0))
#define JZ_MMC_CMDAT_RSP_R1 1
#define JZ_MMC_CMDAT_RSP_R2 2
#define JZ_MMC_CMDAT_RSP_R3 3

#define JZ_MMC_IRQ_SDIO BIT(7)
#define JZ_MMC_IRQ_TXFIFO_WR_REQ BIT(6)
#define JZ_MMC_IRQ_RXFIFO_RD_REQ BIT(5)
#define JZ_MMC_IRQ_END_CMD_RES BIT(2)
#define JZ_MMC_IRQ_PRG_DONE BIT(1)
#define JZ_MMC_IRQ_DATA_TRAN_DONE BIT(0)

#define JZ_MMC_DMAC_DMA_SEL BIT(1)
#define JZ_MMC_DMAC_DMA_EN BIT(0)

#define	JZ_MMC_LPM_DRV_RISING BIT(31)
#define	JZ_MMC_LPM_DRV_RISING_QTR_PHASE_DLY BIT(31)
#define	JZ_MMC_LPM_DRV_RISING_1NS_DLY BIT(30)
#define	JZ_MMC_LPM_SMP_RISING_QTR_OR_HALF_PHASE_DLY BIT(29)
#define	JZ_MMC_LPM_LOW_POWER_MODE_EN BIT(0)

#define JZ_MMC_CLK_RATE 24000000
#define JZ_MMC_REQ_TIMEOUT_MS 5000

enum x1000_mmc_state {
	X1000_MMC_STATE_READ_RESPONSE,
	X1000_MMC_STATE_TRANSFER_DATA,
	X1000_MMC_STATE_SEND_STOP,
	X1000_MMC_STATE_DONE,
};

/*
 * The MMC core allows to prepare a mmc_request while another mmc_request
 * is in-flight. This is used via the pre_req/post_req hooks.
 * This driver uses the pre_req/post_req hooks to map/unmap the mmc_request.
 * Following what other drivers do (sdhci, dw_mmc) we use the following cookie
 * flags to keep track of the mmc_request mapping state.
 *
 * COOKIE_UNMAPPED: the request is not mapped.
 * COOKIE_PREMAPPED: the request was mapped in pre_req,
 * and should be unmapped in post_req.
 * COOKIE_MAPPED: the request was mapped in the irq handler,
 * and should be unmapped before mmc_request_done is called..
 */
enum x1000_cookie {
	COOKIE_UNMAPPED = 0,
	COOKIE_PREMAPPED,
	COOKIE_MAPPED,
};

struct x1000_mmc_host {
	struct mmc_host *mmc;
	struct platform_device *pdev;
	struct clk *clk;

	int irq;
	int card_detect_irq;

	void __iomem *base;
	struct resource *mem_res;
	struct mmc_request *req;
	struct mmc_command *cmd;

	unsigned long waiting;

	uint32_t cmdat;

	uint32_t irq_mask;

	spinlock_t lock;

	struct timer_list timeout_timer;
	struct sg_mapping_iter miter;
	enum x1000_mmc_state state;

	/* DMA support */
	struct dma_chan *dma_rx;
	struct dma_chan *dma_tx;

	bool req_in_progress;
	bool dma_in_progress;
/* The DMA trigger level is 8 words, that is to say, the DMA read
 * trigger is when data words in MSC_RXFIFO is >= 8 and the DMA write
 * trigger is when data words in MSC_TXFIFO is < 8.
 */
#define X1000_MMC_FIFO_HALF_SIZE 8
};

static void x1000_mmc_dma_callback(void *param);

static void x1000_mmc_write_irq_mask(struct x1000_mmc_host *host,
				      uint32_t val)
{
	return writel(val, host->base + JZ_REG_MMC_IMASK);

}

static void x1000_mmc_write_irq_reg(struct x1000_mmc_host *host,
				     uint32_t val)
{
	writel(val, host->base + JZ_REG_MMC_IREG);
}

static uint32_t x1000_mmc_read_irq_reg(struct x1000_mmc_host *host)
{
	return readl(host->base + JZ_REG_MMC_IREG);
}

/*----------------------------------------------------------------------------*/
/* DMA infrastructure */

static void x1000_mmc_release_dma_channels(struct x1000_mmc_host *host)
{
	dma_release_channel(host->dma_tx);
	dma_release_channel(host->dma_rx);
}

static int x1000_mmc_acquire_dma_channels(struct x1000_mmc_host *host)
{
	host->dma_tx = dma_request_chan(mmc_dev(host->mmc), "tx");
	if (IS_ERR(host->dma_tx)) {
		dev_err(mmc_dev(host->mmc), "Failed to get dma_tx channel\n");
		return PTR_ERR(host->dma_tx);
	}

	host->dma_rx = dma_request_chan(mmc_dev(host->mmc), "rx");
	if (IS_ERR(host->dma_rx)) {
		dev_err(mmc_dev(host->mmc), "Failed to get dma_rx channel\n");
		dma_release_channel(host->dma_tx);
		return PTR_ERR(host->dma_rx);
	}

	return 0;
}

static inline struct dma_chan *x1000_mmc_get_dma_chan(struct x1000_mmc_host *host,
						       struct mmc_data *data)
{
	return (data->flags & MMC_DATA_READ) ? host->dma_rx : host->dma_tx;
}

static void x1000_mmc_dma_unmap(struct x1000_mmc_host *host,
				 struct mmc_data *data)
{
	struct dma_chan *chan = x1000_mmc_get_dma_chan(host, data);
	enum dma_data_direction dir = mmc_get_dma_dir(data);

	dma_unmap_sg(chan->device->dev, data->sg, data->sg_len, dir);
	data->host_cookie = COOKIE_UNMAPPED;
}

/* Prepares DMA data for current or next transfer.
 * A request can be in-flight when this is called.
 */
static int x1000_mmc_prepare_dma_data(struct x1000_mmc_host *host,
				       struct mmc_data *data,
				       int cookie)
{
	struct dma_chan *chan = x1000_mmc_get_dma_chan(host, data);
	enum dma_data_direction dir = mmc_get_dma_dir(data);
	int sg_count;

	if (data->host_cookie == COOKIE_PREMAPPED)
		return data->sg_count;

	sg_count = dma_map_sg(chan->device->dev,
			data->sg,
			data->sg_len,
			dir);

	if (sg_count <= 0) {
		dev_err(mmc_dev(host->mmc),
			"Failed to map scatterlist for DMA operation\n");
		return -EINVAL;
	}

	data->sg_count = sg_count;
	data->host_cookie = cookie;

	return data->sg_count;
}

static int x1000_mmc_start_dma_transfer(struct x1000_mmc_host *host,
					 struct mmc_data *data)
{
	struct dma_chan *chan = x1000_mmc_get_dma_chan(host, data);
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config conf = {
		.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
		.src_maxburst = X1000_MMC_FIFO_HALF_SIZE,
		.dst_maxburst = X1000_MMC_FIFO_HALF_SIZE,
	};
	int sg_count;
	unsigned long flags;

	BUG_ON(host->req_in_progress == 0);
	if (data->flags & MMC_DATA_WRITE) {
		conf.direction = DMA_MEM_TO_DEV;
		conf.dst_addr = host->mem_res->start + JZ_REG_MMC_TXFIFO;
	} else {
		conf.direction = DMA_DEV_TO_MEM;
		conf.src_addr = host->mem_res->start + JZ_REG_MMC_RXFIFO;
	}

	sg_count = x1000_mmc_prepare_dma_data(host, data, COOKIE_MAPPED);
	if (sg_count < 0)
		return sg_count;

	dmaengine_slave_config(chan, &conf);
	desc = dmaengine_prep_slave_sg(chan, data->sg, sg_count,
			conf.direction,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dev_err(mmc_dev(host->mmc),
			"Failed to allocate DMA %s descriptor",
			 conf.direction == DMA_MEM_TO_DEV ? "TX" : "RX");
		goto dma_unmap;
	}

	desc->callback = x1000_mmc_dma_callback;
	desc->callback_param = host;

	spin_lock_irqsave(&host->lock, flags);
	host->dma_in_progress = 1;
	spin_unlock_irqrestore(&host->lock, flags);

	dmaengine_submit(desc);
	dma_async_issue_pending(chan);

	return 0;

dma_unmap:
	if (data->host_cookie == COOKIE_MAPPED)
		x1000_mmc_dma_unmap(host, data);
	return -ENOMEM;
}

static void x1000_mmc_pre_request(struct mmc_host *mmc,
				   struct mmc_request *mrq)
{
	struct x1000_mmc_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	data->host_cookie = COOKIE_UNMAPPED;
	if (x1000_mmc_prepare_dma_data(host, data, COOKIE_PREMAPPED) < 0)
		data->host_cookie = COOKIE_UNMAPPED;
}

static void x1000_mmc_post_request(struct mmc_host *mmc,
				    struct mmc_request *mrq,
				    int err)
{
	struct x1000_mmc_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (data && data->host_cookie != COOKIE_UNMAPPED)
		x1000_mmc_dma_unmap(host, data);

	if (err) {
		struct dma_chan *chan = x1000_mmc_get_dma_chan(host, data);

		dmaengine_terminate_all(chan);
	}
}

/*----------------------------------------------------------------------------*/

static void x1000_mmc_set_irq_enabled(struct x1000_mmc_host *host,
	unsigned int irq, bool enabled)
{
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	if (enabled)
		host->irq_mask &= ~irq;
	else
		host->irq_mask |= irq;

	x1000_mmc_write_irq_mask(host, host->irq_mask);
	spin_unlock_irqrestore(&host->lock, flags);
}

static void x1000_mmc_clock_enable(struct x1000_mmc_host *host,
	bool start_transfer)
{
	uint16_t val = JZ_MMC_STRPCL_CLOCK_START;

	if (start_transfer)
		val |= JZ_MMC_STRPCL_START_OP;

	writew(val, host->base + JZ_REG_MMC_STRPCL);
}

static void x1000_mmc_clock_disable(struct x1000_mmc_host *host)
{
	uint32_t status;
	unsigned int timeout = 1000;

	writew(JZ_MMC_STRPCL_CLOCK_STOP, host->base + JZ_REG_MMC_STRPCL);
	do {
		status = readl(host->base + JZ_REG_MMC_STATUS);
	} while (status & JZ_MMC_STATUS_CLK_EN && --timeout);
}

static void x1000_mmc_reset(struct x1000_mmc_host *host)
{
	uint32_t status;
	unsigned int timeout = 1000;

	writew(JZ_MMC_STRPCL_RESET, host->base + JZ_REG_MMC_STRPCL);
	udelay(10);
	do {
		status = readl(host->base + JZ_REG_MMC_STATUS);
	} while (status & JZ_MMC_STATUS_IS_RESETTING && --timeout);
}

static void x1000_mmc_finish_request(struct x1000_mmc_host *host)
{
	struct mmc_request *req;

	req = host->req;
	host->req = NULL;

	mmc_request_done(host->mmc, req);
}

static void x1000_mmc_dma_callback(void *param)
{
	struct x1000_mmc_host *host = param;
	struct mmc_data *data;

	bool req_in_progress, dma_in_progress;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	req_in_progress = host->req_in_progress;
	dma_in_progress = host->dma_in_progress;
	host->dma_in_progress = 0;
	data = host->req->data;
	spin_unlock_irqrestore(&host->lock, flags);

	if(!dma_in_progress)
		/* dma cleanup has been done already in an error handler */
		return;

	if (data && data->host_cookie == COOKIE_MAPPED)
		x1000_mmc_dma_unmap(host, data);

	/* Do not complete the operation if request is still in progress */
	if (req_in_progress)
		return;

	x1000_mmc_finish_request(host);
}

static void x1000_mmc_request_done(struct x1000_mmc_host *host)
{
	bool dma_in_progress, have_data;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	host->req_in_progress = 0;
	dma_in_progress = host->dma_in_progress;
	have_data = host->req->data != NULL;
	spin_unlock_irqrestore(&host->lock, flags);

	/* Do not complete the operation if DMA is still in progress */
	if (have_data && dma_in_progress)
		return;

	BUG_ON(!have_data && dma_in_progress);

	x1000_mmc_finish_request(host);
}

static unsigned int x1000_mmc_poll_irq(struct x1000_mmc_host *host,
	unsigned int irq)
{
	unsigned int timeout = 8;
	uint32_t status;

	do {
		status = x1000_mmc_read_irq_reg(host);
	} while (!(status & irq) && --timeout);

	if (timeout == 0) {
		set_bit(0, &host->waiting);
		mod_timer(&host->timeout_timer,
			  jiffies + msecs_to_jiffies(JZ_MMC_REQ_TIMEOUT_MS));
		x1000_mmc_set_irq_enabled(host, irq, true);
		return true;
	}

	return false;
}

static void x1000_mmc_transfer_check_state(struct x1000_mmc_host *host,
	struct mmc_data *data)
{
	int status;

	status = readl(host->base + JZ_REG_MMC_STATUS);
	if (status & JZ_MMC_STATUS_WRITE_ERROR_MASK) {
		if (status & (JZ_MMC_STATUS_TIMEOUT_WRITE)) {
			host->req->cmd->error = -ETIMEDOUT;
			data->error = -ETIMEDOUT;
		} else {
			host->req->cmd->error = -EIO;
			data->error = -EIO;
		}
	} else if (status & JZ_MMC_STATUS_READ_ERROR_MASK) {
		if (status & (JZ_MMC_STATUS_TIMEOUT_READ)) {
			host->req->cmd->error = -ETIMEDOUT;
			data->error = -ETIMEDOUT;
		} else {
			host->req->cmd->error = -EIO;
			data->error = -EIO;
		}
	}
}

static void x1000_mmc_timeout(struct timer_list *t)
{
	struct x1000_mmc_host *host = from_timer(host, t, timeout_timer);

	if (!test_and_clear_bit(0, &host->waiting))
		return;

	x1000_mmc_set_irq_enabled(host, JZ_MMC_IRQ_END_CMD_RES, false);

	host->req->cmd->error = -ETIMEDOUT;

	x1000_mmc_dma_callback(host);
	x1000_mmc_request_done(host);
}

static void x1000_mmc_read_response(struct x1000_mmc_host *host,
	struct mmc_command *cmd)
{
	int i;
	uint16_t tmp;
	void __iomem *fifo_addr = host->base + JZ_REG_MMC_RESP_FIFO;

	if (cmd->flags & MMC_RSP_136) {
		tmp = readw(fifo_addr);
		for (i = 0; i < 4; ++i) {
			cmd->resp[i] = tmp << 24;
			tmp = readw(fifo_addr);
			cmd->resp[i] |= tmp << 8;
			tmp = readw(fifo_addr);
			cmd->resp[i] |= tmp >> 8;
		}
	} else {
		cmd->resp[0] = readw(fifo_addr) << 24;
		cmd->resp[0] |= readw(fifo_addr) << 8;
		cmd->resp[0] |= readw(fifo_addr) & 0xff;
	}
}

static void x1000_mmc_send_command(struct x1000_mmc_host *host,
	struct mmc_command *cmd)
{
	uint32_t cmdat = host->cmdat;
	unsigned long flags;

	host->cmdat &= ~JZ_MMC_CMDAT_INIT;
	x1000_mmc_clock_disable(host);

	host->cmd = cmd;

	spin_lock_irqsave(&host->lock, flags);
	host->req_in_progress = 1;
	spin_unlock_irqrestore(&host->lock, flags);

	if (cmd->flags & MMC_RSP_BUSY)
		cmdat |= JZ_MMC_CMDAT_BUSY;

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_R1B:
	case MMC_RSP_R1:
		cmdat |= JZ_MMC_CMDAT_RSP_R1;
		break;
	case MMC_RSP_R2:
		cmdat |= JZ_MMC_CMDAT_RSP_R2;
		break;
	case MMC_RSP_R3:
		cmdat |= JZ_MMC_CMDAT_RSP_R3;
		break;
	default:
		break;
	}

	if (cmd->data) {
		cmdat |= JZ_MMC_CMDAT_DATA_EN;
		if (cmd->data->flags & MMC_DATA_WRITE)
			cmdat |= JZ_MMC_CMDAT_WRITE;
		writel(JZ_MMC_DMAC_DMA_EN | JZ_MMC_DMAC_DMA_SEL,
		       host->base + JZ_REG_MMC_DMAC);

		writew(cmd->data->blksz, host->base + JZ_REG_MMC_BLKLEN);
		writew(cmd->data->blocks, host->base + JZ_REG_MMC_NOB);
	}

	writeb(cmd->opcode, host->base + JZ_REG_MMC_CMD);
	writel(cmd->arg, host->base + JZ_REG_MMC_ARG);
	writel(cmdat, host->base + JZ_REG_MMC_CMDAT);

	x1000_mmc_clock_enable(host, 1);
}

static void jz_mmc_prepare_data_transfer(struct x1000_mmc_host *host)
{
	struct mmc_command *cmd = host->req->cmd;
	struct mmc_data *data = cmd->data;
	int direction;

	if (data->flags & MMC_DATA_READ)
		direction = SG_MITER_TO_SG;
	else
		direction = SG_MITER_FROM_SG;

	sg_miter_start(&host->miter, data->sg, data->sg_len, direction);
}


static irqreturn_t jz_mmc_irq_worker(int irq, void *devid)
{
	struct x1000_mmc_host *host = (struct x1000_mmc_host *)devid;
	struct mmc_command *cmd = host->req->cmd;
	struct mmc_request *req = host->req;
	struct mmc_data *data = cmd->data;
	bool timeout = false;

	if (cmd->error)
		host->state = X1000_MMC_STATE_DONE;

	switch (host->state) {
	case X1000_MMC_STATE_READ_RESPONSE:
		if (cmd->flags & MMC_RSP_PRESENT)
			x1000_mmc_read_response(host, cmd);

		if (!data)
			break;

		jz_mmc_prepare_data_transfer(host);
		/* fall through */

	case X1000_MMC_STATE_TRANSFER_DATA:
		/* Data transfer direction is defined later by
		 * relying on data flags in
		 * x1000_mmc_prepare_dma_data() and
		 * x1000_mmc_start_dma_transfer().
		 */
		timeout = x1000_mmc_start_dma_transfer(host, data);
		data->bytes_xfered = data->blocks * data->blksz;

		if (unlikely(timeout)) {
			host->state = X1000_MMC_STATE_TRANSFER_DATA;
			break;
		}

		x1000_mmc_transfer_check_state(host, data);

		timeout = x1000_mmc_poll_irq(host, JZ_MMC_IRQ_DATA_TRAN_DONE);
		if (unlikely(timeout)) {
			host->state = X1000_MMC_STATE_SEND_STOP;
			break;
		}
		x1000_mmc_write_irq_reg(host, JZ_MMC_IRQ_DATA_TRAN_DONE);
		/* fall through */

	case X1000_MMC_STATE_SEND_STOP:
		if (!req->stop)
			break;

		x1000_mmc_send_command(host, req->stop);

		if (mmc_resp_type(req->stop) & MMC_RSP_BUSY) {
			timeout = x1000_mmc_poll_irq(host,
						      JZ_MMC_IRQ_PRG_DONE);
			if (timeout) {
				host->state = X1000_MMC_STATE_DONE;
				break;
			}
		}
	case X1000_MMC_STATE_DONE:
		break;
	}

	if (!timeout)
		x1000_mmc_request_done(host);

	return IRQ_HANDLED;
}

static irqreturn_t jz_mmc_irq(int irq, void *devid)
{
	struct x1000_mmc_host *host = devid;
	struct mmc_command *cmd = host->cmd;
	uint32_t irq_reg, status, tmp;

	status = readl(host->base + JZ_REG_MMC_STATUS);
	irq_reg = x1000_mmc_read_irq_reg(host);

	tmp = irq_reg;
	irq_reg &= ~host->irq_mask;

	tmp &= ~(JZ_MMC_IRQ_TXFIFO_WR_REQ | JZ_MMC_IRQ_RXFIFO_RD_REQ |
		JZ_MMC_IRQ_PRG_DONE | JZ_MMC_IRQ_DATA_TRAN_DONE);

	if (tmp != irq_reg)
		x1000_mmc_write_irq_reg(host, tmp & ~irq_reg);

	if (irq_reg & JZ_MMC_IRQ_SDIO) {
		x1000_mmc_write_irq_reg(host, JZ_MMC_IRQ_SDIO);
		mmc_signal_sdio_irq(host->mmc);
		irq_reg &= ~JZ_MMC_IRQ_SDIO;
	}

	if (host->req && cmd && irq_reg) {
		if (test_and_clear_bit(0, &host->waiting)) {
			del_timer(&host->timeout_timer);

			if (status & JZ_MMC_STATUS_TIMEOUT_RES) {
				cmd->error = -ETIMEDOUT;
			} else if (status & JZ_MMC_STATUS_CRC_RES_ERR) {
				cmd->error = -EIO;
			} else if (status & (JZ_MMC_STATUS_CRC_READ_ERROR |
				    JZ_MMC_STATUS_CRC_WRITE_ERROR)) {
				if (cmd->data)
					cmd->data->error = -EIO;
				cmd->error = -EIO;
			}

			x1000_mmc_set_irq_enabled(host, irq_reg, false);
			x1000_mmc_write_irq_reg(host, irq_reg);

			return IRQ_WAKE_THREAD;
		}
	}

	return IRQ_HANDLED;
}

static int x1000_mmc_set_clock_rate(struct x1000_mmc_host *host, int rate)
{
	int div = 0;
	int real_rate;

	x1000_mmc_clock_disable(host);

	/*
	 * About the internal clock divider, the programming manual says:
	 * > This field must be set to 0 when the controller works during
	 * > normal writing or reading.
	 *
	 * It is not entirely clear what "normal" actually means. We suppose
	 * it is OK to use the divider only during the ID phase, then it
	 * should be avoided.
	 */

	if (rate < 2000000) {
		/* Input at 24Mhz */
		clk_set_rate(host->clk, 24000000);
		real_rate = clk_get_rate(host->clk);

		while (real_rate > rate && div < 7) {
			++div;
			real_rate >>= 1;
		}
	} else {
		clk_set_rate(host->clk, rate);
		real_rate = clk_get_rate(host->clk);
	}

	writew(div, host->base + JZ_REG_MMC_CLKRT);

	if (rate > 25000000) {
		/*
		 * We should probably use the timing information instead of the clock.
		 * While the vendor seems to do something similar, this is suspect
		 */
		dev_info(mmc_dev(host->mmc), "using high-speed timing settings");
		writel(JZ_MMC_LPM_DRV_RISING_QTR_PHASE_DLY |
			   JZ_MMC_LPM_SMP_RISING_QTR_OR_HALF_PHASE_DLY |
			   JZ_MMC_LPM_LOW_POWER_MODE_EN,
			   host->base + JZ_REG_MMC_LPM);

	} else {
		/* Care to reset ??? */
		writel(0, host->base + JZ_REG_MMC_LPM);
	}

	return real_rate;
}

static void x1000_mmc_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct x1000_mmc_host *host = mmc_priv(mmc);

	host->req = req;

	x1000_mmc_write_irq_reg(host, ~0);
	x1000_mmc_set_irq_enabled(host, JZ_MMC_IRQ_END_CMD_RES, true);

	host->state = X1000_MMC_STATE_READ_RESPONSE;
	set_bit(0, &host->waiting);
	mod_timer(&host->timeout_timer,
		  jiffies + msecs_to_jiffies(JZ_MMC_REQ_TIMEOUT_MS));
	x1000_mmc_send_command(host, req->cmd);
}

static void x1000_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct x1000_mmc_host *host = mmc_priv(mmc);
	if (ios->clock)
		x1000_mmc_set_clock_rate(host, ios->clock);

	switch (ios->power_mode) {
	case MMC_POWER_UP:
		x1000_mmc_reset(host);
		if (!IS_ERR(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, ios->vdd);
		host->cmdat |= JZ_MMC_CMDAT_INIT;
		clk_prepare_enable(host->clk);
		break;
	case MMC_POWER_ON:
		break;
	default:
		if (!IS_ERR(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);
		clk_disable_unprepare(host->clk);
		break;
	}

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		host->cmdat &= ~JZ_MMC_CMDAT_BUS_WIDTH_MASK;
		break;
	case MMC_BUS_WIDTH_4:
		host->cmdat &= ~JZ_MMC_CMDAT_BUS_WIDTH_MASK;
		host->cmdat |= JZ_MMC_CMDAT_BUS_WIDTH_4BIT;
		break;
	case MMC_BUS_WIDTH_8:
		host->cmdat &= ~JZ_MMC_CMDAT_BUS_WIDTH_MASK;
		host->cmdat |= JZ_MMC_CMDAT_BUS_WIDTH_8BIT;
		break;
	default:
		break;
	}
}

static void x1000_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct x1000_mmc_host *host = mmc_priv(mmc);
	x1000_mmc_set_irq_enabled(host, JZ_MMC_IRQ_SDIO, enable);
}

static const struct mmc_host_ops x1000_mmc_ops = {
	.request	= x1000_mmc_request,
	.pre_req	= x1000_mmc_pre_request,
	.post_req	= x1000_mmc_post_request,
	.set_ios	= x1000_mmc_set_ios,
	.get_ro		= mmc_gpio_get_ro,
	.get_cd		= mmc_gpio_get_cd,
	.enable_sdio_irq = x1000_mmc_enable_sdio_irq,
};

static const struct of_device_id x1000_mmc_of_match[] = {
	{ .compatible = "ingenic,x1000-mmc-dma", .data = NULL },
	{},
};
MODULE_DEVICE_TABLE(of, x1000_mmc_of_match);

static int x1000_mmc_probe(struct platform_device* pdev)
{
	int ret;
	struct mmc_host *mmc;
	struct x1000_mmc_host *host;

	mmc = mmc_alloc_host(sizeof(struct x1000_mmc_host), &pdev->dev);
	if (!mmc) {
		dev_err(&pdev->dev, "Failed to alloc mmc host structure\n");
		return -ENOMEM;
	}

	host = mmc_priv(mmc);

	ret = mmc_of_parse(mmc);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"could not parse device properties: %d\n", ret);
		goto err_free_host;
	}

	mmc_regulator_get_supply(mmc);

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0) {
		ret = host->irq;
		goto err_free_host;
	}

	host->clk = devm_clk_get(&pdev->dev, "mmc");
	if (IS_ERR(host->clk)) {
		ret = PTR_ERR(host->clk);
		dev_err(&pdev->dev, "Failed to get mmc clock\n");
		goto err_free_host;
	}

	host->mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->base = devm_ioremap_resource(&pdev->dev, host->mem_res);
	if (IS_ERR(host->base)) {
		ret = PTR_ERR(host->base);
		dev_err(&pdev->dev, "Failed to ioremap base memory\n");
		goto err_free_host;
	}

	mmc->ops = &x1000_mmc_ops;
	if (!mmc->f_max)
		mmc->f_max = JZ_MMC_CLK_RATE;
	mmc->f_min = mmc->f_max / 128;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	/*
	 * We use a fixed timeout of 5s, hence inform the core about it. A
	 * future improvement should instead respect the cmd->busy_timeout.
	 */
	mmc->max_busy_timeout = JZ_MMC_REQ_TIMEOUT_MS;

	mmc->max_blk_size = (1 << 10) - 1;
	mmc->max_blk_count = (1 << 15) - 1;

	/* HACK:
	 * this is a very small system - let's limit the allocation of the mmc
	 * 32MB is our whole RAM so it is a bit too much
	 */
	mmc->max_req_size = min_t(unsigned int, mmc->max_blk_size * mmc->max_blk_count,
				  64 * PAGE_SIZE);

	mmc->max_segs = 128;
	mmc->max_seg_size = mmc->max_req_size;

	host->mmc = mmc;
	host->pdev = pdev;
	spin_lock_init(&host->lock);
	host->irq_mask = ~0;

	x1000_mmc_reset(host);

	ret = request_threaded_irq(host->irq, jz_mmc_irq, jz_mmc_irq_worker, 0,
			dev_name(&pdev->dev), host);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", ret);
		goto err_free_host;
	}

	x1000_mmc_clock_disable(host);
	timer_setup(&host->timeout_timer, x1000_mmc_timeout, 0);

	// this driver only supports DMA
	ret = x1000_mmc_acquire_dma_channels(host);
	if (ret)
		goto err_free_irq;

	platform_set_drvdata(pdev, host);
	ret = mmc_add_host(mmc);

	if (ret) {
		dev_err(&pdev->dev, "Failed to add mmc host: %d\n", ret);
		goto err_release_dma;
	}
	dev_info(&pdev->dev, "Ingenic/ML SD/MMC card driver registered\n");

	dev_info(&pdev->dev, "Using DMA, %d-bit mode, caps: %08X | %08X\n",
		 (mmc->caps & MMC_CAP_8_BIT_DATA) ? 8 :
		 ((mmc->caps & MMC_CAP_4_BIT_DATA) ? 4 : 1),
		 mmc->caps, mmc->caps2);

	return 0;

err_release_dma:
	x1000_mmc_release_dma_channels(host);
err_free_irq:
	free_irq(host->irq, host);
err_free_host:
	mmc_free_host(mmc);

	return ret;
}

static int x1000_mmc_remove(struct platform_device *pdev)
{
	struct x1000_mmc_host *host = platform_get_drvdata(pdev);

	del_timer_sync(&host->timeout_timer);
	x1000_mmc_set_irq_enabled(host, 0xff, false);
	x1000_mmc_reset(host);

	mmc_remove_host(host->mmc);

	free_irq(host->irq, host);

	x1000_mmc_release_dma_channels(host);

	mmc_free_host(host->mmc);

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int x1000_mmc_suspend(struct device *dev)
{
	return pinctrl_pm_select_sleep_state(dev);
}

static int x1000_mmc_resume(struct device *dev)
{
	return pinctrl_select_default_state(dev);
}

static SIMPLE_DEV_PM_OPS(x1000_mmc_pm_ops, x1000_mmc_suspend,
	x1000_mmc_resume);
#define X1000_MMC_PM_OPS (&x1000_mmc_pm_ops)
#else
#define X1000_MMC_PM_OPS NULL
#endif

static struct platform_driver x1000_mmc_driver = {
	.probe = x1000_mmc_probe,
	.remove = x1000_mmc_remove,
	.driver = {
		.name = "x1000-ml-mmc",
		.of_match_table = of_match_ptr(x1000_mmc_of_match),
		.pm = X1000_MMC_PM_OPS,
	},
};

module_platform_driver(x1000_mmc_driver);

MODULE_DESCRIPTION("X1000 SD/MMC controller driver [DMA]");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_AUTHOR("Pieter Palmers <pieter.palmers@pentasono.com>");
