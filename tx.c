#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <libbladeRF.h>

#define SR 300000
#define BD 300
#define FREQ 315000000

struct bladerf* blade;
FILE* inf;

void transmit_file()
{
    int rv;
    int i, j, k;
    int bit;
    char* fbuf;
    int16_t *buffer, *warmupbuf;
    long filesize, buffersize, warmupsize;

    fseek(inf, 0, SEEK_END);
    filesize = ftell(inf);
    fseek(inf, 0, SEEK_SET);

    fbuf = malloc(filesize);
    if(buffer == NULL) {
        perror("Could not allocate memory for file buffer");
        return;
    }

    // For each byte in the file, we need 10 bits transmitted
    // For each bit transmitted, we need SR/BD samples
    // For each sample, we need four bytes
    // We then need to round up to multiples of 1024 samples.
    buffersize = filesize * 10 * (SR/BD) * 4;
    buffersize += 4 * (1024 - (buffersize / 4) % 1024);
    printf("Allocating %d bytes buffer..\n", buffersize);
    buffer = malloc(buffersize);
    if(buffer == NULL) {
        perror("Could not allocate memory for sample buffer");
        return;
    }

    warmupsize = 4 * SR * 5;
    warmupbuf = malloc(warmupsize);
    if(warmupbuf == NULL) {
        perror("Could not allocate memory for warmup buffer");
        return;
    }
    for(i=0; i<warmupsize/2; i++) {
        warmupbuf[i] = 1024;
    }

    fread(fbuf, 1, filesize, inf);

    for(i=0; i<buffersize/2; i+=2) {
        buffer[i] = 1024;
        buffer[i+1] = 1024;
    }

    for(i=0; i<filesize; i++) {
        for(j=0; j<10; j++) {
            // Write RS232 to buffer
            // 1 is 1024, 0 is 0, START is 0, END is 1, IDLE is 1
            // START - LSB - ... - MSB - END
            // Ten bits per byte in the file.
            if(j == 0)
                bit = 0;
            else if(j == 9)
                bit = 1;
            else
                bit = (fbuf[i] & (1<<(j - 1)))>>(j - 1);

            bit *= 1024;
            for(k=0; k<(SR/BD)*2; k+=2) {
                buffer[i * 10 * (SR/BD) * 2 + j * (SR/BD) * 2 + k] = bit;
                buffer[i * 10 * (SR/BD) * 2 + j * (SR/BD) * 2 + k + 1] = bit;
            }
        }
    }

    printf("Setting up BladeRF for synchronous transmission...\n");
    /*
    rv = bladerf_sync_config(blade, BLADERF_MODULE_TX, BLADERF_FORMAT_SC16_Q11,
                             10, buffersize/10, 8, 2 * buffersize * 1000);
    */
    rv = bladerf_sync_config(blade, BLADERF_MODULE_TX, BLADERF_FORMAT_SC16_Q11,
                             2, buffersize / 8, 1, 60000);
    if(rv < 0) {
        printf("Error setting BladeRF for synchronous transmission: %d\n", rv);
        return;
    }

    printf("Enabling TX\n");
    bladerf_enable_module(blade, BLADERF_MODULE_TX, 1);
    if(rv < 0) {
        printf("Error enabling TX module: %d\n", rv);
        return;
    }

    printf("Warming up transmitter...\n");
    rv = bladerf_sync_tx(blade, warmupbuf, warmupsize/4, NULL, 0);
    if(rv < 0) {
        printf("Error transmitting warmup: %d\n", rv);
        return;
    }

    printf("Transmitting data now...\n");
    for(i=0; i<10; i++) {
        rv = bladerf_sync_tx(blade, buffer, buffersize/4, NULL, 0);
        if(rv < 0) {
            printf("Error transmitting data: %d\n", rv);
            return;
        }
    }
}

int main(int argc, char* argv[])
{
    int rv;
    unsigned int actual_sr, actual_bw;
    struct bladerf_devinfo* devlist;

    if(argc != 2) {
        printf("Usage: %s <file to transmit>\n", argv[0]);
        return 1;
    }
    
    printf("Opening input file '%s'...\n", argv[1]);
    inf = fopen(argv[1], "r");
    if(inf == NULL) {
        printf("Error opening input file: %d.\n", errno);
        return 1;
    }

    printf("Getting list of BladeRF devices...\n");
    rv = bladerf_get_device_list(&devlist);
    if(rv < 0) {
        printf("Error getting list of BladeRF devices: %d.\n", rv);
        return 1;
    } else if(rv == 0) {
        printf("No BladeRF devices found.\n");
        return 1;
    }
    printf("Found %d device(s).\n", rv);
    
    printf("Opening first device...\n");
    rv = bladerf_open_with_devinfo(&blade, &devlist[0]);
    if(rv < 0) {
        printf("Error opening BladeRF device: %d\n", rv);
        return 1;
    }
    printf("Device opened.\n");

    bladerf_set_loopback(blade, BLADERF_LB_NONE);
    if(rv < 0) {
        printf("Error setting loopback mode: %d\n", rv);
        return 1;
    }

    bladerf_set_sample_rate(blade, BLADERF_MODULE_TX, SR, &actual_sr);
    if(rv < 0) {
        printf("Error setting sample rate: %d\n", rv);
        return 1;
    }
    printf("Set sample rate to %d\n", actual_sr);

    bladerf_set_sampling(blade, BLADERF_SAMPLING_INTERNAL);
    if(rv < 0) {
        printf("Error setting sampling mode: %d\n", rv);
        return 1;
    }

    bladerf_set_txvga2(blade, 9);
    if(rv < 0) {
        printf("Error setting PA gain: %d\n", rv);
        return 1;
    }

    bladerf_set_bandwidth(blade, BLADERF_MODULE_TX, SR/2, &actual_bw);
    if(rv < 0) {
        printf("Error setting bandwidth: %d\n", rv);
        return 1;
    }
    printf("Set bandwidth to %d\n", actual_bw);

    bladerf_set_lpf_mode(blade, BLADERF_MODULE_TX, BLADERF_LPF_NORMAL);
    if(rv < 0) {
        printf("Error setting LPF mode: %d\n", rv);
        return 1;
    }

    bladerf_select_band(blade, BLADERF_MODULE_TX, FREQ);
    if(rv < 0) {
        printf("Error setting frequency band: %d\n", rv);
        return 1;
    }

    bladerf_set_frequency(blade, BLADERF_MODULE_TX, FREQ);
    if(rv < 0) {
        printf("Error setting frequency: %d\n", rv);
        return 1;
    }

    transmit_file();

    bladerf_free_device_list(devlist);
    bladerf_close(blade);

    return 0;
}
