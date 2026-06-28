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

    //bool dumped;
} WiiTexturePage;

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

    WiiTexturePage* pages;
    int pageCount;
} WiiRenderer;

Renderer* WiiRenderer_create(void);
