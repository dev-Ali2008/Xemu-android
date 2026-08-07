/*
 * QEMU MCPX Audio Processing Unit implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2018-2019 Jannik Vogel
 * Copyright (c) 2019-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "apu_int.h"

MCPXAPUState *g_state; // Used via debug handlers

static void update_irq(MCPXAPUState *d)
{
    if (d->regs[NV_PAPU_FECTL] & NV_PAPU_FECTL_FEMETHMODE_TRAPPED) {
        qatomic_or(&d->regs[NV_PAPU_ISTS], NV_PAPU_ISTS_FETINTSTS);
    }
    if ((d->regs[NV_PAPU_IEN] & NV_PAPU_ISTS_GINTSTS) &&
        ((d->regs[NV_PAPU_ISTS] & ~NV_PAPU_ISTS_GINTSTS) &
         d->regs[NV_PAPU_IEN])) {
        qatomic_or(&d->regs[NV_PAPU_ISTS], NV_PAPU_ISTS_GINTSTS);
        // fprintf(stderr, "mcpx irq raise ien=%08x ists=%08x\n",
        //         d->regs[NV_PAPU_IEN], d->regs[NV_PAPU_ISTS]);
        pci_irq_assert(PCI_DEVICE(d));
    } else {
        qatomic_and(&d->regs[NV_PAPU_ISTS], ~NV_PAPU_ISTS_GINTSTS);
        // fprintf(stderr, "mcpx irq lower ien=%08x ists=%08x\n",
        //         d->regs[NV_PAPU_IEN], d->regs[NV_PAPU_ISTS]);
        pci_irq_deassert(PCI_DEVICE(d));
    }
}

static uint64_t mcpx_apu_read(void *opaque, hwaddr addr, unsigned int size)
{
    MCPXAPUState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PAPU_XGSCNT:
        r = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 100; //???
        break;
    default:
        if (addr < 0x20000) {
            r = qatomic_read(&d->regs[addr]);
        }
        break;
    }

    trace_mcpx_apu_reg_read(addr, size, r);
    return r;
}

static void mcpx_apu_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned int size)
{
    MCPXAPUState *d = opaque;

    trace_mcpx_apu_reg_write(addr, size, val);

    switch (addr) {
    case NV_PAPU_ISTS:
        /* the bits of the interrupts to clear are written */
        qatomic_and(&d->regs[NV_PAPU_ISTS], ~val);
        update_irq(d);
        qemu_cond_broadcast(&d->cond);
        break;
    case NV_PAPU_FECTL:
    case NV_PAPU_SECTL:
        qatomic_set(&d->regs[addr], val);
        qemu_cond_broadcast(&d->cond);
        break;
    case NV_PAPU_FEMEMDATA:
        /* 'magic write'
         * This value is expected to be written to FEMEMADDR on completion of
         * something to do with notifies. Just do it now :/ */
        stl_le_phys(&address_space_memory, d->regs[NV_PAPU_FEMEMADDR], val);
        // fprintf(stderr, "MAGIC WRITE\n");
        qatomic_set(&d->regs[addr], val);
        break;
    default:
        if (addr < 0x20000) {
            qatomic_set(&d->regs[addr], val);
        }
        break;
    }
}

static const MemoryRegionOps mcpx_apu_mmio_ops = {
    .read = mcpx_apu_read,
    .write = mcpx_apu_write,
};

static int monitor_num_used_bytes(MCPXAPUState *d)
{
    int queued_bytes;
    qemu_spin_lock(&d->monitor.fifo_lock);
    queued_bytes = (int)fifo8_num_used(&d->monitor.fifo);
    qemu_spin_unlock(&d->monitor.fifo_lock);
    return queued_bytes;
}

static void throttle(MCPXAPUState *d)
{
    if (d->ep_frame_div % 8) {
        return;
    }

    if (d->monitor.fifo_capacity_bytes <= 0) {
        return;
    }

    int64_t start_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    int queued_bytes = monitor_num_used_bytes(d);

    while (!qatomic_read(&d->exiting) &&
           queued_bytes >= d->monitor.queued_bytes_high) {
        qemu_cond_timedwait(&d->cond, &d->lock, EP_FRAME_US / 1000);
        if (qatomic_read(&d->exiting)) {
            break;
        }
        queued_bytes = monitor_num_used_bytes(d);
    }

#ifdef __ANDROID__
    /* Android scheduler granularity is often too coarse for the extra
     * low-watermark pacing below and can make speech sound dragged out.
     * Keep FIFO backpressure, but let the output callback set the pace.
     */
    d->next_frame_time_us = 0;
    return;
#endif

    if (queued_bytes > d->monitor.queued_bytes_low) {
        int64_t now_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        if (d->next_frame_time_us == 0 ||
            now_us - d->next_frame_time_us > EP_FRAME_US) {
            d->next_frame_time_us = now_us;
        }
        while (!qatomic_read(&d->exiting)) {
            now_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
            int64_t remaining_ms = (d->next_frame_time_us - now_us) / 1000;
            if (remaining_ms > 0) {
                int sleep_ms = remaining_ms > INT_MAX ? INT_MAX : (int)remaining_ms;
                qemu_cond_timedwait(&d->cond, &d->lock, sleep_ms);
            } else {
                break;
            }
        }
        d->next_frame_time_us += EP_FRAME_US;

        /* Nudge frame timing based on queue level to avoid drifting
         * toward one of the watermarks.
         */
        int mid = (d->monitor.queued_bytes_low + d->monitor.queued_bytes_high) / 2;
        d->next_frame_time_us += (queued_bytes > mid) - (queued_bytes < mid);
    } else {
        d->next_frame_time_us = start_us;
    }
}

static void se_frame(MCPXAPUState *d)
{
    mcpx_apu_update_dsp_preference(d);
    mcpx_debug_begin_frame();
    g_dbg.gp_realtime = d->gp.realtime;
    g_dbg.ep_realtime = d->ep.realtime;

    int64_t start_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    int64_t elapsed_us = start_us - d->frame_count_time_us;
    if (elapsed_us >= 1000000) {
        /* A rudimentary calculation to determine approximately how taxed the APU
         * thread is, by measuring how much time we spend building frames.
         * =1: thread is not sleeping and likely falling behind realtime
         * <1: thread is able to complete work on time
         */
        g_dbg.utilization = (double)d->frame_work_acc_us / (double)elapsed_us;
        g_dbg.frames_processed = (int)(d->frame_count * 1000000.0 / elapsed_us + 0.5);
        d->frame_count_time_us = start_us;
        d->frame_count = 0;
        d->frame_work_acc_us = 0;
    }
    d->frame_count++;

    /* Buffer for all mixbins for this frame */
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };

    mcpx_apu_vp_frame(d, mixbins);
    mcpx_apu_dsp_frame(d, mixbins);

    if ((d->ep_frame_div + 1) % 8 == 0) {
#if 0
        FILE *fd = fopen("ep.pcm", "a+");
        assert(fd != NULL);
        fwrite(d->apu_fifo_output, sizeof(d->apu_fifo_output), 1, fd);
        fclose(fd);
#endif

        if (0 <= g_config.audio.volume_limit && g_config.audio.volume_limit < 1) {
            float f = pow(g_config.audio.volume_limit, M_E);
            for (int i = 0; i < 256; i++) {
                d->monitor.frame_buf[i][0] *= f;
                d->monitor.frame_buf[i][1] *= f;
            }
        }

        if (d->monitor.fifo_capacity_bytes > 0) {
            qemu_spin_lock(&d->monitor.fifo_lock);
            int num_bytes_free = (int)fifo8_num_free(&d->monitor.fifo);
            assert(num_bytes_free >= sizeof(d->monitor.frame_buf));
            fifo8_push_all(&d->monitor.fifo, (uint8_t *)d->monitor.frame_buf,
                           sizeof(d->monitor.frame_buf));
            qemu_spin_unlock(&d->monitor.fifo_lock);
        }
        memset(d->monitor.frame_buf, 0, sizeof(d->monitor.frame_buf));
    }

    d->ep_frame_div++;
    d->frame_work_acc_us += qemu_clock_get_us(QEMU_CLOCK_REALTIME) - start_us;

    mcpx_debug_end_frame();
}

/* Note: only supports millisecond resolution on Windows */
static void sleep_ns(int64_t ns)
{
#ifndef _WIN32
        struct timespec sleep_delay, rem_delay;
        sleep_delay.tv_sec = ns / 1000000000LL;
        sleep_delay.tv_nsec = ns % 1000000000LL;
        nanosleep(&sleep_delay, &rem_delay);
#else
        Sleep(ns / SCALE_MS);
#endif
}

static int getenv_int_clamped(const char *name, int min_value, int max_value,
                              int fallback)
{
    const char *value = getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }

    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        return fallback;
    }

    if (parsed < min_value) {
        return min_value;
    }
    if (parsed > max_value) {
        return max_value;
    }
    return (int)parsed;
}

static void monitor_apply_fade_in(uint8_t *stream, int len)
{
    int frame_bytes = sizeof(int16_t[2]);
    int frames = len / frame_bytes;

    if (frames <= 1) {
        return;
    }

    int fade_frames = MIN(frames, 64);
    int16_t *samples = (int16_t *)stream;
    for (int i = 0; i < fade_frames; i++) {
        int gain_num = i;
        int gain_den = fade_frames - 1;
        samples[i * 2 + 0] = (int16_t)((samples[i * 2 + 0] * gain_num) / gain_den);
        samples[i * 2 + 1] = (int16_t)((samples[i * 2 + 1] * gain_num) / gain_den);
    }
}

static void monitor_fill_underrun(const int16_t start_sample[2], uint8_t *stream,
                                  int len)
{
    int frame_bytes = sizeof(int16_t[2]);
    int frames = len / frame_bytes;
    int16_t *out = (int16_t *)stream;

    if (frames == 1) {
        out[0] = 0;
        out[1] = 0;
    } else if (frames > 1) {
        int fade_frames = MIN(frames, 64);
        for (int i = 0; i < fade_frames; i++) {
            int gain_num = fade_frames - 1 - i;
            int gain_den = fade_frames - 1;
            out[i * 2 + 0] =
                (int16_t)((start_sample[0] * gain_num) / gain_den);
            out[i * 2 + 1] =
                (int16_t)((start_sample[1] * gain_num) / gain_den);
        }
        if (fade_frames < frames) {
            memset(stream + (fade_frames * frame_bytes), 0,
                   (frames - fade_frames) * frame_bytes);
        }
    }

    int tail_bytes = len - (frames * frame_bytes);
    if (tail_bytes > 0) {
        memset(stream + (frames * frame_bytes), 0, tail_bytes);
    }
}

static void monitor_sink_cb(void *opaque, uint8_t *stream, int free_b)
{
    MCPXAPUState *s = MCPX_APU_DEVICE(opaque);

    if (!runstate_is_running()) {
        memset(stream, 0, free_b);
        return;
    }

    int avail = 0;
    int wait_attempts = 10;
    for (int i = 0; i < wait_attempts; i++) {
        qemu_spin_lock(&s->monitor.fifo_lock);
        avail = fifo8_num_used(&s->monitor.fifo);
        qemu_spin_unlock(&s->monitor.fifo_lock);
        if (avail >= free_b) {
            break;
        }
        sleep_ns(500000);
        qemu_cond_broadcast(&s->cond);
        if (!runstate_is_running()) {
            memset(stream, 0, free_b);
            return;
        }
    }

    int copied = 0;
    int to_copy = MIN(free_b, avail);
    while (copied < to_copy) {
        uint32_t chunk_len = 0;
        qemu_spin_lock(&s->monitor.fifo_lock);
        chunk_len = fifo8_pop_buf(&s->monitor.fifo, stream + copied,
                                  to_copy - copied);
        qemu_spin_unlock(&s->monitor.fifo_lock);
        if (!chunk_len) {
            break;
        }
        copied += chunk_len;
    }

    if (copied > 0 && s->monitor.resume_fade_pending) {
        monitor_apply_fade_in(stream, copied);
        s->monitor.resume_fade_pending = false;
    }

    if (copied < free_b) {
        int16_t fill_from[2] = {
            s->monitor.last_output_sample[0],
            s->monitor.last_output_sample[1],
        };
        if (copied >= sizeof(fill_from)) {
            memcpy(fill_from, stream + copied - sizeof(fill_from),
                   sizeof(fill_from));
        }
        monitor_fill_underrun(fill_from, stream + copied, free_b - copied);
        s->monitor.resume_fade_pending = true;
    }

    if (free_b >= sizeof(s->monitor.last_output_sample)) {
        int16_t *last = (int16_t *)(stream + free_b -
                                    sizeof(s->monitor.last_output_sample));
        s->monitor.last_output_sample[0] = last[0];
        s->monitor.last_output_sample[1] = last[1];
    }

    qemu_cond_broadcast(&s->cond);
}

static void monitor_init(MCPXAPUState *d)
{
    qemu_spin_init(&d->monitor.fifo_lock);
    d->monitor.fifo_capacity_bytes = 0;
    d->monitor.device_buffer_bytes = 0;
    d->monitor.queued_bytes_low = 0;
    d->monitor.queued_bytes_high = 0;
    d->monitor.last_output_sample[0] = 0;
    d->monitor.last_output_sample[1] = 0;
    d->monitor.resume_fade_pending = false;

    int fifo_frames = 3;
    int audio_samples = 512;
#ifdef __ANDROID__
    /* Give Android more audio headroom to ride out scheduling stalls. */
    fifo_frames = 24;
    audio_samples = 2048;
    fifo_frames = getenv_int_clamped("XEMU_ANDROID_AUDIO_FIFO_FRAMES", 3, 32,
                                     fifo_frames);
    audio_samples = getenv_int_clamped("XEMU_ANDROID_AUDIO_SAMPLES", 256, 4096,
                                       audio_samples);
#endif
    int fifo_capacity_bytes = fifo_frames * sizeof(d->monitor.frame_buf);
    fifo8_create(&d->monitor.fifo, fifo_capacity_bytes);

    struct SDL_AudioSpec sdl_audio_spec = {
        .freq = 48000,
        .format = AUDIO_S16LSB,
        .channels = 2,
        .samples = audio_samples,
        .callback = monitor_sink_cb,
        .userdata = d,
    };

    if (SDL_Init(SDL_INIT_AUDIO) < 0)  {
        fprintf(stderr, "WARNING: Failed to initialize SDL audio subsystem: %s\n",
                SDL_GetError());
        return;
    }

    SDL_AudioDeviceID sdl_audio_dev;
    SDL_AudioSpec obtained_audio_spec;
    sdl_audio_dev = SDL_OpenAudioDevice(NULL, 0, &sdl_audio_spec,
                                        &obtained_audio_spec, 0);
    if (sdl_audio_dev == 0) {
        fprintf(stderr, "WARNING: SDL_OpenAudioDevice failed: %s\n",
                SDL_GetError());
        return;
    }

    int bytes_per_sample = SDL_AUDIO_BITSIZE(obtained_audio_spec.format) / 8;
    if (bytes_per_sample <= 0) {
        bytes_per_sample = SDL_AUDIO_BITSIZE(sdl_audio_spec.format) / 8;
    }
    if (bytes_per_sample <= 0) {
        bytes_per_sample = 2;
    }
    int device_buffer_bytes = obtained_audio_spec.samples *
                              obtained_audio_spec.channels *
                              bytes_per_sample;
    if (device_buffer_bytes <= 0) {
        device_buffer_bytes = audio_samples * sdl_audio_spec.channels *
                              bytes_per_sample;
    }

    int frame_bytes = sizeof(d->monitor.frame_buf);
    int drain_bytes = MAX(device_buffer_bytes, frame_bytes);
    int max_high = MAX(fifo_capacity_bytes - frame_bytes, frame_bytes);
    d->monitor.fifo_capacity_bytes = fifo_capacity_bytes;
    d->monitor.device_buffer_bytes = device_buffer_bytes;
#ifdef __ANDROID__
    d->monitor.queued_bytes_high = MIN(3 * drain_bytes, max_high);
    d->monitor.queued_bytes_low = MIN(drain_bytes, d->monitor.queued_bytes_high);
#else
    d->monitor.queued_bytes_high = MIN(3 * drain_bytes, max_high);
    d->monitor.queued_bytes_low = MIN(drain_bytes, d->monitor.queued_bytes_high);
#endif

    SDL_PauseAudioDevice(sdl_audio_dev, 0);
}

static void mcpx_apu_realize(PCIDevice *dev, Error **errp)
{
    MCPXAPUState *d = MCPX_APU_DEVICE(dev);

    dev->config[PCI_INTERRUPT_PIN] = 0x01;

    memory_region_init_io(&d->mmio, OBJECT(dev), &mcpx_apu_mmio_ops, d,
                          "mcpx-apu-mmio", 0x80000);

    memory_region_init_io(&d->vp.mmio, OBJECT(dev), &vp_ops, d,
                          "mcpx-apu-vp", 0x10000);
    memory_region_add_subregion(&d->mmio, 0x20000, &d->vp.mmio);

    memory_region_init_io(&d->gp.mmio, OBJECT(dev), &gp_ops, d,
                          "mcpx-apu-gp", 0x10000);
    memory_region_add_subregion(&d->mmio, 0x30000, &d->gp.mmio);

    memory_region_init_io(&d->ep.mmio, OBJECT(dev), &ep_ops, d,
                          "mcpx-apu-ep", 0x10000);
    memory_region_add_subregion(&d->mmio, 0x50000, &d->ep.mmio);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);
}

static void mcpx_apu_exitfn(PCIDevice *dev)
{
    MCPXAPUState *d = MCPX_APU_DEVICE(dev);
    d->exiting = true;
    qemu_cond_broadcast(&d->cond);
    qemu_thread_join(&d->apu_thread);
    mcpx_apu_vp_finalize(d);
}

static void mcpx_apu_reset(MCPXAPUState *d)
{
    qemu_mutex_lock(&d->lock); // FIXME: Can fail if thread is pegged, add flag
    memset(d->regs, 0, sizeof(d->regs));

    mcpx_apu_vp_reset(d);

    // FIXME: Reset DSP state
    dsp_invalidate_opcache(d->gp.dsp);
    dsp_invalidate_opcache(d->ep.dsp);
    d->set_irq = false;
    d->next_frame_time_us = 0;
    qemu_cond_signal(&d->cond);
    qemu_mutex_unlock(&d->lock);
}

// Note: This is handled as a VM state change and not as a `pre_save` callback
// because we want to halt the FIFO before any VM state is saved/restored to
// avoid corruption.
static void mcpx_apu_vm_state_change(void *opaque, bool running, RunState state)
{
    MCPXAPUState *d = opaque;

    if (state == RUN_STATE_SAVE_VM) {
        qemu_mutex_lock(&d->lock);
        if (d->gp.dsp) {
            dsp_sync_to_vm(d->gp.dsp);
        }
        if (d->ep.dsp) {
            dsp_sync_to_vm(d->ep.dsp);
        }
    }
}

static int mcpx_apu_post_save(void *opaque)
{
    MCPXAPUState *d = opaque;
    qemu_cond_signal(&d->cond);
    qemu_mutex_unlock(&d->lock);
    return 0;
}

static int mcpx_apu_pre_load(void *opaque)
{
    MCPXAPUState *d = opaque;
    mcpx_apu_reset(d);
    qemu_mutex_lock(&d->lock);
    return 0;
}

static int mcpx_apu_post_load(void *opaque, int version_id)
{
    MCPXAPUState *d = opaque;
    if (d->gp.dsp) {
        dsp_sync_from_vm(d->gp.dsp);
    }
    if (d->ep.dsp) {
        dsp_sync_from_vm(d->ep.dsp);
    }
    qemu_cond_signal(&d->cond);
    qemu_mutex_unlock(&d->lock);
    return 0;
}

static void mcpx_apu_reset_hold(Object *obj, ResetType type)
{
    MCPXAPUState *d = MCPX_APU_DEVICE(obj);
    mcpx_apu_reset(d);
}

static bool vp_dsp_dma_read_count_needed(void *opaque)
{
    DSPDMAState *s = opaque;
    return s->dma_read_count != 0;
}

static const VMStateDescription vmstate_vp_dsp_dma_read_count = {
    .name = "mcpx-apu/dsp-state/dma/dma_read_count",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vp_dsp_dma_read_count_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(dma_read_count, DSPDMAState),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_vp_dsp_dma_state = {
    .name = "mcpx-apu/dsp-state/dma",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(configuration, DSPDMAState),
        VMSTATE_UINT32(control, DSPDMAState),
        VMSTATE_UINT32(start_block, DSPDMAState),
        VMSTATE_UINT32(next_block, DSPDMAState),
        VMSTATE_BOOL(error, DSPDMAState),
        VMSTATE_BOOL(eol, DSPDMAState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_vp_dsp_dma_read_count,
        NULL
    }
};

const VMStateDescription vmstate_vp_dsp_core_state = {
    .name = "mcpx-apu/dsp-state/core",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT16(instr_cycle, DspCoreState),
        VMSTATE_UINT32(pc, DspCoreState),
        VMSTATE_UINT32_ARRAY(registers, DspCoreState, DSP_REG_MAX),
        VMSTATE_UINT32_2DARRAY(stack, DspCoreState, 2, 16),
        VMSTATE_UINT32_ARRAY(xram, DspCoreState, DSP_XRAM_SIZE),
        VMSTATE_UINT32_ARRAY(yram, DspCoreState, DSP_YRAM_SIZE),
        VMSTATE_UINT32_ARRAY(pram, DspCoreState, DSP_PRAM_SIZE),
        VMSTATE_UINT32_ARRAY(mixbuffer, DspCoreState, DSP_MIXBUFFER_SIZE),
        VMSTATE_UINT32_ARRAY(periph, DspCoreState, DSP_PERIPH_SIZE),
        VMSTATE_UINT32(loop_rep, DspCoreState),
        VMSTATE_UINT32(pc_on_rep, DspCoreState),
        VMSTATE_UINT16(interrupt_state, DspCoreState),
        VMSTATE_UINT16(interrupt_instr_fetch, DspCoreState),
        VMSTATE_UINT16(interrupt_save_pc, DspCoreState),
        VMSTATE_UINT16(interrupt_counter, DspCoreState),
        VMSTATE_UINT16(interrupt_ipl_to_raise, DspCoreState),
        VMSTATE_UINT16(interrupt_pipeline_count, DspCoreState),
        VMSTATE_INT16_ARRAY(interrupt_ipl, DspCoreState, 12),
        VMSTATE_UINT16_ARRAY(interrupt_is_pending, DspCoreState, 12),
        VMSTATE_UNUSED(4),   /* was num_inst */
        VMSTATE_UINT32(cur_inst_len, DspCoreState),
        VMSTATE_UINT32(cur_inst, DspCoreState),
        VMSTATE_UNUSED(273), /* was: unused(1) + disasm_memory_ptr(4) +
                              * exception_debugging(1) + disasm_prev_inst_pc(4) +
                              * disasm_is_looping(1) + disasm_cur_inst(4) +
                              * disasm_cur_inst_len(2) +
                              * disasm_registers_save(256) */
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_vp_dsp_state = {
    .name = "mcpx-apu/dsp-state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(core, DSPState, 1, vmstate_vp_dsp_core_state, DspCoreState),
        VMSTATE_STRUCT(dma, DSPState, 1, vmstate_vp_dsp_dma_state, DSPDMAState),
        VMSTATE_INT32(save_cycles, DSPState),
        VMSTATE_UINT32(interrupts, DSPState),
        VMSTATE_END_OF_LIST()
    }
};


const VMStateDescription vmstate_vp_ssl_data = {
    .name = "mcpx_apu_voice_data",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(base, MCPXAPUVPSSLData, MCPX_HW_SSLS_PER_VOICE),
        VMSTATE_UINT8_ARRAY(count, MCPXAPUVPSSLData, MCPX_HW_SSLS_PER_VOICE),
        VMSTATE_INT32(ssl_index, MCPXAPUVPSSLData),
        VMSTATE_INT32(ssl_seg, MCPXAPUVPSSLData),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_mcpx_apu = {
    .name = "mcpx-apu",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_save = mcpx_apu_post_save,
    .pre_load = mcpx_apu_pre_load,
    .post_load = mcpx_apu_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, MCPXAPUState),
        VMSTATE_STRUCT_POINTER(gp.dsp, MCPXAPUState, vmstate_vp_dsp_state,
                               DSPState),
        VMSTATE_UINT32_ARRAY(gp.regs, MCPXAPUState, 0x10000),
        VMSTATE_STRUCT_POINTER(ep.dsp, MCPXAPUState, vmstate_vp_dsp_state,
                               DSPState),
        VMSTATE_UINT32_ARRAY(ep.regs, MCPXAPUState, 0x10000),
        VMSTATE_UINT32_ARRAY(regs, MCPXAPUState, 0x20000),
        VMSTATE_UINT32(vp.inbuf_sge_handle, MCPXAPUState),
        VMSTATE_UINT32(vp.outbuf_sge_handle, MCPXAPUState),
        VMSTATE_STRUCT_ARRAY(vp.ssl, MCPXAPUState, MCPX_HW_MAX_VOICES, 1,
                             vmstate_vp_ssl_data, MCPXAPUVPSSLData),
        VMSTATE_INT32(vp.ssl_base_page, MCPXAPUState),
        VMSTATE_UINT8_ARRAY(vp.hrtf_submix, MCPXAPUState, 4),
        VMSTATE_UINT8(vp.hrtf_headroom, MCPXAPUState),
        VMSTATE_UINT8_ARRAY(vp.submix_headroom, MCPXAPUState, NUM_MIXBINS),
        VMSTATE_UINT64_ARRAY(vp.voice_locked, MCPXAPUState, 4),
        VMSTATE_END_OF_LIST()
    },
};

static void mcpx_apu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_MCPX_APU;
    k->revision = 177;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    k->realize = mcpx_apu_realize;
    k->exit = mcpx_apu_exitfn;

    rc->phases.hold = mcpx_apu_reset_hold;

    dc->desc = "MCPX Audio Processing Unit";
    dc->vmsd = &vmstate_mcpx_apu;
}

static const TypeInfo mcpx_apu_info = {
    .name = "mcpx-apu",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MCPXAPUState),
    .class_init = mcpx_apu_class_init,
    .interfaces =
        (InterfaceInfo[]){
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            {},
        },
};

static void mcpx_apu_register(void)
{
    type_register_static(&mcpx_apu_info);
}
type_init(mcpx_apu_register);

static void *mcpx_apu_frame_thread(void *arg)
{
    MCPXAPUState *d = MCPX_APU_DEVICE(arg);
    qemu_mutex_lock(&d->lock);
    while (!qatomic_read(&d->exiting)) {
        int xcntmode = GET_MASK(qatomic_read(&d->regs[NV_PAPU_SECTL]),
                                NV_PAPU_SECTL_XCNTMODE);
        uint32_t fectl = qatomic_read(&d->regs[NV_PAPU_FECTL]);
        if (xcntmode == NV_PAPU_SECTL_XCNTMODE_OFF ||
            (fectl & NV_PAPU_FECTL_FEMETHMODE_TRAPPED) ||
            (fectl & NV_PAPU_FECTL_FEMETHMODE_HALTED)) {
            d->set_irq = true;
        }

        if (d->set_irq) {
            qemu_mutex_unlock(&d->lock);
            bql_lock();
            update_irq(d);
            bql_unlock();
            qemu_mutex_lock(&d->lock);
            d->set_irq = false;
        }

        xcntmode = GET_MASK(qatomic_read(&d->regs[NV_PAPU_SECTL]),
                            NV_PAPU_SECTL_XCNTMODE);
        fectl = qatomic_read(&d->regs[NV_PAPU_FECTL]);
        if (xcntmode == NV_PAPU_SECTL_XCNTMODE_OFF ||
            (fectl & NV_PAPU_FECTL_FEMETHMODE_TRAPPED) ||
            (fectl & NV_PAPU_FECTL_FEMETHMODE_HALTED)) {
            qemu_cond_wait(&d->cond, &d->lock);
            continue;
        }
        throttle(d);
        se_frame((void *)d);
    }
    qemu_mutex_unlock(&d->lock);
    return NULL;
}

void mcpx_apu_init(PCIBus *bus, int devfn, MemoryRegion *ram)
{
    PCIDevice *dev = pci_create_simple(bus, devfn, "mcpx-apu");
    MCPXAPUState *d = MCPX_APU_DEVICE(dev);

    g_state = d;

    d->ram = ram;
    d->ram_ptr = memory_region_get_ram_ptr(d->ram);

    mcpx_apu_dsp_init(d);

    d->set_irq = false;
    d->exiting = false;

    qemu_mutex_init(&d->lock);
    qemu_cond_init(&d->cond);
    qemu_add_vm_change_state_handler(mcpx_apu_vm_state_change, d);

    mcpx_apu_vp_init(d);
    monitor_init(d);
    qemu_thread_create(&d->apu_thread, "mcpx.apu_thread", mcpx_apu_frame_thread,
                       d, QEMU_THREAD_JOINABLE);
}
