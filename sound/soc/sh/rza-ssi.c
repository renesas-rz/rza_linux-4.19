// SPDX-License-Identifier: GPL-2.0
/*
 * Serial Sound Interface (SSIF) support for RZ/A SoCs
 * Copyright (C) 2019 Renesas Electronics Corporation
 * Copyright (C) 2019 Chris Brandt
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/dmaengine.h>
#include <sound/soc.h>

/* SSIF/SSIF-2 REGISTER OFFSET */
#define SSICR                   0x000
#define SSISR                   0x004
#define SSIFCR                  0x010
#define SSIFSR                  0x014
#define SSIFTDR                 0x018
#define SSIFRDR                 0x01C
#define SSIOFR                  0x020
#define SSISCR                  0x024

/* SSI REGISTER BITS */
#define SSICR_DWL(x)            (((x) & 0x7) << 19)
#define SSICR_SWL(x)            (((x) & 0x7) << 16)
#define SSICR_MST               (1 << 14)
#define SSICR_CKDV(x)           (((x) & 0xf) << 4)

#define SSICR_CKS		(1 << 30)
#define SSICR_TUIEN             (1 << 29)
#define SSICR_TOIEN             (1 << 28)
#define SSICR_RUIEN             (1 << 27)
#define SSICR_ROIEN             (1 << 26)
#define SSICR_IIEN              (1 << 25)
#define SSICR_FRM
#define SSICR_MST               (1 << 14)
#define SSICR_BCKP		(1 << 13)
#define SSICR_LRCKP		(1 << 12)
#define SSICR_CKDV(x)           (((x) & 0xf) << 4)
#define SSICR_MUEN              (1 << 3)
#define SSICR_TEN               (1 << 1)
#define SSICR_REN               (1 << 0)

#define SSISR_TUIRQ		(1 << 29)
#define SSISR_TOIRQ		(1 << 28)
#define SSISR_RUIRQ		(1 << 27)
#define SSISR_ROIRQ		(1 << 26)
#define SSISR_IIRQ		(1 << 25)

#define SSIFCR_AUCKE            (1 << 31)
#define SSIFCR_SSIRST           (1 << 16)
#define SSIFCR_TIE              (1 << 3)
#define SSIFCR_RIE              (1 << 2)
#define SSIFCR_TFRST            (1 << 1)
#define SSIFCR_RFRST            (1 << 0)

#define	SSIFSR_TDC_MASK		0x3f
#define	SSIFSR_TDC_SHIFT	24
#define	SSIFSR_RDC_MASK		0x3f
#define	SSIFSR_RDC_SHIFT	8

#define	SSIFSR_TDC(x)		(((x) & 0x1f) << 24)
#define	SSIFSR_TDE		(1 << 16)
#define	SSIFSR_RDC(x)		(((x) & 0x1f) << 8)
#define	SSIFSR_RDF		(1 << 0)

#define SSIOFR_BCKASTP		(1 << 9)
#define SSIOFR_LRCONT		(1 << 8)
#define	SSIOFR_OMOD_I2S		(0 << 0)
#define	SSIOFR_OMOD_TDM		(1 << 0)
#define	SSIOFR_OMOD_MONO	(2 << 0)

#define	SSISCR_TDES(x)		(((x) & 0x1f) << 8)
#define	SSISCR_RDFS(x)		(((x) & 0x1f) << 0)

/* Pre allocated buffers sizes */
#define PREALLOC_BUFFER		(32 * 1024)
#define PREALLOC_BUFFER_MAX	(32 * 1024)

#define SSI_RATES	SNDRV_PCM_RATE_8000_48000	/* 8k-44.1kHz */
#define SSI_FMTS	SNDRV_PCM_FMTBIT_S16_LE
#define SSI_CHAN_MIN	2
#define SSI_CHAN_MAX	2

struct ssi_priv;

struct ssi_stream {
	struct ssi_priv *priv;
	struct snd_pcm_substream *substream;
	int fifo_sample_size;	/* sample capacity of SSI FIFO */
	int buffer_pos;		/* current frame position in the buffer */
	int period_counter;	/* for keeping track of how many periods were transfered */
	int dma_buffer_pos;	/* The address for the next DMA descriptor */
	int running;		/* 0=stopped, 1=running */

	int sample_width;	/* sample width */
	int uerr_num;
	int oerr_num;

	struct dma_chan		*dma_ch;

	int (*transfer)(struct ssi_priv *ssi, struct ssi_stream *strm);
};

struct ssi_data {
	int ver;
	int fifo_sample_size;		/* fifo depth */
};

struct ssi_priv {
	void __iomem *base;
	struct platform_device *pdev;
	struct device *dev;
	const struct ssi_data *data;

	phys_addr_t phys;
	int irq_int;		/* int_req */
	int irq_tx;		/* dma_tx */
	int irq_rx;		/* dma_rx */

	spinlock_t lock;

	/* TODO
	 * The RZ/A2 SSI supports full-duplex transmission and reception.
	 * However, if an error occurs, channel reset (both transmission
	 * and reception  reset) is required.
	 * So it is better to use as half-duplex (playing and recording
	 * should be done on separate channels).
	 */
	struct ssi_stream playback;
	struct ssi_stream capture;

	/* clock */
	unsigned long audio_mck;
	int use_audio_x1;

	int chan_num;			/* Number of channels */
	unsigned int bckp:1;		/* Bit clock polarity (SSICR.BCKP) */
	unsigned int lrckp:1;		/* LR clock polarity (SSICR.LRCKP) */
	unsigned int dma_enabled:1;
};

static void ssi_reg_writel(struct ssi_priv __iomem *priv, uint reg, u32 data)
{
	writel(data, (priv->base + reg));
}

static u32 ssi_reg_readl(struct ssi_priv __iomem *priv, uint reg)
{
	u32 data = readl(priv->base + reg);
	return data;
}

static void ssi_reg_mask_setl(struct ssi_priv __iomem *priv, uint reg, u32 bclr, u32 bset)
{
	u32 val;

	val = readl(priv->base + reg);
	val = (val & ~bclr) | bset;
	writel(val, (priv->base + reg));
}

static inline struct snd_soc_dai *ssi_get_dai(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	return rtd->cpu_dai;
}

static inline int ssi_stream_is_play(struct ssi_priv *ssi,
				     struct ssi_stream *strm)
{
	return strm->substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
}

static inline struct ssi_stream *ssi_stream_get(struct ssi_priv *ssi,
					struct snd_pcm_substream *substream)
{
	return (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? &ssi->playback : &ssi->capture;
}

static int ssi_stream_is_valid(struct ssi_priv *ssi,
				 struct ssi_stream *strm)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ssi->lock, flags);
	ret = !!(strm->substream && strm->substream->runtime);
	spin_unlock_irqrestore(&ssi->lock, flags);

	return ret;
}

static int ssi_stream_init(struct ssi_priv *ssi,
			    struct ssi_stream *strm,
			    struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	strm->substream = substream;

	strm->buffer_pos = 0;
	strm->dma_buffer_pos = 0;
	strm->period_counter = 0;
	strm->sample_width	= samples_to_bytes(runtime, 1);
	strm->oerr_num	= -1; /* ignore 1st err */
	strm->uerr_num	= -1; /* ignore 1st err */
	strm->running = 0;

	/* fifo init */
	strm->fifo_sample_size = ssi->data->fifo_sample_size;

	if (runtime->sample_bits != 16) {
		dev_err(ssi->dev, "Unsupported sample width: %d\n", runtime->sample_bits);
		return -EINVAL;
	}

	if (runtime->frame_bits != 32) {
		dev_err(ssi->dev, "Unsupported frame width: %d\n", runtime->sample_bits);
		return -EINVAL;
	}

	if (runtime->channels != 2) {
		dev_err(ssi->dev, "Number of channels not matched\n");
		return -EINVAL;
	}

	/* print out some info */
	dev_dbg(ssi->dev, "%s: buffer_size=%d, period_size=%d, periods=%d\n",
		 __func__,
		(unsigned int)runtime->buffer_size,
		(unsigned int)runtime->period_size,
		(unsigned int)runtime->periods);

	return 0;
}

static void ssi_stream_quit(struct ssi_priv *ssi, struct ssi_stream *strm)
{
	struct snd_soc_dai *dai = ssi_get_dai(strm->substream);

	if (strm->oerr_num > 0)
		dev_info(dai->dev, "overrun = %d\n", strm->oerr_num);

	if (strm->uerr_num > 0)
		dev_info(dai->dev, "underrun = %d\n", strm->uerr_num);

	strm->substream	= NULL;
	strm->buffer_pos = 0;
	strm->period_counter = 0;
	strm->oerr_num	= 0;
	strm->uerr_num	= 0;
}

static int ssi_clk_setup(struct ssi_priv *ssi, unsigned int rate, unsigned int channels)
{
	static s8 ckdv[16] = { 1,  2,  4,  8, 16, 32, 64, 128,
			       6, 12, 24, 48, 96, -1, -1, -1 };
	unsigned long bclk_rate;
	unsigned int div;
	u32 clk_ckdv;
	unsigned int channel_bits = 32;	/* System Word Length */
	u32 ssicr = 0;
	int i;

	/* Clear AUCKE so we can set MST */
	ssi_reg_writel(ssi, SSIFCR, 0);

	/* Continue to output LRCK pin even when idle */
	ssi_reg_writel(ssi, SSIOFR, SSIOFR_LRCONT);

	/* Clock setting */
	ssicr |= SSICR_MST;
	if (ssi->use_audio_x1)
		ssicr |= SSICR_CKS;
	if (ssi->bckp)
		ssicr |= SSICR_BCKP;
	if (ssi->lrckp)
		ssicr |= SSICR_LRCKP;

	/* Determine the clock divider */
	bclk_rate = rate * channels * channel_bits;
	clk_ckdv = 0;
	div = ssi->audio_mck / bclk_rate;
	/* try to find an match */
	for (i = 0; i < ARRAY_SIZE(ckdv); i++) {
		if (ckdv[i] == div) {
			clk_ckdv = i;
			break;
		}
	}
	if (i == ARRAY_SIZE(ckdv)) {
		dev_err(ssi->dev, "Error: Sample rate not divisible by audio clock source\n");
#ifdef DEBUG
		{
			char msg[128];
			int index = sprintf(msg, "Supported (stereo) rates are: ");

			for (i = 0; i < 13; i++)
				index += sprintf(msg+index, "%ld ", (ssi->audio_mck / ckdv[i]) / 32 / 2);
			index = sprintf(msg+index, "\n");
			dev_err(ssi->dev, msg);
		}
#endif
		return -EINVAL;
	}
	ssicr |= SSICR_CKDV(clk_ckdv);

	/* DWL: Data Word Length = 16 bits
	 * SWL: System Word Length = 32 bits
	 */
	ssicr |= SSICR_DWL(1) | SSICR_SWL(3);

	ssi_reg_writel(ssi, SSICR, ssicr);
	ssi_reg_writel(ssi, SSIFCR, (SSIFCR_AUCKE | SSIFCR_TFRST | SSIFCR_RFRST));

	return 0;
}

static int ssi_start_stop(struct ssi_priv *ssi, struct ssi_stream *strm,
			       int start)
{
	u32 ssicr, ssifcr;
	int tmout;

	if (start) {

		ssicr = ssi_reg_readl(ssi, SSICR);
		ssifcr = ssi_reg_readl(ssi, SSIFCR) & ~0xF;

		/* FIFO interrupt thresholds */
		if (ssi->dma_enabled)
			ssi_reg_writel(ssi, SSISCR, 0);
		else
			ssi_reg_writel(ssi, SSISCR, SSISCR_TDES((strm->fifo_sample_size)/2 - 1) | SSISCR_RDFS(0));

		/* enable IRQ */
		if (ssi_stream_is_play(ssi, strm)) {
			ssicr |= SSICR_TUIEN | SSICR_TOIEN;
			ssifcr |= SSIFCR_TIE | SSIFCR_RFRST;
		} else {
			ssicr |= SSICR_RUIEN | SSICR_ROIEN;
			ssifcr |= SSIFCR_RIE | SSIFCR_TFRST;
		}
		ssi_reg_writel(ssi, SSICR, ssicr);
		ssi_reg_writel(ssi, SSIFCR, ssifcr);

		/* Clear all error flags */
		ssi_reg_mask_setl(ssi, SSISR, (SSISR_TOIRQ | SSISR_TUIRQ | SSISR_ROIRQ | SSISR_RUIRQ), 0);

		strm->running = 1;

		if (ssi_stream_is_play(ssi, strm))
			ssicr |= SSICR_TEN;
		else
			ssicr |= SSICR_REN;
		ssi_reg_writel(ssi, SSICR, ssicr);

	} else {
		strm->running = 0;

		/* Cancel all remaining DMA transactions */
		if (ssi->dma_enabled)
			dmaengine_terminate_all(strm->dma_ch);

		/* Disable irqs */
		ssi_reg_mask_setl(ssi, SSICR, SSICR_TUIEN | SSICR_TOIEN | SSICR_RUIEN | SSICR_ROIEN, 0);
		ssi_reg_mask_setl(ssi, SSIFCR, SSIFCR_TIE | SSIFCR_RIE, 0);

		/* Clear all error flags */
		ssi_reg_mask_setl(ssi, SSISR, (SSISR_TOIRQ | SSISR_TUIRQ | SSISR_ROIRQ | SSISR_RUIRQ), 0);

		/* Wait for idle */
		tmout = 1000;
		while (--tmout) {
			if (ssi_reg_readl(ssi, SSISR) | SSISR_IIRQ)
				break;
			udelay(1);
		}
		if (!tmout)
			pr_info("timeout waiting for SSI idle\n");

		/* Disable TX/RX */
		ssi_reg_mask_setl(ssi, SSICR, SSICR_TEN | SSICR_REN, 0);

		/* Hold FIFOs in reset */
		ssi_reg_mask_setl(ssi, SSIFCR, 0, SSIFCR_TFRST | SSIFCR_RFRST);
	}

	return 0;
}

static void ssi_pointer_update(struct ssi_stream *strm, int frames)
{
	struct snd_pcm_runtime *runtime = strm->substream->runtime;
	int current_period;

	strm->buffer_pos += frames;

	WARN_ON(strm->buffer_pos > runtime->buffer_size);

	/* ring buffer */
	if (strm->buffer_pos == runtime->buffer_size)
		strm->buffer_pos = 0;

	current_period = strm->buffer_pos / runtime->period_size;
	if (strm->period_counter != current_period) {
		snd_pcm_period_elapsed(strm->substream);
		strm->period_counter = current_period;
	}
}

static int ssi_pio_recv(struct ssi_priv *ssi, struct ssi_stream *strm)
{
	struct snd_pcm_substream *substream = strm->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	u16 *buf;
	int i;
	int frames_left;
	int samples = 0;
	int fifo_samples;

	if (!ssi_stream_is_valid(ssi, strm))
		return -EINVAL;

	/* frames left in this period */
	frames_left = runtime->period_size - (strm->buffer_pos % runtime->period_size);
	if (frames_left == 0)
		frames_left = runtime->period_size;	/* new period */

	/* Samples in RX FIFO */
	fifo_samples = (ssi_reg_readl(ssi, SSIFSR) >> SSIFSR_RDC_SHIFT) & SSIFSR_RDC_MASK;

	/* Only read full frames at a time */
	while (frames_left && (fifo_samples >= runtime->channels)) {
		samples += runtime->channels;
		fifo_samples -= runtime->channels;
		frames_left--;
	}

	/* not enough samples yet */
	if (samples == 0)
		return 0;

	/* calculate new buffer index */
	buf = (u16 *)(strm->substream->runtime->dma_area);
	buf += strm->buffer_pos * runtime->channels;

	/* Note, only supports 16-bit samples */
	for (i = 0; i < samples; i++)
		*buf++ = (u16)(ssi_reg_readl(ssi, SSIFRDR) >> 16);

	ssi_reg_mask_setl(ssi, SSIFSR, SSIFSR_RDF, 0);

	ssi_pointer_update(strm, samples / runtime->channels);

	/* If we finished this period, but there are more samples in
	 * the RX FIFO, call this function again
	 */
	if ((frames_left == 0) && (fifo_samples >= runtime->channels))
		ssi_pio_recv(ssi, strm);

	return 0;
}

static int ssi_pio_send(struct ssi_priv *ssi, struct ssi_stream *strm)
{
	struct snd_pcm_substream *substream = strm->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int sample_space;
	int samples = 0;
	u16 *buf;
	int i;
	int frames_left;
	u32 ssifsr;

	if (!ssi_stream_is_valid(ssi, strm))
		return -EINVAL;

	/* frames left in this period */
	frames_left = runtime->period_size - (strm->buffer_pos % runtime->period_size);
	if (frames_left == 0)
		frames_left = runtime->period_size;	/* new period */

	sample_space = strm->fifo_sample_size;
	ssifsr = ssi_reg_readl(ssi, SSIFSR);
	sample_space -= (ssifsr >> SSIFSR_TDC_SHIFT) & SSIFSR_TDC_MASK;

	/* Only add full frames at a time */
	while (frames_left && (sample_space >= runtime->channels)) {
		samples += runtime->channels;
		sample_space -= runtime->channels;
		frames_left--;
	}

	/* no space to send anything right now */
	if (samples == 0)
		return 0;

	/* calculate new buffer index */
	buf = (u16 *)(strm->substream->runtime->dma_area);
	buf += strm->buffer_pos * runtime->channels;

	/* Note, only supports 16-bit samples */
	for (i = 0; i < samples; i++)
		ssi_reg_writel(ssi, SSIFTDR, ((u32)(*buf++) << 16));

	ssi_reg_mask_setl(ssi, SSIFSR, SSIFSR_TDE, 0);

	ssi_pointer_update(strm, samples / runtime->channels);

	return 0;
}

static irqreturn_t ssi_interrupt(int irq, void *data)
{
	struct ssi_priv *ssi = data;
	struct ssi_stream *strm = NULL;
	u32 ssisr = ssi_reg_readl(ssi, SSISR);

	if (ssi->playback.substream)
		strm = &ssi->playback;
	else if (ssi->capture.substream)
		strm = &ssi->capture;
	else
		return IRQ_HANDLED;	/* Left over TX/RX interrupt */

	if (irq == ssi->irq_int) {	/* error or idle */
		if (ssisr & SSISR_TUIRQ)
			strm->uerr_num++;
		if (ssisr & SSISR_TOIRQ)
			strm->oerr_num++;
		if (ssisr & SSISR_RUIRQ)
			strm->uerr_num++;
		if (ssisr & SSISR_ROIRQ)
			strm->oerr_num++;

		if (ssisr & (SSISR_TUIRQ | SSISR_TOIRQ | SSISR_RUIRQ | SSISR_ROIRQ)) {
			/* Error handling */
			/* You must reset (stop/restart) after each interrupt */
			ssi_start_stop(ssi, strm, 0);

			/* Clear all flags */
			ssi_reg_mask_setl(ssi, SSISR, (SSISR_TOIRQ | SSISR_TUIRQ | SSISR_ROIRQ | SSISR_RUIRQ), 0);

			/* Add/remove more data */
			strm->transfer(ssi, strm);

			/* Resume */
			ssi_start_stop(ssi, strm, 1);
		}
	}

	if (!strm->running)
		return IRQ_HANDLED;

	/* tx data empty */ /* rx data full if half-duplex */
	if (irq == ssi->irq_tx)
		strm->transfer(ssi, &ssi->playback);

	/* rx data full */
	if (irq == ssi->irq_rx) {
		strm->transfer(ssi, &ssi->capture);
		ssi_reg_mask_setl(ssi, SSIFSR, SSIFSR_RDF, 0);
	}

	return IRQ_HANDLED;
}

static int ssi_dma_transfer(struct ssi_priv *ssi, struct ssi_stream *strm);
static void ssi_dma_complete(void *data)
{
	struct ssi_stream *strm = (struct ssi_stream *)data;
	struct ssi_priv *ssi = strm->priv;

	if (strm->running) {
		/*
		 * Note that next DMA transaction has probably already
		 * started
		 */
		ssi_pointer_update(strm,
				   strm->substream->runtime->period_size
				   );

		/* Queue up another DMA transaction */
		ssi_dma_transfer(ssi, strm);
	}
}

static int ssi_dma_transfer(struct ssi_priv *ssi, struct ssi_stream *strm)
{
	int is_play;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	struct dma_async_tx_descriptor *desc;
	enum dma_transfer_direction dir;
	u32 dma_size;
	u32 dma_paddr;
	int amount;

	if (!ssi_stream_is_valid(ssi, strm))
		return -EINVAL;

	is_play = ssi_stream_is_play(ssi, strm);
	substream = strm->substream;
	runtime = substream->runtime;

	if (runtime->status->state == SNDRV_PCM_STATE_DRAINING) {
		/*
		 * Stream is ending, so do not queue up any more DMA
		 * transfers otherwise we play partial sound clips
		 * because we can't shut off the DMA quick enough.
		 */
		return 0;
	}

	if (is_play)
		dir = DMA_MEM_TO_DEV;
	else
		dir = DMA_DEV_TO_MEM;

	/* Always transfer 1 period */
	amount = runtime->period_size;

	/* DMA physical address and size */
	dma_paddr = runtime->dma_addr + frames_to_bytes(runtime, strm->dma_buffer_pos);
	dma_size = frames_to_bytes(runtime, amount);

	/* Update DMA pointer for next descriptor */
	strm->dma_buffer_pos += amount;
	if (strm->dma_buffer_pos >= runtime->buffer_size)
		strm->dma_buffer_pos = 0;

	desc = dmaengine_prep_slave_single(strm->dma_ch,
		dma_paddr,
		dma_size,
		dir, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dev_err(ssi->dev, "dmaengine_prep_slave_single() fail\n");
		return -ENOMEM;
	}

	desc->callback		= ssi_dma_complete;
	desc->callback_param	= strm;

	if (dmaengine_submit(desc) < 0) {
		dev_err(ssi->dev, "dmaengine_submit() fail\n");
		return -EIO;
	}

	/* Start DMA */
	dma_async_issue_pending(strm->dma_ch);

	return 0;
}

static int ssi_dma_request(struct ssi_priv *ssi, struct device *dev)
{
	struct dma_slave_config cfg;
	int ret;

	ssi->playback.dma_ch = dma_request_slave_channel(dev, "tx");
	ssi->capture.dma_ch = dma_request_slave_channel(dev, "rx");
	if (!ssi->playback.dma_ch || !ssi->capture.dma_ch)
		goto no_dma;

	memset(&cfg, 0, sizeof(cfg));

	/* tx */
	cfg.direction		= DMA_MEM_TO_DEV;
	cfg.dst_addr		= ssi->phys + SSIFTDR;
	cfg.dst_addr_width	= DMA_SLAVE_BUSWIDTH_2_BYTES;
	ret = dmaengine_slave_config(ssi->playback.dma_ch, &cfg);
	if (ret < 0)
		goto no_dma;

	/* rx */
	cfg.direction		= DMA_DEV_TO_MEM;
	cfg.src_addr		= ssi->phys + SSIFRDR;
	cfg.src_addr_width	= DMA_SLAVE_BUSWIDTH_2_BYTES;
	ret = dmaengine_slave_config(ssi->capture.dma_ch, &cfg);
	if (ret < 0)
		goto no_dma;

	return 0;

no_dma:
	if (ssi->playback.dma_ch) {
		dma_release_channel(ssi->playback.dma_ch);
		ssi->playback.dma_ch = NULL;
	}
	if (ssi->capture.dma_ch) {
		dma_release_channel(ssi->capture.dma_ch);
		ssi->capture.dma_ch = NULL;
	}
	return -ENODEV;
}

static int ssi_dai_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	return 0;
}

static void ssi_dai_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
}

static int ssi_dai_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct ssi_priv *ssi = snd_soc_dai_get_drvdata(dai);
	struct ssi_stream *strm = ssi_stream_get(ssi, substream);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* Soft Reset */
		ssi_reg_mask_setl(ssi, SSIFCR, 0, SSIFCR_SSIRST);
		udelay(100);
		ssi_reg_mask_setl(ssi, SSIFCR, SSIFCR_SSIRST, 0);
		udelay(100);

		ret = ssi_stream_init(ssi, strm, substream);

		if (!ret)
			ret = strm->transfer(ssi, strm);
		/* For DMA, queue up multiple DMA descriptors */
		if (!ret && ssi->dma_enabled)
			ret = strm->transfer(ssi, strm);
		if (!ret && ssi->dma_enabled)
			ret = strm->transfer(ssi, strm);
		if (!ret && ssi->dma_enabled)
			ret = strm->transfer(ssi, strm);
		if (!ret)
			ret = ssi_start_stop(ssi, strm, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		ssi_start_stop(ssi, strm, 0);
		ssi_stream_quit(ssi, strm);
		break;
	}

	return ret;
}


/* Called when codec is found and matched */
static int ssi_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct ssi_priv *ssi = snd_soc_dai_get_drvdata(dai);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		asm("nop");	/* codec is slave */
		break;
	default:
		dev_err(ssi->dev, "Only master mode is supported.\n");
		return -EINVAL;
	}

	/* set clock polarity */
	/*
	 * "normal" BCLK = Signal is available at rising edge of BCLK
	 * "normal" FSYNC = (I2S) Left ch starts with falling FSYNC edge
	 */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:	/* BCLK rise + FSYNC fall */
		ssi->bckp = 0;
		ssi->lrckp = 0;
		break;
	case SND_SOC_DAIFMT_NB_IF:	/* BCLK rise + FSYNC rise */
		ssi->bckp = 0;
		ssi->lrckp = 1;
		break;
	case SND_SOC_DAIFMT_IB_NF:	/* BCLK fall + FSYNC fall */
		ssi->bckp = 1;
		ssi->lrckp = 0;
		break;
	case SND_SOC_DAIFMT_IB_IF:	/* BCLK fall + FSYNC rise */
		ssi->bckp = 1;
		ssi->lrckp = 1;
		break;
	default:
		return -EINVAL;
	}

	/* only i2s support */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ssi->chan_num = 2;
		break;
	default:
		dev_err(ssi->dev, "Only I2S mode is supported.\n");
		return -EINVAL;
	}

	return 0;
}


/* Called when a new stream is to be played */
static int ssi_dai_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct ssi_priv *ssi = snd_soc_dai_get_drvdata(dai);

	if (ssi_clk_setup(ssi, params_rate(params), params_channels(params)))
		return -EINVAL;

	return 0;
}

static const struct snd_soc_dai_ops ssi_dai_ops = {
	.startup	= ssi_dai_startup,
	.shutdown	= ssi_dai_shutdown,
	.trigger	= ssi_dai_trigger,
	.set_fmt	= ssi_dai_set_fmt,
	.hw_params	= ssi_dai_hw_params,
};

/* This tells the upper layer our min and max values */
static struct snd_pcm_hardware ssi_pcm_hardware = {
	.info =		SNDRV_PCM_INFO_INTERLEAVED	|
			SNDRV_PCM_INFO_MMAP		|
			SNDRV_PCM_INFO_MMAP_VALID,
	.buffer_bytes_max	= PREALLOC_BUFFER,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.channels_min		= SSI_CHAN_MIN,
	.channels_max		= SSI_CHAN_MAX,
	.periods_min		= 1,
	.periods_max		= 32,
	.fifo_size		= 32*2,  /* in bytes (TODO, RZ/A1 is only 8) */
};

static int ssi_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;

	snd_soc_set_runtime_hwparams(substream, &ssi_pcm_hardware);

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);

	return ret;
}

/* called before stream gets triggered */
static int ssi_pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int ssi_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static snd_pcm_uframes_t ssi_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_dai *dai = ssi_get_dai(substream);
	struct ssi_priv *ssi = snd_soc_dai_get_drvdata(dai);
	struct ssi_stream *strm = ssi_stream_get(ssi, substream);

	return strm->buffer_pos;
}

static const struct snd_pcm_ops ssi_pcm_ops = {
	.open		= ssi_pcm_open,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= ssi_pcm_hw_params,
	.hw_free	= ssi_pcm_hw_free,
	.pointer	= ssi_pcm_pointer,
};

static int ssi_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	return snd_pcm_lib_preallocate_pages_for_all(
		rtd->pcm,
		SNDRV_DMA_TYPE_DEV,
		rtd->card->snd_card->dev,
		PREALLOC_BUFFER, PREALLOC_BUFFER_MAX);
}

static struct snd_soc_dai_driver ssi_soc_dai[] = {
	{
		.name			= "ssi-dai",
		.playback = {
			.rates		= SSI_RATES,
			.formats	= SSI_FMTS,
			.channels_min	= SSI_CHAN_MIN,
			.channels_max	= SSI_CHAN_MAX,
		},
		.capture = {
			.rates		= SSI_RATES,
			.formats	= SSI_FMTS,
			.channels_min	= SSI_CHAN_MIN,
			.channels_max	= SSI_CHAN_MAX,
		},
		.ops = &ssi_dai_ops,
	},
};

static const struct snd_soc_component_driver ssi_soc_component = {
	.name		= "ssi",
	.ops		= &ssi_pcm_ops,
	.pcm_new	= ssi_pcm_new,
};

static int ssi_parse_of(struct device_node *np, struct ssi_priv *ssi)
{
	u32 rate;

	/* get source clock (default is AUDIO_CLK) */
	rate = 0;
	of_property_read_u32(np, "audio_clk", &rate);
	if (!rate) {
		of_property_read_u32(np, "audio_x1", &rate);
		if (!rate)
			ssi->use_audio_x1 = 1;
	}
	ssi->audio_mck = rate;

	dev_info(&ssi->pdev->dev, "Using %s, %u Hz\n",
		(ssi->use_audio_x1 ? "audio_x1" : "audio_clk"),
		rate);

	return 0;
}

static const struct ssi_data ssi1_data = {
	.ver	= 1,
	.fifo_sample_size = 8,
};

static const struct ssi_data ssi2_data = {
	.ver	= 2,
	.fifo_sample_size = 32,
};

static int ssi_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct ssi_priv *ssi;
	const struct ssi_data *data;
	int ret;

	ssi = devm_kzalloc(&pdev->dev, sizeof(*ssi), GFP_KERNEL);
	if (!ssi)
		return -ENOMEM;
	ssi->pdev = pdev;
	ssi->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get SSI registers\n");
		return -EINVAL;
	}
	ssi->phys = res->start;
	ssi->base = devm_ioremap_nocache(&pdev->dev, res->start, resource_size(res));
	if (!ssi->base) {
		dev_err(&pdev->dev, "Unable to iomremap SSI registers\n");
		return -ENXIO;
	}

	ret = ssi_parse_of(pdev->dev.of_node, ssi);
	if (ret < 0)
		return ret;

	/* Detect DMA support */
	ret = ssi_dma_request(ssi, &pdev->dev);
	if (ret < 0) {
		dev_warn(&pdev->dev, "DMA not available, using PIO\n");
	} else {
		ssi->dma_enabled = 1;
		dev_info(&pdev->dev, "DMA enalbed");
	}

	/* Handlers (PIO or DMA) */
	if (ssi->dma_enabled) {
		/* DMA */
		ssi->playback.transfer		= ssi_dma_transfer;
		ssi->capture.transfer		= ssi_dma_transfer;
	} else {
		/* PIO */
		ssi->playback.transfer		= ssi_pio_send;
		ssi->capture.transfer		= ssi_pio_recv;

	}
	ssi->playback.priv = ssi;
	ssi->capture.priv = ssi;

	/* Error Interrupt */
	ssi->irq_int = platform_get_irq_byname(pdev, "int");
	if (ssi->irq_int < 0) {
		dev_err(&pdev->dev, "Unable to get SSI int_req IRQ\n");
		return -EINVAL;
	}
	ret = devm_request_irq(&pdev->dev, ssi->irq_int, &ssi_interrupt, 0, dev_name(&pdev->dev), ssi);
	if (ret < 0) {
		dev_err(&pdev->dev, "irq request error (int)\n");
		return ret;
	}

	/* TX,RX Interrupts (pio only) */
	if (!ssi->dma_enabled) {
		ssi->irq_tx = platform_get_irq_byname(pdev, "tx");
		if (ssi->irq_tx < 0) {
			dev_err(&pdev->dev, "Unable to get SSI dma_tx IRQ\n");
			return -EINVAL;
		}
		ssi->irq_rx = platform_get_irq_byname(pdev, "rx");
		if (ssi->irq_rx < 0) {
			dev_err(&pdev->dev, "Unable to get SSI dma_rx IRQ\n");
			return -EINVAL;
		}
	}
	if (ssi->irq_tx)
		ret = devm_request_irq(&pdev->dev, ssi->irq_tx, &ssi_interrupt, 0, dev_name(&pdev->dev), ssi);
	if (ret < 0) {
		dev_err(&pdev->dev, "irq request error (tx)\n");
		return ret;
	}
	if (ssi->irq_rx)
		ret = devm_request_irq(&pdev->dev, ssi->irq_rx, &ssi_interrupt, 0, dev_name(&pdev->dev), ssi);
	if (ret < 0) {
		dev_err(&pdev->dev, "irq request error (rx)\n");
		return ret;
	}

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "Unknown SSI device\n");
		return -ENODEV;
	}
	ssi->data = data;

	spin_lock_init(&ssi->lock);

	dev_set_drvdata(&pdev->dev, ssi);

	ret = devm_snd_soc_register_component(&pdev->dev, &ssi_soc_component,
				    ssi_soc_dai, ARRAY_SIZE(ssi_soc_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register snd component register\n");
		goto exit_ssi_probe;
	}

	return ret;

exit_ssi_probe:
	/* Release DMA */
	if (ssi->playback.dma_ch) {
		dma_release_channel(ssi->playback.dma_ch);
		ssi->playback.dma_ch = NULL;
	}
	if (ssi->capture.dma_ch) {
		dma_release_channel(ssi->capture.dma_ch);
		ssi->capture.dma_ch = NULL;
	}

	return ret;
}

static int ssi_remove(struct platform_device *pdev)
{
	struct ssi_priv *ssi;

	ssi = dev_get_drvdata(&pdev->dev);

	/* Release DMA */
	if (ssi->playback.dma_ch) {
		dma_release_channel(ssi->playback.dma_ch);
		ssi->playback.dma_ch = NULL;
	}
	if (ssi->capture.dma_ch) {
		dma_release_channel(ssi->capture.dma_ch);
		ssi->capture.dma_ch = NULL;
	}

	return 0;
}

static const struct of_device_id ssi_of_match[] = {
	{ .compatible = "renesas,rza1-ssi", .data = &ssi1_data },
	{ .compatible = "renesas,rza2-ssi", .data = &ssi2_data },
	{},
};
MODULE_DEVICE_TABLE(of, ssi_of_match);

static struct platform_driver ssi_driver = {
	.driver		= {
		.name	= "ssi-pcm-audio",
		.of_match_table = ssi_of_match,
	},
	.probe		= ssi_probe,
	.remove		= ssi_remove,
};

module_platform_driver(ssi_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC serial sound interface driver");
