/*
 * Minimal PSP boot diagnostic.
 * Writes checkpoints to ms0:/jadepath_boot.log and shows text for 15 seconds.
 */

#include <pspkernel.h>
#include <pspdebug.h>

#include <stdio.h>

PSP_MODULE_INFO("JadePathHello", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(1024);

static int logStarted = 0;

static void logMessage(const char *message) {
    FILE *file = fopen("ms0:/jadepath_boot.log", logStarted ? "a" : "w");
    if (file != NULL) {
        fprintf(file, "%s\n", message);
        fflush(file);
        fclose(file);
        logStarted = 1;
    }
}

int main(int argc, char *argv[]) {
    logMessage("main: start");

    pspDebugScreenInit();
    logMessage("main: pspDebugScreenInit done");

    pspDebugScreenPrintf("Jade Path\n");
    pspDebugScreenPrintf("HELLO FROM PSP\n\n");
    pspDebugScreenPrintf("If you can read this,\n");
    pspDebugScreenPrintf("the new EBOOT is running.\n\n");
    pspDebugScreenPrintf("Screen will close in 15 seconds.\n");
    logMessage("main: text printed");

    sceKernelDelayThread(15 * 1000 * 1000);
    logMessage("main: delay finished");

    sceKernelExitGame();
    return 0;
}
