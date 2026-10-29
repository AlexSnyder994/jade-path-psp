/*
 * Raw PSP display test.
 * No pspDebug, no GU, no callbacks, no file I/O, no heap.
 * Draws a jade/gold checkerboard directly into the framebuffer.
 */

#include <pspkernel.h>
#include <pspdisplay.h>

PSP_MODULE_INFO("JadePathRaw", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH 512

static unsigned int __attribute__((aligned(16))) framebuffer[BUF_WIDTH * SCREEN_HEIGHT];

static unsigned int abgr(unsigned int r, unsigned int g, unsigned int b) {
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

int main(int argc, char *argv[]) {
    const unsigned int ink = abgr(4, 17, 20);
    const unsigned int jade = abgr(57, 189, 120);
    const unsigned int gold = abgr(216, 179, 104);

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            unsigned int color;
            if (x < 8 || x >= SCREEN_WIDTH - 8 || y < 8 || y >= SCREEN_HEIGHT - 8) {
                color = gold;
            } else if (((x / 40) + (y / 40)) & 1) {
                color = jade;
            } else {
                color = ink;
            }
            framebuffer[y * BUF_WIDTH + x] = color;
        }
    }

    sceKernelDcacheWritebackAll();
    sceDisplaySetMode(0, SCREEN_WIDTH, SCREEN_HEIGHT);
    sceDisplaySetFrameBuf(
        framebuffer,
        BUF_WIDTH,
        PSP_DISPLAY_PIXEL_FORMAT_8888,
        PSP_DISPLAY_SETBUF_NEXTFRAME
    );
    sceDisplayWaitVblankStart();

    sceKernelDelayThread(10 * 1000 * 1000);
    sceKernelExitGame();
    return 0;
}
