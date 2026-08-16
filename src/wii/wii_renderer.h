#include "../renderer.h"

#include <grrlib.h>

typedef struct {
    GRRLIB_texImg* tex;

    uint8_t* blob;
    uint32_t blobSize;

    int width;
    int height;

    int offsetX;
    int offsetY;

    int parentId;

    bool dumped;
} WiiTexturePage;

typedef struct WiiQuad {
    float x0, y0;
    float x1, y1;
    float x2, y2;
    float x3, y3;

    float u0, v0;
    float u1, v1;

    uint32_t color;
    int texturePageId;
} WiiQuad;

#define MAX_QUADS 2048

typedef struct {
    Renderer base;

    int32_t frameWidth;
    int32_t frameHeight;

    int32_t viewX;
    int32_t viewY;
    int32_t viewW;
    int32_t viewH;

    int32_t portX;
    int32_t portY;
    int32_t portW;
    int32_t portH;

    float viewScaleX;
    float viewScaleY;

    uint32_t commandCount;
    uint32_t batchVertexCount;
    uint32_t queuedQuadCount;

    WiiQuad quadBuffer[MAX_QUADS];
    int quadCount;

    WiiTexturePage* pages;
    int pageCount;
    
} WiiRenderer;

Renderer* WiiRenderer_create(void);
