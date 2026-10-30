/*
 * Jade Path - native PSP/PPSSPP port, first playable build.
 *
 * 100 levels, 8x8 match-3 board, line/bomb specials, frozen orbs, lives,
 * level select, pause, hint and simple progress saving.
 * Rendering: GU primitives + PSP debug console text.
 */

#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspthreadman.h>
#include <pspiofilemgr.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("JadePath", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(8 * 1024);

#define SCREEN_W 480
#define SCREEN_H 272
#define BUF_W 512
#define ROWS 8
#define COLS 8
#define CELL 26
#define BOARD_X 136
#define BOARD_Y 40
#define MAX_LEVEL 100
#define EMPTY 255
#define LIST_SIZE 262144

#define SPECIAL_NONE 0
#define SPECIAL_BOMB 1
#define SPECIAL_ROW 2
#define SPECIAL_COL 3

#define ABGR(r, g, b) (0xFF000000u | ((unsigned int)(b) << 16) | ((unsigned int)(g) << 8) | (unsigned int)(r))
#define C_INK ABGR(4, 17, 20)
#define C_PANEL ABGR(9, 40, 38)
#define C_PANEL2 ABGR(7, 29, 30)
#define C_GOLD ABGR(216, 179, 104)
#define C_WHITE ABGR(255, 246, 222)
#define C_RED ABGR(238, 83, 98)
#define C_TEAL ABGR(78, 221, 181)
#define C_ICE ABGR(132, 223, 239)
#define C_DARK ABGR(2, 10, 12)

typedef struct {
    unsigned char color;
    unsigned char special;
    unsigned char frozen;
} Cell;

typedef struct {
    unsigned int magic;
    int unlocked;
    int lives;
    int bestScore[MAX_LEVEL];
    int bestTime[MAX_LEVEL];
} SaveData;

static unsigned int __attribute__((aligned(16))) displayList[LIST_SIZE];
static Cell board[ROWS][COLS];
static int mark[ROWS][COLS];
static int keep[ROWS][COLS];
static int newType[ROWS][COLS];

static const unsigned int palette[7] = {
    ABGR(238, 75, 92),
    ABGR(237, 172, 54),
    ABGR(57, 189, 120),
    ABGR(56, 152, 232),
    ABGR(152, 105, 233),
    ABGR(238, 118, 178),
    ABGR(89, 206, 224),
};

static const unsigned char frozenCounts[MAX_LEVEL] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 2, 3, 3, 4, 4, 5, 5, 6, 7,
    7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
    12, 13, 13, 14, 14, 15, 15, 16, 16, 17,
    16, 16, 17, 17, 18, 18, 18, 19, 19, 20,
    19, 19, 20, 20, 20, 21, 21, 21, 22, 22,
    19, 19, 20, 20, 20, 21, 21, 21, 22, 22,
    20, 20, 21, 21, 21, 21, 22, 22, 22, 22,
    20, 21, 21, 21, 21, 22, 22, 22, 22, 22,
};

enum {
    STATE_MENU,
    STATE_PLAY,
    STATE_PAUSE,
    STATE_WIN,
    STATE_LOSE
};

static int state = STATE_MENU;
static int currentLevel = 1;
static int selectedLevel = 1;
static int unlocked = 1;
static int lives = 3;
static int score = 0;
static int target = 4500;
static int colorCount = 5;
static int frozenTarget = 0;
static int chain = 1;
static int cursorRow = 3;
static int cursorCol = 3;
static int selected = 0;
static int selRow = 0;
static int selCol = 0;
static int hintActive = 0;
static int hintR1 = 0, hintC1 = 0, hintR2 = 0, hintC2 = 0;
static int elapsedSec = 0;
static unsigned long long levelStart = 0;
static char notice[64] = "";
static SaveData saveData;
static unsigned int rngState = 0x51A7BEEFu;

static void saveGame(void);

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }
static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static unsigned int rndNext(void) {
    unsigned int x = rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x;
    return x;
}

static int rndInt(int n) {
    return n > 0 ? (int)(rndNext() % (unsigned int)n) : 0;
}

static int inside(int r, int c) {
    return r >= 0 && r < ROWS && c >= 0 && c < COLS;
}

static int canSwap(int r, int c) {
    return inside(r, c) && board[r][c].color != EMPTY && !board[r][c].frozen;
}

static int levelTarget(int n) {
    if (n <= 1) return 4500;
    if (n <= 20) return 4500 + (n - 1) * 190;
    if (n <= 50) return 8300 + (n - 21) * 200;
    if (n <= 80) return 14300 + (n - 51) * 200;
    return 20300 + (n - 81) * 300;
}

static int levelColors(int n) {
    if (n <= 3) return 5;
    if (n <= 30) return 6;
    return 7;
}

static int levelFrozen(int n) {
    if (n < 1 || n > MAX_LEVEL) return 0;
    return frozenCounts[n - 1];
}

static void setNotice(const char *text) {
    strncpy(notice, text, sizeof(notice) - 1);
    notice[sizeof(notice) - 1] = '\0';
}

/* ------------------------------- drawing ------------------------------- */

typedef struct {
    unsigned int color;
    float x, y, z;
} Vertex;

static void drawRect(float x, float y, float w, float h, unsigned int color) {
    Vertex *v = (Vertex *)sceGuGetMemory(6 * sizeof(Vertex));
    if (v == NULL) return;

    v[0].color = color; v[0].x = x; v[0].y = y; v[0].z = 0.0f;
    v[1].color = color; v[1].x = x + w; v[1].y = y; v[1].z = 0.0f;
    v[2].color = color; v[2].x = x; v[2].y = y + h; v[2].z = 0.0f;
    v[3].color = color; v[3].x = x + w; v[3].y = y; v[3].z = 0.0f;
    v[4].color = color; v[4].x = x + w; v[4].y = y + h; v[4].z = 0.0f;
    v[5].color = color; v[5].x = x; v[5].y = y + h; v[5].z = 0.0f;

    sceGuDrawArray(
        GU_TRIANGLES,
        GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        6, NULL, v
    );
}

static void drawTile(int r, int c) {
    int x = BOARD_X + c * CELL;
    int y = BOARD_Y + r * CELL;
    unsigned int color;

    drawRect((float)x, (float)y, CELL, CELL, C_PANEL2);
    if (board[r][c].color == EMPTY) return;

    color = palette[board[r][c].color % 7];
    drawRect((float)(x + 2), (float)(y + 2), 22, 22, color);
    drawRect((float)(x + 3), (float)(y + 3), 20, 3, C_WHITE);
    drawRect((float)(x + 3), (float)(y + 19), 20, 3, C_DARK);

    if (board[r][c].special == SPECIAL_BOMB) {
        drawRect((float)(x + 8), (float)(y + 8), 10, 10, C_WHITE);
        drawRect((float)(x + 10), (float)(y + 10), 6, 6, color);
    } else if (board[r][c].special == SPECIAL_ROW) {
        drawRect((float)(x + 3), (float)(y + 11), 20, 4, C_WHITE);
    } else if (board[r][c].special == SPECIAL_COL) {
        drawRect((float)(x + 11), (float)(y + 3), 4, 20, C_WHITE);
    }

    if (board[r][c].frozen) {
        drawRect((float)(x + 1), (float)(y + 1), 24, 2, C_ICE);
        drawRect((float)(x + 1), (float)(y + 23), 24, 2, C_ICE);
        drawRect((float)(x + 1), (float)(y + 1), 2, 24, C_ICE);
        drawRect((float)(x + 23), (float)(y + 1), 2, 24, C_ICE);
    }

    if (hintActive &&
        ((r == hintR1 && c == hintC1) || (r == hintR2 && c == hintC2))) {
        drawRect((float)x, (float)y, CELL, 2, C_TEAL);
        drawRect((float)x, (float)(y + CELL - 2), CELL, 2, C_TEAL);
        drawRect((float)x, (float)y, 2, CELL, C_TEAL);
        drawRect((float)(x + CELL - 2), (float)y, 2, CELL, C_TEAL);
    }

    if (selected && r == selRow && c == selCol) {
        drawRect((float)x, (float)y, CELL, 2, C_TEAL);
        drawRect((float)x, (float)(y + CELL - 2), CELL, 2, C_TEAL);
        drawRect((float)x, (float)y, 2, CELL, C_TEAL);
        drawRect((float)(x + CELL - 2), (float)y, 2, CELL, C_TEAL);
    }

    if (r == cursorRow && c == cursorCol) {
        drawRect((float)x, (float)y, CELL, 3, C_GOLD);
        drawRect((float)x, (float)(y + CELL - 3), CELL, 3, C_GOLD);
        drawRect((float)x, (float)y, 3, CELL, C_GOLD);
        drawRect((float)(x + CELL - 3), (float)y, 3, CELL, C_GOLD);
    }
}

static void drawBoard(void) {
    drawRect(0, 0, SCREEN_W, SCREEN_H, C_INK);
    drawRect(0, 0, SCREEN_W, 3, C_GOLD);
    drawRect(0, SCREEN_H - 3, SCREEN_W, 3, C_GOLD);
    drawRect(0, 0, 4, SCREEN_H, C_GOLD);
    drawRect(SCREEN_W - 4, 0, 4, SCREEN_H, C_GOLD);

    drawRect(4, 38, 126, 208, C_PANEL);
    drawRect(350, 38, 126, 208, C_PANEL);
    drawRect(132, 36, 216, 216, C_PANEL);
    drawRect(132, 36, 216, 2, C_GOLD);
    drawRect(132, 250, 216, 2, C_GOLD);

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) drawTile(r, c);
    }

    drawRect(136, 33, 208, 3, C_DARK);
    if (target > 0) {
        float ratio = (float)score / (float)target;
        if (ratio > 1.0f) ratio = 1.0f;
        drawRect(136, 33, 208.0f * ratio, 3, C_GOLD);
    }
}

static void drawMenu(void) {
    drawRect(0, 0, SCREEN_W, SCREEN_H, C_INK);
    drawRect(0, 0, SCREEN_W, 4, C_GOLD);
    drawRect(0, SCREEN_H - 4, SCREEN_W, 4, C_GOLD);
    drawRect(4, 4, 4, SCREEN_H - 8, C_GOLD);
    drawRect(SCREEN_W - 8, 4, 4, SCREEN_H - 8, C_GOLD);
    drawRect(24, 34, 432, 2, C_GOLD);
    drawRect(24, 236, 432, 2, C_GOLD);
}

static void drawPauseOverlay(void) {
    drawRect(132, 38, 216, 208, C_PANEL);
    drawRect(136, 42, 208, 200, C_DARK);
    drawRect(136, 42, 208, 2, C_GOLD);
    drawRect(136, 240, 208, 2, C_GOLD);
}

static void drawResultOverlay(void) {
    drawRect(132, 38, 216, 208, C_PANEL);
    drawRect(136, 42, 208, 200, C_DARK);
    drawRect(136, 42, 208, 2, C_GOLD);
    drawRect(136, 240, 208, 2, C_GOLD);
}

/* -------------------------------- text -------------------------------- */

static void textLine(int row, const char *fmt, ...) {
    char buffer[96];
    char padded[64];
    va_list args;

    if (row < 0 || row > 33) return;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    snprintf(padded, sizeof(padded), "%-60.60s", buffer);
    pspDebugScreenSetXY(0, row);
    pspDebugScreenPrintf("%s", padded);
}

static void textAt(int col, int row, int width, const char *text) {
    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%-*.*s", width, width, text);
    pspDebugScreenSetXY(col, row);
    pspDebugScreenPrintf("%s", buffer);
}

static void formatTime(int seconds, char *out, int outSize) {
    if (seconds < 0) seconds = 0;
    snprintf(out, outSize, "%02d:%02d", seconds / 60, seconds % 60);
}

static void renderMenuText(void) {
    char timeText[16];
    int bestTime = saveData.bestTime[selectedLevel - 1];
    formatTime(bestTime, timeText, sizeof(timeText));

    textLine(2, "J A D E   P A T H");
    textLine(4, "A MATCH-3 JOURNEY");
    textLine(7, "LEVEL %02d", selectedLevel);
    textLine(9, "BEST %06d   BEST TIME %s", saveData.bestScore[selectedLevel - 1], timeText);
    textLine(12, "CROSS   START");
    textLine(14, "LEFT / RIGHT   SELECT LEVEL");
    textLine(16, "UP / DOWN      +/- 10 LEVELS");
    textLine(18, "TRIANGLE       HINT IN GAME");
    textLine(20, "START          PAUSE");
    textLine(23, "LIVES %d", lives);
    textLine(26, "MATCH 3 OR MORE TO SCORE");
    textLine(28, "4 = LINE  5 = BOMB  ICE = FROZEN");
}

static void renderPlayText(void) {
    char timeText[16];
    char bestText[16];
    char line[64];

    formatTime(elapsedSec, timeText, sizeof(timeText));
    formatTime(saveData.bestTime[currentLevel - 1], bestText, sizeof(bestText));

    textLine(0, "LEVEL %02d   SCORE %06d   TARGET %06d", currentLevel, score, target);
    textLine(1, "LIVES %d   CHAIN x%d   TIME %s", lives, chain, timeText);
    textLine(2, "BEST %06d   BEST TIME %s", saveData.bestScore[currentLevel - 1], bestText);
    textLine(3, "%s", notice);

    textAt(0, 5, 16, "JADE");
    textAt(0, 6, 16, "PATH");
    textAt(0, 8, 16, "LEVEL");
    snprintf(line, sizeof(line), "%02d", currentLevel);
    textAt(0, 9, 16, line);
    textAt(0, 11, 16, "TARGET");
    snprintf(line, sizeof(line), "%06d", target);
    textAt(0, 12, 16, line);
    textAt(0, 14, 16, "SCORE");
    snprintf(line, sizeof(line), "%06d", score);
    textAt(0, 15, 16, line);

    textAt(44, 5, 16, "MATCH 3+");
    textAt(44, 6, 16, "4 = LINE");
    textAt(44, 7, 16, "5 = BOMB");
    textAt(44, 9, 16, "ICE");
    textAt(44, 10, 16, "FROZEN");
    textAt(44, 12, 16, "TRI = HINT");
    textAt(44, 13, 16, "START PAUSE");

    textLine(31, "CROSS SELECT / SWAP   CIRCLE CLEAR   START PAUSE");
    textLine(32, "D-PAD MOVE CURSOR   TRIANGLE SHOW HINT");
    textLine(33, "MATCH 3 OR MORE. 4 GIVES LINE, 5 GIVES BOMB.");
}

static void renderText(void) {
    if (state == STATE_MENU) {
        renderMenuText();
        return;
    }

    if (state == STATE_PAUSE) {
        textLine(0, "PAUSED");
        textLine(2, "START   RESUME");
        textLine(4, "CIRCLE  RETURN TO MENU");
        textLine(8, "LEVEL %02d   SCORE %06d", currentLevel, score);
        textLine(10, "TARGET %06d", target);
        textAt(0, 5, 16, "");
        textAt(44, 5, 16, "");
        textLine(31, "");
        textLine(32, "");
        textLine(33, "");
        return;
    }

    if (state == STATE_WIN) {
        char timeText[16];
        formatTime(elapsedSec, timeText, sizeof(timeText));
        textLine(0, "LEVEL %02d CLEAR", currentLevel);
        textLine(2, "SCORE %06d / %06d", score, target);
        textLine(4, "TIME %s", timeText);
        if (currentLevel >= MAX_LEVEL) textLine(6, "CAMPAIGN COMPLETE");
        else textLine(6, "CROSS  NEXT LEVEL");
        textLine(8, "CIRCLE RETURN TO MENU");
        textAt(0, 5, 16, "");
        textAt(44, 5, 16, "");
        textLine(31, "");
        textLine(32, "");
        textLine(33, "");
        return;
    }

    if (state == STATE_LOSE) {
        textLine(0, "OUT OF LIVES");
        textLine(2, "LEVEL %02d   SCORE %06d", currentLevel, score);
        textLine(4, "CROSS   RETRY");
        textLine(6, "CIRCLE  RETURN TO MENU");
        textAt(0, 5, 16, "");
        textAt(44, 5, 16, "");
        textLine(31, "");
        textLine(32, "");
        textLine(33, "");
        return;
    }

    renderPlayText();
}

/* ------------------------------ game logic ----------------------------- */

static int hasMatchAt(int r, int c) {
    int color;
    int n;

    if (!inside(r, c)) return 0;
    color = board[r][c].color;
    if (color == EMPTY) return 0;

    n = 1;
    for (int i = c - 1; i >= 0 && board[r][i].color == color; i--) n++;
    for (int i = c + 1; i < COLS && board[r][i].color == color; i++) n++;
    if (n >= 3) return 1;

    n = 1;
    for (int i = r - 1; i >= 0 && board[i][c].color == color; i--) n++;
    for (int i = r + 1; i < ROWS && board[i][c].color == color; i++) n++;
    return n >= 3;
}

static void swapCells(int r1, int c1, int r2, int c2) {
    Cell temp = board[r1][c1];
    board[r1][c1] = board[r2][c2];
    board[r2][c2] = temp;
}

static int isLegalSwap(int r1, int c1, int r2, int c2) {
    int ok;
    if (!canSwap(r1, c1) || !canSwap(r2, c2)) return 0;
    if (iabs(r1 - r2) + iabs(c1 - c2) != 1) return 0;

    swapCells(r1, c1, r2, c2);
    ok = hasMatchAt(r1, c1) || hasMatchAt(r2, c2);
    swapCells(r1, c1, r2, c2);
    return ok;
}

static int hasLegalMove(void) {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (c + 1 < COLS && isLegalSwap(r, c, r, c + 1)) return 1;
            if (r + 1 < ROWS && isLegalSwap(r, c, r + 1, c)) return 1;
        }
    }
    return 0;
}

static int chooseCandidate(int r, int c, int len, int horizontal, int type) {
    int preferred = -1;
    int first = -1;

    for (int i = 0; i < len; i++) {
        int rr = r + (horizontal ? 0 : i);
        int cc = c + (horizontal ? i : 0);
        if (board[rr][cc].frozen) continue;
        if (first < 0) first = i;
        if (selected && rr == selRow && cc == selCol) preferred = i;
    }

    int i = preferred >= 0 ? preferred : first;
    if (i < 0) return 0;

    int rr = r + (horizontal ? 0 : i);
    int cc = c + (horizontal ? i : 0);
    if (keep[rr][cc]) return 0;

    keep[rr][cc] = 1;
    newType[rr][cc] = type;
    return 1;
}

static int findMatches(void) {
    int found = 0;

    memset(mark, 0, sizeof(mark));
    memset(keep, 0, sizeof(keep));
    memset(newType, 0, sizeof(newType));

    for (int r = 0; r < ROWS; r++) {
        int c = 0;
        while (c < COLS) {
            int color = board[r][c].color;
            int len = 1;
            if (color == EMPTY) { c++; continue; }
            while (c + len < COLS && board[r][c + len].color == color) len++;
            if (len >= 3) {
                for (int i = 0; i < len; i++) mark[r][c + i] = 1;
                if (len >= 4) chooseCandidate(r, c, len, 1, len >= 5 ? SPECIAL_BOMB : SPECIAL_ROW);
                found = 1;
            }
            c += len;
        }
    }

    for (int c = 0; c < COLS; c++) {
        int r = 0;
        while (r < ROWS) {
            int color = board[r][c].color;
            int len = 1;
            if (color == EMPTY) { r++; continue; }
            while (r + len < ROWS && board[r + len][c].color == color) len++;
            if (len >= 3) {
                for (int i = 0; i < len; i++) mark[r + i][c] = 1;
                if (len >= 4) chooseCandidate(r, c, len, 0, len >= 5 ? SPECIAL_BOMB : SPECIAL_COL);
                found = 1;
            }
            r += len;
        }
    }

    return found;
}

static void markArea(int r, int c, int type) {
    if (type == SPECIAL_BOMB) {
        for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
                int rr = r + dr, cc = c + dc;
                if (inside(rr, cc) && !keep[rr][cc]) mark[rr][cc] = 1;
            }
        }
    } else if (type == SPECIAL_ROW) {
        for (int cc = 0; cc < COLS; cc++) if (!keep[r][cc]) mark[r][cc] = 1;
    } else if (type == SPECIAL_COL) {
        for (int rr = 0; rr < ROWS; rr++) if (!keep[rr][c]) mark[rr][c] = 1;
    }
}

static int clearMarked(int chainValue) {
    int activated[ROWS][COLS];
    int cleared = 0;
    int changed;

    memset(activated, 0, sizeof(activated));

    do {
        changed = 0;
        for (int r = 0; r < ROWS; r++) {
            for (int c = 0; c < COLS; c++) {
                if (!mark[r][c] || keep[r][c] || !board[r][c].special || activated[r][c]) continue;
                activated[r][c] = 1;
                markArea(r, c, board[r][c].special);
                changed = 1;
            }
        }
    } while (changed);

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (!mark[r][c] || keep[r][c]) continue;
            if (board[r][c].color == EMPTY) continue;
            score += 100 * chainValue;
            if (board[r][c].frozen) score += 50;
            board[r][c].color = EMPTY;
            board[r][c].special = SPECIAL_NONE;
            board[r][c].frozen = 0;
            cleared++;
        }
    }

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (keep[r][c] && newType[r][c] != SPECIAL_NONE) {
                board[r][c].special = (unsigned char)newType[r][c];
            }
        }
    }

    return cleared;
}

static void collapseBoard(void) {
    for (int c = 0; c < COLS; c++) {
        int dst = ROWS - 1;
        for (int src = ROWS - 1; src >= 0; src--) {
            if (board[src][c].color == EMPTY) continue;
            if (dst != src) board[dst][c] = board[src][c];
            dst--;
        }
        while (dst >= 0) {
            board[dst][c].color = (unsigned char)rndInt(colorCount);
            board[dst][c].special = SPECIAL_NONE;
            board[dst][c].frozen = 0;
            dst--;
        }
    }
}

static void settleBoard(void) {
    chain = 1;
    while (findMatches()) {
        int cleared = clearMarked(chain);
        if (cleared == 0) break;
        collapseBoard();
        chain++;
        if (chain > 20) break;
    }
}

static void generateBoard(int colors, int frozen) {
    for (int attempt = 0; attempt < 80; attempt++) {
        for (int r = 0; r < ROWS; r++) {
            for (int c = 0; c < COLS; c++) {
                int color;
                int guard = 0;
                do {
                    color = rndInt(colors);
                    guard++;
                } while (guard < 40 &&
                         ((c >= 2 && board[r][c - 1].color == color && board[r][c - 2].color == color) ||
                          (r >= 2 && board[r - 1][c].color == color && board[r - 2][c].color == color)));
                board[r][c].color = (unsigned char)color;
                board[r][c].special = SPECIAL_NONE;
                board[r][c].frozen = 0;
            }
        }

        for (int i = 0; i < frozen; ) {
            int r = rndInt(ROWS);
            int c = rndInt(COLS);
            if (board[r][c].frozen) continue;
            board[r][c].frozen = 1;
            i++;
        }

        if (hasLegalMove()) return;
    }
}

static void startLevel(int level) {
    currentLevel = clampInt(level, 1, MAX_LEVEL);
    target = levelTarget(currentLevel);
    colorCount = levelColors(currentLevel);
    frozenTarget = levelFrozen(currentLevel);
    score = 0;
    chain = 1;
    selected = 0;
    hintActive = 0;
    cursorRow = 3;
    cursorCol = 3;
    notice[0] = '\0';
    elapsedSec = 0;
    if (lives <= 0) lives = 3;
    levelStart = (unsigned long long)sceKernelGetSystemTimeWide();
    generateBoard(colorCount, frozenTarget);
    state = STATE_PLAY;
}

static void finishWin(void) {
    if (score > saveData.bestScore[currentLevel - 1]) {
        saveData.bestScore[currentLevel - 1] = score;
    }
    if (saveData.bestTime[currentLevel - 1] == 0 || elapsedSec < saveData.bestTime[currentLevel - 1]) {
        saveData.bestTime[currentLevel - 1] = elapsedSec;
    }
    if (currentLevel + 1 > unlocked) unlocked = currentLevel + 1;
    if (unlocked > MAX_LEVEL) unlocked = MAX_LEVEL;
    saveData.unlocked = unlocked;
    saveData.lives = lives;
    saveGame();
    state = STATE_WIN;
}

static void finishLose(void) {
    saveData.unlocked = unlocked;
    saveData.lives = lives;
    saveGame();
    state = STATE_LOSE;
}

static void handleNoMoves(void) {
    lives--;
    if (lives <= 0) {
        lives = 0;
        finishLose();
        return;
    }
    setNotice("NO MOVES - GARDEN RESHUFFLED");
    generateBoard(colorCount, frozenTarget);
}

static void afterMove(void) {
    if (score >= target) {
        finishWin();
        return;
    }
    if (!hasLegalMove()) handleNoMoves();
}

static void findHint(void) {
    hintActive = 0;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (c + 1 < COLS && isLegalSwap(r, c, r, c + 1)) {
                hintR1 = r; hintC1 = c; hintR2 = r; hintC2 = c + 1;
                hintActive = 1;
                setNotice("HINT: TRY THE MARKED PAIR");
                return;
            }
            if (r + 1 < ROWS && isLegalSwap(r, c, r + 1, c)) {
                hintR1 = r; hintC1 = c; hintR2 = r + 1; hintC2 = c;
                hintActive = 1;
                setNotice("HINT: TRY THE MARKED PAIR");
                return;
            }
        }
    }
    setNotice("NO HINT - PRESS TRIANGLE AGAIN");
}

/* -------------------------------- save --------------------------------- */

static void ensureSaveDir(void) {
    sceIoMkdir("ms0:/PSP/SAVEDATA", 0777);
    sceIoMkdir("ms0:/PSP/SAVEDATA/JadePath", 0777);
}

static void saveGame(void) {
    FILE *file;
    saveData.magic = 0x4A505331u;
    saveData.unlocked = unlocked;
    saveData.lives = lives;
    ensureSaveDir();
    file = fopen("ms0:/PSP/SAVEDATA/JadePath/save.bin", "wb");
    if (file == NULL) return;
    fwrite(&saveData, sizeof(saveData), 1, file);
    fclose(file);
}

static void loadGame(void) {
    FILE *file;
    SaveData loaded;

    memset(&saveData, 0, sizeof(saveData));
    saveData.unlocked = 1;
    saveData.lives = 3;

    file = fopen("ms0:/PSP/SAVEDATA/JadePath/save.bin", "rb");
    if (file == NULL) return;
    if (fread(&loaded, sizeof(loaded), 1, file) != 1) {
        fclose(file);
        return;
    }
    fclose(file);
    if (loaded.magic != 0x4A505331u) return;

    saveData = loaded;
    unlocked = clampInt(saveData.unlocked, 1, MAX_LEVEL);
    lives = clampInt(saveData.lives, 1, 3);
    saveData.unlocked = unlocked;
    saveData.lives = lives;
}

/* -------------------------------- input -------------------------------- */

static unsigned int readPressed(void) {
    static unsigned int previous = 0;
    SceCtrlData pad;
    unsigned int pressed;

    if (sceCtrlReadBufferPositive(&pad, 1) < 0) return 0;
    pressed = pad.Buttons & ~previous;
    previous = pad.Buttons;
    return pressed;
}

static void moveCursor(int dr, int dc) {
    cursorRow = (cursorRow + dr + ROWS) % ROWS;
    cursorCol = (cursorCol + dc + COLS) % COLS;
    hintActive = 0;
    notice[0] = '\0';
}

static void handleCross(void) {
    if (!selected) {
        if (canSwap(cursorRow, cursorCol)) {
            selected = 1;
            selRow = cursorRow;
            selCol = cursorCol;
        }
        return;
    }

    if (cursorRow == selRow && cursorCol == selCol) {
        selected = 0;
        return;
    }

    if (iabs(cursorRow - selRow) + iabs(cursorCol - selCol) == 1) {
        int r1 = selRow, c1 = selCol;
        int r2 = cursorRow, c2 = cursorCol;
        selected = 0;
        hintActive = 0;
        if (canSwap(r1, c1) && canSwap(r2, c2)) {
            swapCells(r1, c1, r2, c2);
            if (!findMatches()) {
                swapCells(r1, c1, r2, c2);
                setNotice("NO MATCH - TRY ANOTHER SWAP");
            } else {
                settleBoard();
                notice[0] = '\0';
                afterMove();
            }
        }
        return;
    }

    if (canSwap(cursorRow, cursorCol)) {
        selRow = cursorRow;
        selCol = cursorCol;
        selected = 1;
    } else {
        selected = 0;
    }
}

static void updateGame(unsigned int pressed) {
    if (state == STATE_MENU) {
        if (pressed & PSP_CTRL_LEFT) selectedLevel--;
        if (pressed & PSP_CTRL_RIGHT) selectedLevel++;
        if (pressed & PSP_CTRL_UP) selectedLevel -= 10;
        if (pressed & PSP_CTRL_DOWN) selectedLevel += 10;
        selectedLevel = clampInt(selectedLevel, 1, unlocked);
        if (pressed & PSP_CTRL_CROSS) startLevel(selectedLevel);
        return;
    }

    if (state == STATE_PAUSE) {
        if (pressed & PSP_CTRL_START) {
            state = STATE_PLAY;
            levelStart = (unsigned long long)sceKernelGetSystemTimeWide() - (unsigned long long)elapsedSec * 1000000ull;
        }
        if (pressed & PSP_CTRL_CIRCLE) {
            selectedLevel = currentLevel;
            state = STATE_MENU;
        }
        return;
    }

    if (state == STATE_WIN) {
        if (pressed & PSP_CTRL_CROSS) {
            if (currentLevel < MAX_LEVEL) startLevel(currentLevel + 1);
            else { selectedLevel = MAX_LEVEL; state = STATE_MENU; }
        }
        if (pressed & PSP_CTRL_CIRCLE) {
            selectedLevel = currentLevel;
            state = STATE_MENU;
        }
        return;
    }

    if (state == STATE_LOSE) {
        if (pressed & PSP_CTRL_CROSS) {
            lives = 3;
            startLevel(currentLevel);
        }
        if (pressed & PSP_CTRL_CIRCLE) {
            selectedLevel = currentLevel;
            state = STATE_MENU;
        }
        return;
    }

    if (pressed & PSP_CTRL_START) {
        state = STATE_PAUSE;
        return;
    }
    if (pressed & PSP_CTRL_TRIANGLE) {
        findHint();
        return;
    }
    if (pressed & PSP_CTRL_CIRCLE) {
        selected = 0;
        hintActive = 0;
        return;
    }
    if (pressed & PSP_CTRL_LEFT) moveCursor(0, -1);
    if (pressed & PSP_CTRL_RIGHT) moveCursor(0, 1);
    if (pressed & PSP_CTRL_UP) moveCursor(-1, 0);
    if (pressed & PSP_CTRL_DOWN) moveCursor(1, 0);
    if (pressed & PSP_CTRL_CROSS) handleCross();

    if (state == STATE_PLAY && levelStart != 0) {
        unsigned long long now = (unsigned long long)sceKernelGetSystemTimeWide();
        elapsedSec = (int)((now - levelStart) / 1000000ull);
    }
}

/* ------------------------------ GU helpers ----------------------------- */

static void guInit(void) {
    void *fbp0 = guGetStaticVramBuffer(BUF_W, SCREEN_H, GU_PSM_8888);
    void *fbp1 = guGetStaticVramBuffer(BUF_W, SCREEN_H, GU_PSM_8888);
    void *zbp = guGetStaticVramBuffer(BUF_W, SCREEN_H, GU_PSM_4444);

    sceGuInit();
    sceGuStart(GU_DIRECT, displayList);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_W);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, fbp1, BUF_W);
    sceGuDepthBuffer(zbp, BUF_W);
    sceGuOffset(2048 - (SCREEN_W / 2), 2048 - (SCREEN_H / 2));
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuDepthRange(0xC350, 0x2710);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuClearColor(C_INK);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void renderFrame(void) {
    sceGuStart(GU_DIRECT, displayList);
    sceGuClearColor(C_INK);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    if (state == STATE_MENU) {
        drawMenu();
    } else {
        drawBoard();
        if (state == STATE_PAUSE) drawPauseOverlay();
        if (state == STATE_WIN || state == STATE_LOSE) drawResultOverlay();
    }

    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();

    renderText();
}

/* ------------------------------- callbacks ----------------------------- */

static int exitCallback(int arg1, int arg2, void *common) {
    sceKernelExitGame();
    return 0;
}

static int callbackThread(SceSize args, void *argp) {
    int callbackId = sceKernelCreateCallback("JadePath Exit Callback", exitCallback, NULL);
    if (callbackId >= 0) sceKernelRegisterExitCallback(callbackId);
    sceKernelSleepThreadCB();
    return 0;
}

static int setupCallbacks(void) {
    int threadId = sceKernelCreateThread("JadePath update thread", callbackThread, 0x11, 0xFA0, 0, NULL);
    if (threadId >= 0) sceKernelStartThread(threadId, 0, NULL);
    return threadId;
}

int main(int argc, char *argv[]) {
    setupCallbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    pspDebugScreenInit();
    loadGame();
    selectedLevel = unlocked;
    guInit();

    while (1) {
        unsigned int pressed = readPressed();
        updateGame(pressed);
        renderFrame();
    }

    sceGuDisplay(GU_FALSE);
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
