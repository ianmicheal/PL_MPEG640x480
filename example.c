#include <kos.h>
#include <string.h>
#include <stdio.h>
#include <arch/arch.h>
#include "mpeg1.h"
#include "profiler.h"
#define SUPERSAMPLING 1 // Set to 1 to enable horizontal FSAA, 0 to disable

/* romdisk */
extern uint8 romdisk_boot[];
KOS_INIT_ROMDISK(romdisk_boot);



int main(void)
{
    pvr_init_params_t params = {
        { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0 },
        1 << 20,       // Vertex buffer size, 3MB
        0,             // No DMA
        SUPERSAMPLING  // Set horizontal FSAA
    };
    pvr_init(&params);
    pvr_set_bg_color(0, 0, 0);

    snd_stream_init();
  Mpeg1Play("/cd/z1.mpg", CONT_START);
  
// Mpeg1Play("/rd/z2.mpg", CONT_START);
  

    return 0;
}
