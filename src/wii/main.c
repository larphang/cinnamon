#include "../data_win.h"
#include "../vm.h"
#include "../runner.h"
#include "../runner_keyboard.h"

#include "../noop_audio_system.h"
#include "../noop_file_system.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include <fat.h>
#include <sys/stat.h>
#include <ogc/tpl.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

#include "wii_renderer.h"

typedef struct {
    
} WiiLoadingState;

typedef struct {
    uint32_t wpadButton;
    int32_t gmlKey;
} WiiWiimoteKeyMap;

typedef struct {
    bool keyHeld[GML_KEY_COUNT];
} WiiInputState;

static const WiiWiimoteKeyMap WII_WIIMOTE_HORIZONTAL_KEY_MAPS[] = {
    { WPAD_BUTTON_LEFT, VK_DOWN },
    { WPAD_BUTTON_RIGHT, VK_UP },
    { WPAD_BUTTON_UP, VK_LEFT },
    { WPAD_BUTTON_DOWN, VK_RIGHT },
    { WPAD_BUTTON_PLUS, 'C' },
    { WPAD_BUTTON_MINUS, 'C' },
    { WPAD_BUTTON_2, 'Z' },
    { WPAD_BUTTON_1, 'X' },
};

static int32_t resolveMappedKey(const RunnerKeyboardState* keyboard, int32_t gmlKey) {
    (void) keyboard;
    return gmlKey;
}

static void setDesiredKeyState(bool* desiredKeys, const RunnerKeyboardState* keyboard, int32_t gmlKey, bool isHeld) {
    int32_t mappedKey = resolveMappedKey(keyboard, gmlKey);
    if (mappedKey >= 0 && mappedKey < GML_KEY_COUNT && isHeld) {
        desiredKeys[mappedKey] = true;
    }
}

static void accumulateWiimoteButtonsToDesiredKeys(bool* desiredKeys, RunnerKeyboardState* keyboard, uint32_t held) {
    repeat(sizeof(WII_WIIMOTE_HORIZONTAL_KEY_MAPS) / sizeof(WII_WIIMOTE_HORIZONTAL_KEY_MAPS[0]), i) {
        setDesiredKeyState(
            desiredKeys,
            keyboard,
            WII_WIIMOTE_HORIZONTAL_KEY_MAPS[i].gmlKey,
            (held & WII_WIIMOTE_HORIZONTAL_KEY_MAPS[i].wpadButton) != 0
        );
    }
}

static void syncDesiredKeysToKeyboard(WiiInputState* inputState, RunnerKeyboardState* keyboard, const bool* desiredKeys) {
    repeat(GML_KEY_COUNT, key) {
        bool isHeld = desiredKeys[key];
        bool wasHeld = inputState->keyHeld[key];
        if (isHeld && !wasHeld) {
            RunnerKeyboard_onKeyDown(keyboard, key);
        } else if (!isHeld && wasHeld) {
            RunnerKeyboard_onKeyUp(keyboard, key);
        }
        inputState->keyHeld[key] = isHeld;
    }
}

bool fileExistsAtPath(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static char* duplicateDirname(const char* path) {
    const char* lastSlash = strrchr(path, '/');
    if (lastSlash == NULL) return strdup(".");
    size_t length = (size_t) (lastSlash - path);
    char* dir = malloc(length + 1);
    memcpy(dir, path, length);
    dir[length] = '\0';
    return dir;
}

static char* buildDefaultDataWinPath(const char* argv0) {
    if (fileExistsAtPath("sd:/cinnamon/data.win")) {
        return strdup("sd:/cinnamon/data.win");
    }

    if (fileExistsAtPath("usb:/cinnamon/data.win")) {
        return strdup("usb:/cinnamon/data.win");
    }

    char* dir = duplicateDirname(argv0);
    size_t dirLen = strlen(dir);
    const char suffix[] = "/data.win";

    char* result = malloc(dirLen + sizeof(suffix));
    memcpy(result, dir, dirLen);
    memcpy(result + dirLen, suffix, sizeof(suffix));

    bool exists = fileExistsAtPath(result);

    free(dir);
    return result;
}

static void WiiDataWinProgressCallback(const char* chunkName, int chunkIndex, int totalChunks, MAYBE_UNUSED DataWin* datawin, void* userData) {
    return;
}

static int32_t clampRenderDimension(int32_t value, int32_t fallback, int32_t maxValue) {
    if (value <= 0) value = fallback;
    if (value > maxValue) value = maxValue;
    return value;
}

int main(int argc, char* argv[]) {
    DataWin* dataWin = NULL;
    Runner* runner = NULL;
    Renderer* renderer = NULL;
    FileSystem* fileSystem = NULL;
    AudioSystem* audioSystem = NULL;
    VMContext* vm = NULL;

    char* dataWinPath = NULL;
    void* loadingThreadStack = NULL;

    fatInitDefault();

    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    
    printf("\x1b[2;0H");

	printf("Hello World!\n");

    dataWinPath = argc > 1 ? strdup(argv[1]) : buildDefaultDataWinPath(argv[0]);

    WiiLoadingState loadingState;

    dataWin = DataWin_parse(
        dataWinPath,
        (DataWinParserOptions) {
            .parseGen8 = true,
            .parseOptn = true,
            .parseLang = true,
            .parseExtn = true,
            .parseSond = true,
            .parseAgrp = true,
            .parseSprt = true,
            .parseBgnd = true,
            .parsePath = true,
            .parseScpt = true,
            .parseGlob = true,
            .parseShdr = true,
            .parseFont = true,
            .parseTmln = true,
            .parseObjt = true,
            .parseRoom = true,
            .parseTpag = true,
            .parseCode = true,
            .parseVari = true,
            .parseFunc = true,
            .parseStrg = true,
            .parseTxtr = true,
            .parseAudo = false,
            .skipLoadingPreciseMasksForNonPreciseSprites = true,
            .progressCallback = WiiDataWinProgressCallback,
            .progressCallbackUserData = &loadingState,
        }
    );

    free(dataWinPath);

    printf("\x1b[4;0H");
    if (dataWin == NULL)
        printf("Failed to load data.win.");
    else
        printf("Successfully loaded data.win!");

    fileSystem = (FileSystem*) NoopFileSystem_create();
    audioSystem = (AudioSystem*) NoopAudioSystem_create();

    vm = VM_create(dataWin);
    renderer = WiiRenderer_create();
    renderer->vtable->init(renderer, dataWin);
    runner = Runner_create(dataWin, vm, renderer, fileSystem, audioSystem);
    runner->osType = OS_WII;

    Runner_initFirstRoom(runner);

    WiiInputState inputState;
    memset(&inputState, 0, sizeof(inputState));

    while (SYS_MainLoop()/*  && !runner->shouldExit */) {
        WPAD_ScanPads();
        WPADData* data = WPAD_Data(0);
        u32 pressed = WPAD_ButtonsDown(0);
        u32 held = WPAD_ButtonsHeld(0);

        //if ( pressed & WPAD_BUTTON_HOME ) break;
        bool desiredKeys[GML_KEY_COUNT];
        memset(desiredKeys, 0, sizeof(desiredKeys));

        accumulateWiimoteButtonsToDesiredKeys(desiredKeys, runner->keyboard, held);
        syncDesiredKeysToKeyboard(&inputState, runner->keyboard, desiredKeys);
        Runner_step(runner);

        Gen8* gen8 = &dataWin->gen8;
        int32_t nativeGameW = (int32_t) gen8->defaultWindowWidth;
        int32_t nativeGameH = (int32_t) gen8->defaultWindowHeight;
        int32_t gameW = clampRenderDimension(nativeGameW, 640, nativeGameW > 0 ? nativeGameW : 640);
        int32_t gameH = clampRenderDimension(nativeGameH, 480, nativeGameH > 0 ? nativeGameH : 480);
        float portScaleX = nativeGameW > 0 ? (float) gameW / (float) nativeGameW : 1.0f;
        float portScaleY = nativeGameH > 0 ? (float) gameH / (float) nativeGameH : 1.0f;

        renderer->vtable->beginFrame(renderer, gameW, gameH, gameW, gameH);

        Room* activeRoom = runner->currentRoom;
        bool viewsEnabled = (activeRoom->flags & 1) != 0;
        bool anyViewRendered = false;

        if (viewsEnabled) {
            repeat(8, vi) {
                if (!activeRoom->views[vi].enabled) continue;
                runner->viewCurrent = vi;
                renderer->vtable->beginView(
                    renderer,
                    activeRoom->views[vi].viewX,
                    activeRoom->views[vi].viewY,
                    activeRoom->views[vi].viewWidth,
                    activeRoom->views[vi].viewHeight,
                    (int32_t) lroundf((float) activeRoom->views[vi].portX * portScaleX),
                    (int32_t) lroundf((float) activeRoom->views[vi].portY * portScaleY),
                    (int32_t) lroundf((float) activeRoom->views[vi].portWidth * portScaleX),
                    (int32_t) lroundf((float) activeRoom->views[vi].portHeight * portScaleY),
                    runner->views[vi].viewAngle
                );
                Runner_draw(runner);
                renderer->vtable->endView(renderer);
                anyViewRendered = true;
            }
        }

        if (!anyViewRendered) {
            runner->viewCurrent = 0;
            renderer->vtable->beginView(renderer, 0, 0, gameW, gameH, 0, 0, gameW, gameH, 0.0f);
            Runner_draw(runner);
            renderer->vtable->endView(renderer);
        }

        runner->viewCurrent = 0;
        renderer->vtable->endFrame(renderer);
    }

    WPAD_Shutdown();

    return 0;
}