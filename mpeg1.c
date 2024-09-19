/*
 * PL_MPEG - MPEG1 Video decoder, MP2 Audio decoder, MPEG-PS demuxer
 * -------------------------------------------------------------
 *
 * Original Author: Dominic Szablewski - https://phoboslab.org
 * Dreamcast Port: Ian Michael (2023/2024)
 * Dreamcast Port:Twada SH4 Optimizing and sound [making it use-able at all]
 * Further optimizing functions for Dreamcast
 * SH4 inline assembly by Ian Michael
 *
 * LICENSE: The MIT License (MIT)
 * ------------------------------
 * Copyright (c) 2019 Dominic Szablewski
 * Copyright (c) 2024 Ian Michael
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <kos.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#include "mpeg1.h"

// Output texture width and height initial values
#define MPEG1_TEXTURE_WIDTH 1024
#define MPEG1_TEXTURE_HEIGHT 512
#define FSAA_ENABLED 1  // Set to 0 for normal mode, 1 for FSAA mode

#if FSAA_ENABLED
    #define SCREEN_WIDTH 1280.0f
#else
    #define SCREEN_WIDTH 640.0f
#endif

#define SCREEN_HEIGHT 480.0f
#define FRAME_TIME_NS 16666667 // Approximately 1/60 second in nanoseconds

static plm_t *plm;
static pvr_ptr_t texture;
static int width, height;

float audio_time;
float audio_interval;
snd_stream_hnd_t snd_hnd;
__attribute__((aligned(32))) unsigned int snd_buf[0x10000 / 4];
static int snd_mod_start = 0;
static int snd_mod_size = 0;

// Performance metrics
uint64 start_time, cpu_start, cpu_end, gpu_start, gpu_end;
float total_frame_time, avrg_gpu_time = 0, avrg_cpu_time = 0, avrg_idle_time = 0;
uint32 fps_counter = 0;
uint32 fps_start_time = 0;
uint32 current_fps = 0;

void update_performance_metrics() {
    fps_counter++;
    uint32 current_time = timer_ms_gettime64();
    if (current_time - fps_start_time >= 1000) {
        current_fps = fps_counter;
        fps_counter = 0;
        fps_start_time = current_time;

        float gpu_time = (gpu_end - gpu_start) / 1000000.0f;
        float cpu_time = (cpu_end - cpu_start) / 1000000.0f;
        total_frame_time = (gpu_end - start_time) / 1000000.0f;
        float idle_time = total_frame_time - gpu_time - cpu_time;

        avrg_gpu_time = avrg_gpu_time * 0.95f + gpu_time * 0.05f;
        avrg_cpu_time = avrg_cpu_time * 0.95f + cpu_time * 0.05f;
        avrg_idle_time = avrg_idle_time * 0.95f + idle_time * 0.05f;

        printf("FPS: %lu, Avg GPU: %.2f ms, Avg CPU: %.2f ms, Avg Idle: %.2f ms\n", 
               (unsigned long)current_fps, avrg_gpu_time, avrg_cpu_time, avrg_idle_time);
    }
}


void bit64_sq_cpy(void *dest, void *src, int n)
{
    uint32 *d, *s;
    uint32 r0, r1, r2, r3, r4, r5, r6, r7;
    
    d = (uint32 *)(0xe0000000 | (((uint32)dest) & 0x03FFFFFF));
    s = (uint32 *)(src);

    *((volatile unsigned int*)0xFF000038) = ((((uint32)dest) >> 26) << 2) & 0x1c;
    *((volatile unsigned int*)0xFF00003C) = ((((uint32)dest) >> 26) << 2) & 0x1c;

    n >>= 6;

    while (n--) 
    {
        r0 = *s++; r1 = *s++;
        r2 = *s++; r3 = *s++;
        r4 = *s++; r5 = *s++;
        r6 = *s++; r7 = *s++;

        __asm__ volatile (
            "mov.l %2,@%0  ; mov.l %3,@(4,%0) \n\t"
            "mov.l %4,@(8,%0) ; mov.l %5,@(12,%0) \n\t"
            "mov.l %6,@(16,%0) ; mov.l %7,@(20,%0) \n\t"
            "mov.l %8,@(24,%0) ; mov.l %9,@(28,%0) \n\t"
            "pref @%0 \n\t"
            "ocbi @%1 \n\t"
            "add #32,%0 \n\t"
            : "+r" (d)
            : "r" (dest), "r" (r0), "r" (r1), "r" (r2), "r" (r3), 
              "r" (r4), "r" (r5), "r" (r6), "r" (r7)
            : "memory"
        );

        r0 = *s++; r1 = *s++;
        r2 = *s++; r3 = *s++;
        r4 = *s++; r5 = *s++;
        r6 = *s++; r7 = *s++;

        __asm__ volatile (
            "mov.l %2,@%0  ; mov.l %3,@(4,%0) \n\t"
            "mov.l %4,@(8,%0) ; mov.l %5,@(12,%0) \n\t"
            "mov.l %6,@(16,%0) ; mov.l %7,@(20,%0) \n\t"
            "mov.l %8,@(24,%0) ; mov.l %9,@(28,%0) \n\t"
            "pref @%0 \n\t"
            "ocbi @%1 \n\t"
            "add #32,%0 \n\t"
            : "+r" (d)
            : "r" (dest), "r" (r0), "r" (r1), "r" (r2), "r" (r3), 
              "r" (r4), "r" (r5), "r" (r6), "r" (r7)
            : "memory"
        );
    }

    *((uint32 *)(0xe0000000)) = 0;
    *((uint32 *)(0xe0000020)) = 0;
}

void *sq_set_imr(void *s, uint32_t c, int n) {
    unsigned int *d = (unsigned int *)(void *)
                      (0xe0000000 | (((unsigned long)s) & 0x03FFFFFF));
    unsigned int fill_value;
    
    *((volatile unsigned int *)0xff000038) = ((((unsigned int)s) >> 26) << 2) & 0x1c;
    *((volatile unsigned int *)0xff00003c) = ((((unsigned int)s) >> 26) << 2) & 0x1c;
    
    fill_value = (c & 0xff) * 0x01010101;
    
    n >>= 5;
    while (n--) {
        __asm__ volatile (
            "ocbi @%0\n\t"
            "pref @%1\n\t"
            "mov.l %2, @%1\n\t"
            "mov.l %2, @(4,%1)\n\t"
            "mov.l %2, @(8,%1)\n\t"
            "mov.l %2, @(12,%1)\n\t"
            "mov.l %2, @(16,%1)\n\t"
            "mov.l %2, @(20,%1)\n\t"
            "mov.l %2, @(24,%1)\n\t"
            "mov.l %2, @(28,%1)\n\t"
            : 
            : "r" (s), "r" (d), "r" (fill_value)
            : "memory"
        );
        d += 8;
        s += 32;
    }
    return s;
}

void display_draw(void)
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t *vert;
    pvr_dr_state_t dr_state;
    float u = (float)width / (float)MPEG1_TEXTURE_WIDTH;
    float v = (float)height / (float)MPEG1_TEXTURE_HEIGHT;
    
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_YUV422 | PVR_TXRFMT_NONTWIDDLED, MPEG1_TEXTURE_WIDTH, MPEG1_TEXTURE_HEIGHT, texture, PVR_FILTER_NONE);
    pvr_poly_compile(&hdr, &cxt);
    
    pvr_prim(&hdr, sizeof(hdr));
    
    pvr_dr_init(&dr_state);
    
    vert = pvr_dr_target(dr_state);
    vert->argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert->oargb = 0;
    vert->flags = PVR_CMD_VERTEX;
    vert->x = 0.0f;
    vert->y = 0.0f;
    vert->z = 1;
    vert->u = 0.0f;
    vert->v = 0.0f;
    pvr_dr_commit(vert);
    
    vert = pvr_dr_target(dr_state);
    vert->argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert->oargb = 0;
    vert->flags = PVR_CMD_VERTEX;
    vert->x = SCREEN_WIDTH;
    vert->y = 0.0f;
    vert->z = 1;
    vert->u = u;
    vert->v = 0.0f;
    pvr_dr_commit(vert);
    
    vert = pvr_dr_target(dr_state);
    vert->argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert->oargb = 0;
    vert->flags = PVR_CMD_VERTEX;
    vert->x = 0.0f;
    vert->y = SCREEN_HEIGHT;
    vert->z = 1;
    vert->u = 0.0f;
    vert->v = v;
    pvr_dr_commit(vert);
    
    vert = pvr_dr_target(dr_state);
    vert->argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert->oargb = 0;
    vert->flags = PVR_CMD_VERTEX_EOL;
    vert->x = SCREEN_WIDTH;
    vert->y = SCREEN_HEIGHT;
    vert->z = 1;
    vert->u = u;
    vert->v = v;
    pvr_dr_commit(vert);
    pvr_dr_finish();
}

void app_on_video(plm_t *mpeg, plm_frame_t *frame, void *user)
{
    uint32_t *src;
    int x, y, w, h, i;

    if(!frame)
        return;

    src = frame->display;

    w = frame->width >> 4;
    h = frame->height >> 4;

    int const min_blocks_x = 32 * (frame->width / 320) - w;
    int const min_blocks_y = 16 * (frame->height / 240) - h;

    for(y = 0; y < h; y++) {
        for(x = 0; x < w; x++, src += 96) {
            bit64_sq_cpy((void *)PVR_TA_YUV_CONV, (void *)src, 384);
        }
        for(i = 0; i < min_blocks_x; i++) {
            sq_set_imr((void *)PVR_TA_YUV_CONV, 0x10800000, 384);
        }
    }

    for(i = 0; i < min_blocks_y; i++) {
         sq_set_imr((void *)PVR_TA_YUV_CONV, 0x10800000, 384 * 32);
    }
}

static int audio_ended = 0;  // Global flag to indicate end of audio

static void *sound_callback(snd_stream_hnd_t snd_hnd, int smp_req, int *smp_recv)
{
    plm_samples_t *sample;
    uint32 *sq;
    uint32 *d, *s;
    int out = 0;
    int remaining;
    
    // Set up destination pointer with SQ address space and alignment
    d = (uint32 *)(0xe0000000 | (((uint32)snd_buf) & 0x03ffffe0));
    
    // Configure memory-mapped registers for SQ access
    *((volatile unsigned int*)0xFF000038) = ((((uint32)snd_buf) >> 26) << 2) & 0x1c;
    *((volatile unsigned int*)0xFF00003C) = ((((uint32)snd_buf) >> 26) << 2) & 0x1c;
    
    // Copy remaining data from previous callback
    s = (uint32 *)snd_buf + snd_mod_start / 4;
    remaining = snd_mod_size / 4;
    while (remaining >= 16) {
        // Prefetch next block
        __asm__("pref @%0" : : "r"(s + 16));
        // Copy 64 bytes (16 uint32 values) using SQ
        sq = d;
        for (int i = 0; i < 2; i++) {
            *sq++ = *s++; *sq++ = *s++; *sq++ = *s++; *sq++ = *s++;
            *sq++ = *s++; *sq++ = *s++; *sq++ = *s++; *sq++ = *s++;
            __asm__("pref @%0" : : "r"(d));
            __asm__("ocbi @%0" : : "r"(snd_buf));
            d += 8;
        }
        remaining -= 16;
    }
    // Handle remaining elements
    while (remaining > 0) {
        *d++ = *s++;
        remaining--;
    }
    out += snd_mod_size;
    
    while (smp_req > out)
    {
        sample = plm_decode_audio(plm);
        if (sample == NULL)
        {
            printf("Debug: End of audio reached in sound_callback at line %d\n", __LINE__);
            // Fill the rest of the buffer with silence to avoid the beep
            memset((uint8_t*)snd_buf + out, 0, smp_req - out);
            out = smp_req;
            audio_ended = 1;  // Set the flag to indicate audio has ended
            break;
        }
        audio_time = sample->time;
        s = (uint32 *)sample->pcm;
        
        // Copy 1152/2 = 576 32-bit values
        for (int i = 0; i < 576 / 16; i++) {
            __asm__("pref @%0" : : "r"(s + 16));
            sq = d;
            for (int j = 0; j < 2; j++) {
                *sq++ = *s++; *sq++ = *s++; *sq++ = *s++; *sq++ = *s++;
                *sq++ = *s++; *sq++ = *s++; *sq++ = *s++; *sq++ = *s++;
                __asm__("pref @%0" : : "r"(d));
                __asm__("ocbi @%0" : : "r"(snd_buf));
                d += 8;
            }
        }
        out += 1152 * 2;
    }
    
    // Clear SQ registers
    *((uint32 *)(0xe0000000)) = 0;
    *((uint32 *)(0xe0000020)) = 0;
    
    snd_mod_start = smp_req;
    snd_mod_size = out - smp_req;
    *smp_recv = out;
    
    return snd_buf;
}

int Mpeg1Play(const char *filename, unsigned int buttons) {
    uint64_t frame_time_ns = FRAME_TIME_NS;
    int cancel = 0;
    plm = plm_create_with_filename(filename);
    if (!plm) {
        printf("Debug: Failed to create plm at line %d\n", __LINE__);
        return -1;
    }
    texture = pvr_mem_malloc(MPEG1_TEXTURE_WIDTH * MPEG1_TEXTURE_HEIGHT * 2);
    width = plm_get_width(plm);
    height = plm_get_height(plm);
    
    // Divide texture width and texture height by 16 and subtract 1.
    // The actual values to set are 1, 3, 7, 15, 31, 63.
    /* Set SQ to YUV converter. */
    PVR_SET(PVR_YUV_ADDR, (((unsigned int)texture) & 0xffffff));
    PVR_SET(PVR_YUV_CFG, (((MPEG1_TEXTURE_HEIGHT / 16) - 1) << 8) | ((MPEG1_TEXTURE_WIDTH / 16) - 1));
    PVR_GET(PVR_YUV_CFG);
    
    /* First frame */
    plm_frame_t *frame = plm_decode_video(plm);
    int decoded = 1;
    
    /* Init sound stream. */
    int samplerate = plm_get_samplerate(plm);
    snd_mod_size = 0;
    snd_mod_start = 0;
    audio_time = 0.0f;
    snd_hnd = snd_stream_alloc(sound_callback, 0x10000);
    snd_stream_volume(snd_hnd, 0xff);
    snd_stream_queue_enable(snd_hnd);
    snd_stream_start(snd_hnd, samplerate, 0);
    snd_stream_queue_go(snd_hnd);
    audio_interval = audio_time;
    
    fps_start_time = timer_ms_gettime64();
    
    audio_ended = 0;  // Reset the audio_ended flag

    while (!cancel) {
        start_time = timer_ns_gettime64(); // Start timing the frame
        
        cpu_start = timer_ns_gettime64();
        /* Check cancel buttons. */
        MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
            if (buttons && ((st->buttons & buttons) == buttons))
                cancel = 1; /* Push cancel buttons */
        MAPLE_FOREACH_END()
        
        /* Decode */
        if ((audio_time - audio_interval) >= frame->time) {
            frame = plm_decode_video(plm);
            if (!frame) {
                printf("Debug: End of video reached at line %d\n", __LINE__);
                goto cleanup;
            }
            decoded = 1;
        }
        
        snd_stream_poll(snd_hnd);

        // Check if audio has ended
        if (audio_ended) {
            printf("Debug: Audio has ended, finishing playback at line %d\n", __LINE__);
            goto cleanup;
        }

        cpu_end = timer_ns_gettime64();
        
        pvr_wait_ready();
        
        gpu_start = timer_ns_gettime64();
        pvr_scene_begin();
        
        if (decoded) {
            app_on_video(plm, frame, 0);
            decoded = 0;
        }
        
        pvr_list_begin(PVR_LIST_OP_POLY);
        display_draw();
        
        /* Finish up */
        pvr_list_finish();
        pvr_scene_finish();
        gpu_end = timer_ns_gettime64();
        
        update_performance_metrics();
        
        // Calculate how much time has passed in nanoseconds
        uint64_t frame_time_taken_ns = gpu_end - start_time;
        
        // If frame was rendered faster than desired frame time, delay to maintain frame rate
        if (frame_time_taken_ns < frame_time_ns) {
            uint64_t delay_ns = frame_time_ns - frame_time_taken_ns;
            
            // Use a combination of microsecond and nanosecond delays for longer waits
            while (delay_ns > 65535) {
                timer_spin_delay_us(65); // 65 microseconds
                delay_ns -= 65000;
            }
            
            // Use nanosecond delay for the remainder
            if (delay_ns > 0) {
                timer_spin_delay_ns(delay_ns);
            }
        }
    }

cleanup:
    printf("Debug: Starting cleanup at line %d\n", __LINE__);
    snd_stream_stop(snd_hnd);
    plm_destroy(plm);
    pvr_mem_free(texture);
    pvr_shutdown(); // Clean up PVR resources
    vid_shutdown(); // This function reinitializes the video system to what dcload and friends expect it to be
    
    // Run the main application here
    thd_sleep(5); // Sleep for a while (adjust the delay as needed)
    timer_spin_sleep(50);
    
    void* subelf;
    int length;
    length = fs_load("/rd/game.pl", &subelf);
    arch_exec_at(subelf, length, 0x8C010000);
    
    printf("arch_exit\n");
    return cancel;
}

