#include "mbox.h"
#include "peripherals/mbox.h"
#include "uart0.h"

unsigned int width, height, pitch, isrgb; /* dimensions and channel order */
unsigned char *fb;                        /* raw frame buffer address */

void fb_init() {
    unsigned int __attribute__((aligned(16))) mbox[35];

    mbox[0] = 35 * 4;
    mbox[1] = MBOX_CODE_BUF_REQ;

    // set physical width / height
    mbox[2] = MBOX_TAG_SET_PHY_WIDTH_HEIGHT;
    mbox[3] = 8;
    mbox[4] = MBOX_CODE_TAG_REQ;
    mbox[5] = 960;  // width in pixels
    mbox[6] = 720;  // height in pixels

    // set virtual width / height
    mbox[7] = MBOX_TAG_SET_VTL_WIDTH_HEIGHT;
    mbox[8] = 8;
    mbox[9] = MBOX_CODE_TAG_REQ;
    mbox[10] = 920;  // width in pixels
    mbox[11] = 720;  // height in pixels

    // set virtual offset
    mbox[12] = MBOX_TAG_SET_VTL_OFFSET;
    mbox[13] = 8;
    mbox[14] = MBOX_CODE_TAG_REQ;
    mbox[15] = 0;  // X in pixels
    mbox[16] = 0;  // Y in pixels

    // set depth
    mbox[17] = MBOX_TAG_SET_DEPTH;
    mbox[18] = 4;
    mbox[19] = MBOX_CODE_TAG_REQ;
    mbox[20] = 32;  // bits per pixel

    // set pixel order
    mbox[21] = MBOX_TAG_SET_PIXEL_ORDER;
    mbox[22] = 4;
    mbox[23] = MBOX_CODE_TAG_REQ;
    mbox[24] = 1;  // 0x0: BGR, 0x1: RGB

    // allocate buffer
    mbox[25] = MBOX_TAG_ALLOCATE_BUFFER;
    mbox[26] = 8;
    mbox[27] = MBOX_CODE_TAG_REQ;
    mbox[28] = 4096;  // req: alignment in bytes / res: frame buffer base address in bytes
    mbox[29] = 0;     // frame buffer size in bytes

    // get pitch
    mbox[30] = MBOX_TAG_GET_PITCH;
    mbox[31] = 4;
    mbox[32] = MBOX_CODE_TAG_REQ;
    mbox[33] = 0;  // res: bytes per line

    mbox[34] = 0x0;

    mbox_call(mbox, 8);

    if (mbox[20] == 32 && mbox[28] != 0) {
        mbox[28] &= 0x3FFFFFFF;  // convert GPU address to ARM address
        width = mbox[5];         // get actual physical width
        height = mbox[6];        // get actual physical height
        pitch = mbox[33];        // get number of bytes per line
        isrgb = mbox[24];        // get the actual channel order
        fb = (void *)((unsigned long)mbox[28]);
    }
    else {
        uart_printf("Unable to set screen resolution to 1024x768x32\n");
    }
}

void fb_splash() {
    unsigned int x, y;
    unsigned char *ptr = fb;
    unsigned int white = 255 << 16 | 255 << 8 | 255;  // A B G R
    unsigned int black = 0;
    unsigned int current, start = black, spacing = 40;

    for (y = 0; y < height; y++) {
        if (y % spacing == 0 && y != 0) {
            start = (start == white) ? black : white;
        }
        current = start;
        for (x = 0; x < width; x++) {
            if (x % spacing == 0 && x != 0) {
                current = (current == white) ? black : white;
            }
            *((unsigned int *)ptr) = current;
            ptr += 4;
        }
    }
}