/*
 * «Нефритовый путь» — PSP build smoke test.
 *
 * This is the first CI checkpoint for the native PSP port. It initialises the
 * GU, draws a landscape 7x7 board preview and exercises the D-pad / Cross /
 * Start controls. The same display pipeline will be used by the full game.
 */

#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspthreadman.h>

#include <stdio.h>

PSP_MODULE_INFO("JadePath", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(16 * 1024);

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
#define BUF_WIDTH 512
#define LIST_SIZE 262144

#define ABGR(r, g, b) (0xFF000000u | ((unsigned int)(b) << 16) | ((unsigned int)(g) << 8) | (unsigned int)(r))
#define COLOR_INK ABGR(4, 17, 20)
#define COLOR_PANEL ABGR(8, 39, 38)
#define COLOR_GOLD ABGR(216, 179, 104)
#define COLOR_TEXT ABGR(255, 245, 222)
#define COLOR_DANGER ABGR(238, 83, 98)

#define BOARD_COLS 7
#define BOARD_ROWS 7
#define CELL_SIZE 30
#define TILE_SIZE 24
#define BOARD_X 135
#define BOARD_Y 31

static unsigned int __attribute__((aligned(16))) displayList[LIST_SIZE];

static const unsigned int tileColors[5] = {
    ABGR(238, 75, 92),    /* рубин */
    ABGR(237, 172, 54),   /* янтарь */
    ABGR(57, 189, 120),   /* нефрит */
    ABGR(56, 152, 232),   /* лазурь */
    ABGR(152, 105, 233),  /* аметист */
};

static void drawRect(float x, float y, float width, float height, unsigned int color) {
    typedef struct {
        unsigned int color;
        float x;
        float y;
        float z;
    } Vertex;

    Vertex *vertices = (Vertex *)sceGuGetMemory(6 * sizeof(Vertex));
    if (vertices == NULL) return;

    vertices[0].color = color;
    vertices[0].x = x;
    vertices[0].y = y;
    vertices[0].z = 0.0f;
    vertices[1].color = color;
    vertices[1].x = x + width;
    vertices[1].y = y;
    vertices[1].z = 0.0f;
    vertices[2].color = color;
    vertices[2].x = x;
    vertices[2].y = y + height;
    vertices[2].z = 0.0f;

    vertices[3].color = color;
    vertices[3].x = x + width;
    vertices[3].y = y;
    vertices[3].z = 0.0f;
    vertices[4].color = color;
    vertices[4].x = x + width;
    vertices[4].y = y + height;
    vertices[4].z = 0.0f;
    vertices[5].color = color;
    vertices[5].x = x;
    vertices[5].y = y + height;
    vertices[5].z = 0.0f;

    sceGuDrawArray(
        GU_TRIANGLES,
        GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        6,
        NULL,
        vertices
    );
}

static void drawBoard(int selectedRow, int selectedCol) {
    drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_INK);

    /* Верхняя декоративная полоса. */
    drawRect(0, 0, SCREEN_WIDTH, 4, COLOR_GOLD);
    drawRect(16, 9, 92, 3, COLOR_GOLD);
    drawRect(372, 9, 92, 3, COLOR_GOLD);

    /* Панель поля. */
    drawRect(BOARD_X - 7, BOARD_Y - 7, BOARD_COLS * CELL_SIZE + 14, BOARD_ROWS * CELL_SIZE + 14, COLOR_PANEL);
    drawRect(BOARD_X - 8, BOARD_Y - 8, BOARD_COLS * CELL_SIZE + 16, 1, COLOR_GOLD);

    for (int row = 0; row < BOARD_ROWS; row++) {
        for (int col = 0; col < BOARD_COLS; col++) {
            int colorIndex = (row * 3 + col * 2) % 5;
            float x = (float)(BOARD_X + col * CELL_SIZE + (CELL_SIZE - TILE_SIZE) / 2);
            float y = (float)(BOARD_Y + row * CELL_SIZE + (CELL_SIZE - TILE_SIZE) / 2);
            drawRect(x, y, TILE_SIZE, TILE_SIZE, tileColors[colorIndex]);
        }
    }

    /* Курсор. */
    float cursorX = (float)(BOARD_X + selectedCol * CELL_SIZE);
    float cursorY = (float)(BOARD_Y + selectedRow * CELL_SIZE);
    drawRect(cursorX - 1, cursorY - 1, CELL_SIZE + 2, 2, COLOR_GOLD);
    drawRect(cursorX - 1, cursorY + CELL_SIZE - 1, CELL_SIZE + 2, 2, COLOR_GOLD);
    drawRect(cursorX - 1, cursorY - 1, 2, CELL_SIZE + 2, COLOR_GOLD);
    drawRect(cursorX + CELL_SIZE - 1, cursorY - 1, 2, CELL_SIZE + 2, COLOR_GOLD);

    /* Три «жизни» в правом верхнем углу. */
    drawRect(432, 14, 10, 10, COLOR_DANGER);
    drawRect(447, 14, 10, 10, COLOR_DANGER);
    drawRect(462, 14, 10, 10, COLOR_DANGER);

    /* Нижняя подсказка-полоска — просто визуальный маркер сборки. */
    drawRect(16, 258, 448, 1, COLOR_GOLD);
    drawRect(16, 262, 94, 2, COLOR_GOLD);
    drawRect(370, 262, 94, 2, COLOR_GOLD);
}

static int exitCallback(int arg1, int arg2, void *common) {
    sceKernelExitGame();
    return 0;
}

static int callbackThread(SceSize args, void *argp) {
    int callbackId = sceKernelCreateCallback("JadePath Exit Callback", exitCallback, NULL);
    if (callbackId >= 0) {
        sceKernelRegisterExitCallback(callbackId);
    }
    sceKernelSleepThreadCB();
    return 0;
}

static int setupCallbacks(void) {
    int threadId = sceKernelCreateThread(
        "JadePath update thread",
        callbackThread,
        0x11,
        0xFA0,
        0,
        NULL
    );
    if (threadId >= 0) {
        sceKernelStartThread(threadId, 0, NULL);
    }
    return threadId;
}

int main(int argc, char *argv[]) {
    setupCallbacks();

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    sceGuInit();
    sceGuStart(GU_DIRECT, displayList);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_WIDTH);
    sceGuDispBuffer(SCREEN_WIDTH, SCREEN_HEIGHT, (void *)0x88000, BUF_WIDTH);
    sceGuDepthBuffer((void *)0x110000, BUF_WIDTH);
    sceGuOffset(2048 - (SCREEN_WIDTH / 2), 2048 - (SCREEN_HEIGHT / 2));
    sceGuViewport(2048, 2048, SCREEN_WIDTH, SCREEN_HEIGHT);
    sceGuDepthRange(0xC350, 0xFFFF);
    sceGuScissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuClearColor(COLOR_INK);
    sceGuClearDepth(0);
    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    int selectedRow = 0;
    int selectedCol = 0;
    unsigned int previousButtons = 0;
    int running = 1;

    while (running) {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~previousButtons;
        previousButtons = pad.Buttons;

        if (pressed & PSP_CTRL_LEFT) {
            selectedCol = (selectedCol + BOARD_COLS - 1) % BOARD_COLS;
        }
        if (pressed & PSP_CTRL_RIGHT) {
            selectedCol = (selectedCol + 1) % BOARD_COLS;
        }
        if (pressed & PSP_CTRL_UP) {
            selectedRow = (selectedRow + BOARD_ROWS - 1) % BOARD_ROWS;
        }
        if (pressed & PSP_CTRL_DOWN) {
            selectedRow = (selectedRow + 1) % BOARD_ROWS;
        }
        if (pressed & PSP_CTRL_START) {
            running = 0;
        }

        sceGuStart(GU_DIRECT, displayList);
        sceGuClearColor(COLOR_INK);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        drawBoard(selectedRow, selectedCol);
        sceGuFinish();
        sceGuSync(0, 0);
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }

    sceGuDisplay(GU_FALSE);
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
