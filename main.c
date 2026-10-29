/*
 * Absolute minimum PSP homebrew test.
 * No GU, no callbacks, no file I/O, no custom heap.
 */

#include <pspkernel.h>
#include <pspdebug.h>

PSP_MODULE_INFO("JadePathMin", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

int main(int argc, char *argv[]) {
    pspDebugScreenInit();
    pspDebugScreenPrintf("Jade Path MINIMAL\n\n");
    pspDebugScreenPrintf("If this text is visible,\n");
    pspDebugScreenPrintf("the pspdev build runs.\n\n");
    pspDebugScreenPrintf("Closing in 10 seconds...\n");

    sceKernelDelayThread(10 * 1000 * 1000);
    sceKernelExitGame();
    return 0;
}
