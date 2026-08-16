#include "wii_renderer.h"

#include <ogc/video.h>
#include <malloc.h>
#include <string.h>
#include <math.h>

#include <stb/image/stb_image.h>
#include <stb/image/stb_image_write.h>

#include "../text_utils.h"
#include "../utils.h"

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

static GRRLIB_texImg *framebuffer = NULL;

#include <stdio.h>
#include <stdarg.h>

static FILE* g_logFile = NULL;
static int g_logInitialized = 0;

static void Log_ensureOpen(void)
{
    if (g_logInitialized)
        return;

    g_logFile = fopen("sd:/debug.log", "a");

    if (g_logFile)
    {
        fprintf(g_logFile, "---- Log started ----\n");
        fflush(g_logFile);
    }
    else
    {
        printf("Log failed to open\n");
    }

    g_logInitialized = 1;
}

void Log_write(const char* fmt, ...)
{
    Log_ensureOpen();

    if (!g_logFile)
        return;

    va_list args;
    va_start(args, fmt);

    vfprintf(g_logFile, fmt, args);
    fprintf(g_logFile, "\n");

    va_end(args);

    fflush(g_logFile);
}

void Log_close(void)
{
    if (!g_logFile)
        return;

    fprintf(g_logFile, "---- Log closed ----\n");
    fclose(g_logFile);
    g_logFile = NULL;
}

static inline int align4(int v) {
    return (v + 3) & ~3;
}

static void WiiRenderer_destroy(Renderer* base) {
    WiiRenderer* renderer = (WiiRenderer*) base;

    if (framebuffer) {
        GRRLIB_FreeTexture(framebuffer);
        framebuffer = NULL;
    }

    GRRLIB_Exit();
}

static void WiiRenderer_BeginFrame(Renderer* base, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH) {
    WiiRenderer* renderer = (WiiRenderer*) base;

    (void) windowW;
    (void) windowH;

    renderer->frameWidth = gameW;
    renderer->frameHeight = gameH;

    renderer->viewX = 0;
    renderer->viewY = 0;
    renderer->viewW = gameW;
    renderer->viewH = gameH;

    renderer->portX = 0;
    renderer->portY = 0;
    renderer->portW = gameW;
    renderer->portH = gameH;

    renderer->viewScaleX = 1.0f;
    renderer->viewScaleY = 1.0f;

    renderer->commandCount = 0;
    renderer->batchVertexCount = 0;
    renderer->queuedQuadCount = 0;

    GRRLIB_FillScreen(0x000000FF);
}

static void WiiRenderer_EndFrame(MAYBE_UNUSED Renderer* base) {
    GRRLIB_Render();
}

static void WiiRenderer_beginView(Renderer* base, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, float viewAngle) {
    (void) viewAngle;

    WiiRenderer* renderer = (WiiRenderer*) base;
    renderer->viewX = viewX;
    renderer->viewY = viewY;
    renderer->viewW = viewW != 0 ? viewW : 1;
    renderer->viewH = viewH != 0 ? viewH : 1;
    renderer->portX = portX;
    renderer->portY = portY;
    renderer->portW = portW;
    renderer->portH = portH;
    renderer->viewScaleX = (float) portW / (float) renderer->viewW;
    renderer->viewScaleY = (float) portH / (float) renderer->viewH;
}

static void WiiRenderer_endView(Renderer* base) {
    (void) base;
}

static void WiiRenderer_loadTexturePages(WiiRenderer* renderer, DataWin* dataWin)
{
    printf("loadTexturePages CALLED\n");

    int totalPages = 0;

    for (int i = 0; i < dataWin->txtr.count; i++) {
        Texture* tex = &dataWin->txtr.textures[i];

        int w, h, comp;

        if (!stbi_info_from_memory(tex->blobData, tex->blobSize, &w, &h, &comp)) {
            continue;
        }

        int pagesX = (w + 1023) / 1024;
        int pagesY = (h + 1023) / 1024;

        totalPages += pagesX * pagesY;
    }

    renderer->pageCount = totalPages;
    renderer->pages = calloc(totalPages, sizeof(WiiTexturePage));

    if (!renderer->pages)
        return;

    int pageIndex = 0;

    for (int i = 0; i < dataWin->txtr.count; i++) {
        Texture* tex = &dataWin->txtr.textures[i];

        int w, h, comp;

        if (!stbi_info_from_memory(tex->blobData, tex->blobSize, &w, &h, &comp)) {
            continue;
        }

        int pagesX = (w + 1023) / 1024;
        int pagesY = (h + 1023) / 1024;

        for (int py = 0; py < pagesY; py++) {
            for (int px = 0; px < pagesX; px++) {

                WiiTexturePage* page = &renderer->pages[pageIndex++];

                page->blob = tex->blobData;
                page->blobSize = tex->blobSize;

                page->tex = NULL;

                page->width = MIN(1024, w - px * 1024);
                page->height = MIN(1024, h - py * 1024);

                page->offsetX = px * 1024;
                page->offsetY = py * 1024;

                page->parentId = i;

                //page->dumped = false;
            }
        }
    }
}

typedef struct {
    unsigned char* data;
    int size;
} MemBuffer;

static void mem_write_func(void* context, void* data, int size)
{
    MemBuffer* buf = (MemBuffer*)context;

    buf->data = realloc(buf->data, buf->size + size);
    memcpy(buf->data + buf->size, data, size);
    buf->size += size;
}

static void WiiRenderer_ensureTextureLoaded(WiiRenderer* renderer, int i)
{
    if (!renderer || !renderer->pages)
        return;

    if (i < 0 || i >= renderer->pageCount)
        return;

    WiiTexturePage* page = &renderer->pages[i];

    if (page->tex)
        return;

    if (!page->blob || page->blobSize == 0)
        return;

    int imgW, imgH, comp;

    unsigned char* rgba = stbi_load_from_memory(page->blob, page->blobSize, &imgW, &imgH, &comp, 4);

    if (!rgba) {
        return;
    }

    unsigned char* chunk = (unsigned char*)malloc(page->width * page->height * 4);

    if (!chunk) {
        stbi_image_free(rgba);
        return;
    }

    for (int y = 0; y < page->height; y++) {
        int srcY = page->offsetY + y;

        unsigned char* src = rgba + (srcY * imgW + page->offsetX) * 4;
        unsigned char* dst = chunk + (y * page->width) * 4;

        memcpy(dst, src, page->width * 4);
    }

    int pngSize = 0;

    MemBuffer buf = {0};

    stbi_write_png_to_func(mem_write_func, &buf, page->width, page->height, 4, chunk, page->width * 4);

    if (!buf.data) {
        free(chunk);
        stbi_image_free(rgba);
        return;
    }

    GRRLIB_texImg* gtex = GRRLIB_LoadTexture(buf.data);

    free(buf.data);
    free(chunk);
    stbi_image_free(rgba);

    if (!gtex) {
        return;
    }

    page->tex = gtex;
    page->width = gtex->w;
    page->height = gtex->h;

    /* if (!page->dumped) {
        page->dumped = true;

        char path[64];
        snprintf(path, sizeof(path), "sd:/tpag_%03d.png", i);

        FILE* f = fopen(path, "wb");
        if (f) {
            fwrite(page->blob, 1, page->blobSize, f);
            fclose(f);
        }
    } */
}

static void WiiRenderer_drawSprite(Renderer* base, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {
    WiiRenderer* renderer = (WiiRenderer*)base;
    DataWin* dataWin = base->dataWin;

    if (tpagIndex < 0 || tpagIndex >= dataWin->tpag.count)
        return;

    TexturePageItem* tpag = &dataWin->tpag.items[tpagIndex];

    float drawX = x - originX * xscale;
    float drawY = y - originY * yscale;

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    uint8_t a = (uint8_t)(alpha * 255.0f);

    int srcX = tpag->sourceX;
    int srcY = tpag->sourceY;
    int srcW = tpag->sourceWidth;
    int srcH = tpag->sourceHeight;

    for (int i = 0; i < renderer->pageCount; i++) {
        WiiTexturePage* page = &renderer->pages[i];

        if (page->parentId != tpag->texturePageId)
            continue;

        WiiRenderer_ensureTextureLoaded(renderer, i);

        if (!page->tex)
            continue;

        int ix1 = MAX(srcX, page->offsetX);
        int iy1 = MAX(srcY, page->offsetY);

        int ix2 = MIN(srcX + srcW, page->offsetX + page->width);

        int iy2 = MIN(srcY + srcH, page->offsetY + page->height);

        if (ix1 >= ix2 || iy1 >= iy2)
            continue;

        int tileSrcX = ix1 - page->offsetX;
        int tileSrcY = iy1 - page->offsetY;

        int pieceW = ix2 - ix1;
        int pieceH = iy2 - iy1;

        float pieceDrawX = drawX + (ix1 - srcX) * xscale;

        float pieceDrawY = drawY + (iy1 - srcY) * yscale;

        GRRLIB_DrawPart(
            pieceDrawX,
            pieceDrawY,
            tileSrcX,
            tileSrcY,
            pieceW,
            pieceH,
            page->tex,
            angleDeg,
            xscale,
            yscale,
            RGBA(r, g, b, a)
        );
    }
}

static void WiiRenderer_drawSpritePart(Renderer* base,
                                      int32_t tpagIndex,
                                      int32_t srcOffX, int32_t srcOffY,
                                      int32_t srcW, int32_t srcH,
                                      float x, float y,
                                      float xscale, float yscale,
                                      float angleDeg,
                                      float pivotX, float pivotY,
                                      uint32_t color,
                                      float alpha)
{
    WiiRenderer* renderer = (WiiRenderer*)base;

    float drawX = x - pivotX * xscale;
    float drawY = y - pivotY * yscale;

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    uint8_t a = (uint8_t)(alpha * 255.0f);

    u32 gxColor = RGBA(r, g, b, a);

    for (int i = 0; i < renderer->pageCount; i++)
    {
        WiiTexturePage* page = &renderer->pages[i];

        if (page->parentId != tpagIndex)
            continue;

        WiiRenderer_ensureTextureLoaded(renderer, i);

        if (!page->tex)
            continue;

        int ix1 = MAX(srcOffX, page->offsetX);
        int iy1 = MAX(srcOffY, page->offsetY);

        int ix2 = MIN(srcOffX + srcW, page->offsetX + page->width);
        int iy2 = MIN(srcOffY + srcH, page->offsetY + page->height);

        if (ix1 >= ix2 || iy1 >= iy2)
            continue;

        int pieceW = ix2 - ix1;
        int pieceH = iy2 - iy1;

        float pieceDrawX = drawX + (ix1 - srcOffX) * xscale;
        float pieceDrawY = drawY + (iy1 - srcOffY) * yscale;


        guVector quad[4];

        quad[0].x = pieceDrawX;
        quad[0].y = pieceDrawY;
        quad[0].z = 0.0f;

        quad[1].x = pieceDrawX + pieceW * xscale;
        quad[1].y = pieceDrawY;
        quad[1].z = 0.0f;

        quad[2].x = pieceDrawX + pieceW * xscale;
        quad[2].y = pieceDrawY + pieceH * yscale;
        quad[2].z = 0.0f;

        quad[3].x = pieceDrawX;
        quad[3].y = pieceDrawY + pieceH * yscale;
        quad[3].z = 0.0f;

        GRRLIB_DrawImgQuad(
            quad,
            page->tex,
            gxColor
        );
    }
}

// 
// stubs
//

static void WiiRenderer_beginGUI(Renderer* renderer,
                                 int32_t guiW, int32_t guiH,
                                 int32_t portX, int32_t portY,
                                 int32_t portW, int32_t portH)
{
    (void)renderer;
    (void)guiW;
    (void)guiH;
    (void)portX;
    (void)portY;
    (void)portW;
    (void)portH;
}

static void WiiRenderer_endGUI(Renderer* renderer)
{
    (void)renderer;
}

static void WiiRenderer_drawRectangle(Renderer* renderer,
                                      float x1, float y1,
                                      float x2, float y2,
                                      uint32_t color,
                                      float alpha,
                                      bool outline)
{
    (void)renderer;

    float x = x1;
    float y = y1;
    float w = x2 - x1;
    float h = y2 - y1;

    if (w < 0) { w = -w; x = x2; }
    if (h < 0) { h = -h; y = y2; }

    uint8_t a = (uint8_t)(fmaxf(0.0f, fminf(alpha, 1.0f)) * 255.0f);

    uint32_t argb = (color & 0x00FFFFFF) | ((uint32_t)a << 24);

    if (outline) {
        GRRLIB_Line(x,     y,     x + w, y,     argb);
        GRRLIB_Line(x + w, y,     x + w, y + h, argb);
        GRRLIB_Line(x + w, y + h, x,     y + h, argb);
        GRRLIB_Line(x,     y + h, x,     y,     argb);
    } else {
        GRRLIB_Rectangle(x, y, w, h, argb, true);
    }
}

static void WiiRenderer_drawLine(Renderer* renderer,
                                 float x1, float y1,
                                 float x2, float y2,
                                 float width,
                                 uint32_t color,
                                 float alpha)
{
    (void)renderer;
    (void)x1; (void)y1;
    (void)x2; (void)y2;
    (void)width;
    (void)color;
    (void)alpha;
}

static void WiiRenderer_drawTriangle(Renderer* renderer,
                                     float x1, float y1,
                                     float x2, float y2,
                                     float x3, float y3,
                                     bool outline)
{
    (void)renderer;


    guVector v[3];

    v[0].x = x1; v[0].y = y1; v[0].z = 0.0f;
    v[1].x = x2; v[1].y = y2; v[1].z = 0.0f;
    v[2].x = x3; v[2].y = y3; v[2].z = 0.0f;

    u32 col[3] = { 0xFF, 0xFF, 0xFF };

    GRRLIB_NGoneFilled(v, col, 3);
}

static void WiiRenderer_drawLineColor(Renderer* renderer,
                                      float x1, float y1,
                                      float x2, float y2,
                                      float width,
                                      uint32_t color1,
                                      uint32_t color2,
                                      float alpha)
{
    (void)renderer;

    uint8_t a = (uint8_t)(fmaxf(0.0f, fminf(alpha, 1.0f)) * 255.0f);

    uint32_t c1 = (color1 & 0x00FFFFFF) | ((uint32_t)a << 24);
    uint32_t c2 = (color2 & 0x00FFFFFF) | ((uint32_t)a << 24);

    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);

    if (len < 0.0001f) {
        GRRLIB_Rectangle(x1, y1, width, width, c1, true);
        return;
    }

    float steps = len / 4.0f;
    if (steps < 1) steps = 1;

    float prevX = x1;
    float prevY = y1;

    for (int i = 1; i <= (int)steps; i++) {
        float t = (float)i / steps;

        float nx = x1 + dx * t;
        float ny = y1 + dy * t;

        uint8_t r1 = (c1 >> 16) & 0xFF;
        uint8_t g1 = (c1 >> 8) & 0xFF;
        uint8_t b1 = c1 & 0xFF;

        uint8_t r2 = (c2 >> 16) & 0xFF;
        uint8_t g2 = (c2 >> 8) & 0xFF;
        uint8_t b2 = c2 & 0xFF;

        uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
        uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
        uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);

        uint32_t col = (a << 24) | (r << 16) | (g << 8) | b;

        GRRLIB_Line(prevX, prevY, nx, ny, col);

        prevX = nx;
        prevY = ny;
    }
}

static void WiiRenderer_drawText(Renderer* base,
                                 const char* text,
                                 float x, float y,
                                 float xscale, float yscale,
                                 float angleDeg)
{
    WiiRenderer* renderer = (WiiRenderer*)base;
    DataWin* dataWin = base->dataWin;

    int32_t fontIndex = base->drawFont;
    if (fontIndex < 0 || (uint32_t)fontIndex >= dataWin->font.count)
        return;

    Font* font = &dataWin->font.fonts[fontIndex];
    int32_t fontTpagIndex = font->tpagIndex;
    if (fontTpagIndex < 0 || (uint32_t)fontTpagIndex >= dataWin->tpag.count)
        return;

    TexturePageItem* tpag = &dataWin->tpag.items[fontTpagIndex];

    int32_t texPageId = tpag->texturePageId;
    if (texPageId < 0 || (uint32_t)texPageId >= renderer->pageCount)
        return;

    WiiTexturePage* page = &renderer->pages[texPageId];
    WiiRenderer_ensureTextureLoaded(renderer, texPageId);

    if (!page->tex || !page->tex->data)
        return;

    float cursorX = x;
    float cursorY = y;

    const char* p = text;

    while (*p)
    {
        uint8_t ch = (uint8_t)*p++;

        if (ch == '\n')
        {
            cursorX = x;
            cursorY += font->emSize * yscale;
            continue;
        }

        FontGlyph* glyph = TextUtils_findGlyph(font, ch);
        if (!glyph)
            continue;

        if (glyph->sourceWidth == 0 || glyph->sourceHeight == 0)
        {
            cursorX += glyph->shift * xscale;
            continue;
        }

        float drawX = cursorX + glyph->offset * xscale;
        float drawY = cursorY;

        float srcX = (float)glyph->sourceX;
        float srcY = (float)glyph->sourceY;
        float srcW = (float)glyph->sourceWidth;
        float srcH = (float)glyph->sourceHeight;

        GRRLIB_DrawPart(drawX, drawY, srcX, srcY, srcW, srcH, page->tex, angleDeg, xscale, yscale,
            //base->drawColor
            RGBA(255, 255, 255, 255)
        );

        cursorX += glyph->shift * xscale;
    }
}

static void WiiRenderer_flush(Renderer* renderer)
{
    (void)renderer;
}

static void WiiRenderer_clearScreen(Renderer* renderer,
                                    uint32_t color,
                                    float alpha)
{
    (void)renderer;
    (void)alpha;

    GRRLIB_FillScreen((color << 8) | 0xFF);
}

static int32_t WiiRenderer_createSpriteFromSurface(Renderer* renderer,
                                                   int32_t surfaceID,
                                                   int32_t x,
                                                   int32_t y,
                                                   int32_t w,
                                                   int32_t h,
                                                   bool removeback,
                                                   bool smooth,
                                                   int32_t xorig,
                                                   int32_t yorig)
{
    (void)renderer;
    (void)surfaceID;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)removeback;
    (void)smooth;
    (void)xorig;
    (void)yorig;
    return -1;
}

static void WiiRenderer_deleteSprite(Renderer* renderer, int32_t spriteIndex)
{
    (void)renderer;
    (void)spriteIndex;
}

static void WiiRenderer_gpuSetBlendMode(Renderer* renderer, int32_t mode)
{
    (void)renderer;
    (void)mode;
}

static void WiiRenderer_gpuSetBlendModeExt(Renderer* renderer,
                                           int32_t sfactor,
                                           int32_t dfactor)
{
    (void)renderer;
    (void)sfactor;
    (void)dfactor;
}

static void WiiRenderer_gpuSetBlendEnable(Renderer* renderer, bool enable)
{
    (void)renderer;
    (void)enable;
}

static void WiiRenderer_gpuSetAlphaTestEnable(Renderer* renderer, bool enable)
{
    (void)renderer;
    (void)enable;
}

static void WiiRenderer_gpuSetAlphaTestRef(Renderer* renderer, uint8_t ref)
{
    (void)renderer;
    (void)ref;
}

static void WiiRenderer_gpuSetColorWriteEnable(Renderer* renderer,
                                               bool red,
                                               bool green,
                                               bool blue,
                                               bool alpha)
{
    (void)renderer;
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
}

static void WiiRenderer_gpuSetFog(Renderer* renderer,
                                  bool enable,
                                  uint32_t color)
{
    (void)renderer;
    (void)enable;
    (void)color;
}

static void WiiRenderer_prewarmRoom(Renderer* renderer, Runner* runner)
{
    (void)renderer;
    (void)runner;
}

static int32_t WiiRenderer_createSurface(Renderer* renderer,
                                         int32_t width,
                                         int32_t height)
{
    (void)renderer;
    (void)width;
    (void)height;
    return -1;
}

static bool WiiRenderer_surfaceExists(Renderer* renderer, int32_t surfaceID)
{
    (void)renderer;
    (void)surfaceID;
    return false;
}

static bool WiiRenderer_setSurfaceTarget(Renderer* renderer, int32_t surfaceID)
{
    (void)renderer;
    (void)surfaceID;
    return false;
}

static bool WiiRenderer_resetSurfaceTarget(Renderer* renderer)
{
    (void)renderer;
    return false;
}

static float WiiRenderer_getSurfaceWidth(Renderer* renderer, int32_t surfaceID)
{
    (void)renderer;
    (void)surfaceID;
    return 0.0f;
}

static float WiiRenderer_getSurfaceHeight(Renderer* renderer, int32_t surfaceID)
{
    (void)renderer;
    (void)surfaceID;
    return 0.0f;
}

static void WiiRenderer_drawSurface(Renderer* renderer,
                                    int32_t surfaceID,
                                    float x, float y,
                                    float xscale, float yscale,
                                    float angleDeg,
                                    uint32_t color,
                                    float alpha)
{
    (void)renderer;
    (void)surfaceID;
    (void)x; (void)y;
    (void)xscale; (void)yscale;
    (void)angleDeg;
    (void)color;
    (void)alpha;
}

static void WiiRenderer_drawSurfacePart(Renderer* renderer,
                                        int32_t surfaceID,
                                        int32_t x,
                                        int32_t y,
                                        int32_t left,
                                        int32_t top,
                                        int32_t width,
                                        int32_t height,
                                        float xscale,
                                        float yscale,
                                        uint32_t color,
                                        float alpha)
{
    (void)renderer;
    (void)surfaceID;
    (void)x;
    (void)y;
    (void)left;
    (void)top;
    (void)width;
    (void)height;
    (void)xscale;
    (void)yscale;
    (void)color;
    (void)alpha;
}

static void WiiRenderer_drawSurfaceStretched(Renderer* renderer,
                                             int32_t surfaceID,
                                             float x,
                                             float y,
                                             float width,
                                             float height)
{
    (void)renderer;
    (void)surfaceID;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void WiiRenderer_surfaceResize(Renderer* renderer,
                                      int32_t surfaceID,
                                      int32_t width,
                                      int32_t height)
{
    (void)renderer;
    (void)surfaceID;
    (void)width;
    (void)height;
}

static void WiiRenderer_surfaceFree(Renderer* renderer, int32_t surfaceID)
{
    (void)renderer;
    (void)surfaceID;
}

static void WiiRenderer_surfaceCopy(Renderer* renderer,
                                    int32_t DestSurfaceID,
                                    int32_t DestX,
                                    int32_t DestY,
                                    int32_t SrcSurfaceID,
                                    int32_t SrcX,
                                    int32_t SrcY,
                                    int32_t SrcW,
                                    int32_t SrcH,
                                    bool part)
{
    (void)renderer;
    (void)DestSurfaceID;
    (void)DestX;
    (void)DestY;
    (void)SrcSurfaceID;
    (void)SrcX;
    (void)SrcY;
    (void)SrcW;
    (void)SrcH;
    (void)part;
}

static void WiiRenderer_drawTiledPart(Renderer* renderer,
                                      int32_t tpagIndex,
                                      int32_t srcX,
                                      int32_t srcY,
                                      int32_t srcW,
                                      int32_t srcH,
                                      float dstX,
                                      float dstY,
                                      float dstW,
                                      float dstH,
                                      uint32_t color,
                                      float alpha)
{
    WiiRenderer* self = (WiiRenderer*)renderer;

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    uint8_t a = (uint8_t)(fmaxf(0.0f, fminf(alpha, 1.0f)) * 255.0f);

    uint32_t col = RGBA(r, g, b, a);

    for (int i = 0; i < self->pageCount; i++) {
        WiiTexturePage* page = &self->pages[i];

        if (page->parentId != tpagIndex)
            continue;

        WiiRenderer_ensureTextureLoaded(self, i);

        if (!page->tex)
            continue;

        int ix1 = MAX(srcX, page->offsetX);
        int iy1 = MAX(srcY, page->offsetY);

        int ix2 = MIN(srcX + srcW, page->offsetX + page->width);
        int iy2 = MIN(srcY + srcH, page->offsetY + page->height);

        if (ix1 >= ix2 || iy1 >= iy2)
            continue;

        int tileSrcX = ix1 - page->offsetX;
        int tileSrcY = iy1 - page->offsetY;

        int pieceW = ix2 - ix1;
        int pieceH = iy2 - iy1;

        float pieceDstX = dstX + (float)(ix1 - srcX);
        float pieceDstY = dstY + (float)(iy1 - srcY);

        float scaleX = dstW / (float)srcW;
        float scaleY = dstH / (float)srcH;

        pieceDstX = dstX + (float)(ix1 - srcX) * scaleX;
        pieceDstY = dstY + (float)(iy1 - srcY) * scaleY;

        float pieceDstW = (float)pieceW * scaleX;
        float pieceDstH = (float)pieceH * scaleY;

        GRRLIB_DrawPart(pieceDstX, pieceDstY,tileSrcX, tileSrcY, pieceW, pieceH, page->tex, 0.0f,scaleX, scaleY, col);
    }
}

static void WiiRenderer_init(Renderer* base, DataWin* dataWin) {
    WiiRenderer* renderer = (WiiRenderer*) base;
    base->dataWin = dataWin;

    GRRLIB_Init();

    renderer->frameWidth = rmode->fbWidth;
    renderer->frameHeight = rmode->efbHeight;

    renderer->viewX = 0;
    renderer->viewY = 0;
    renderer->viewW = renderer->frameWidth;
    renderer->viewH = renderer->frameHeight;

    renderer->portX = 0;
    renderer->portY = 0;
    renderer->portW = renderer->frameWidth;
    renderer->portH = renderer->frameHeight;

    renderer->viewScaleX = 1.0f;
    renderer->viewScaleY = 1.0f;

    renderer->commandCount = 0;
    renderer->batchVertexCount = 0;
    renderer->queuedQuadCount = 0;

    WiiRenderer_loadTexturePages(renderer, dataWin);
}

static RendererVtable WiiRendererVtable = {
    .init = WiiRenderer_init,
    .destroy = WiiRenderer_destroy,

    .beginFrame = WiiRenderer_BeginFrame,
    .endFrame = WiiRenderer_EndFrame,

    .beginView = WiiRenderer_beginView,
    .endView = WiiRenderer_endView,

    .beginGUI = WiiRenderer_beginGUI,
    .endGUI = WiiRenderer_endGUI,

    .drawSprite = WiiRenderer_drawSprite,
    .drawSpritePart = WiiRenderer_drawSpritePart,

    .drawRectangle = WiiRenderer_drawRectangle,
    .drawLine = WiiRenderer_drawLine,
    .drawTriangle = WiiRenderer_drawTriangle,
    .drawLineColor = WiiRenderer_drawLineColor,

    .drawText = WiiRenderer_drawText,

    .flush = WiiRenderer_flush,
    .clearScreen = WiiRenderer_clearScreen,

    .createSpriteFromSurface = WiiRenderer_createSpriteFromSurface,
    .deleteSprite = WiiRenderer_deleteSprite,

    .gpuSetBlendMode = WiiRenderer_gpuSetBlendMode,
    .gpuSetBlendModeExt = WiiRenderer_gpuSetBlendModeExt,
    .gpuSetBlendEnable = WiiRenderer_gpuSetBlendEnable,
    .gpuSetAlphaTestEnable = WiiRenderer_gpuSetAlphaTestEnable,
    .gpuSetAlphaTestRef = WiiRenderer_gpuSetAlphaTestRef,
    .gpuSetColorWriteEnable = WiiRenderer_gpuSetColorWriteEnable,
    .gpuSetFog = WiiRenderer_gpuSetFog,

    .drawTile = NULL,
    .prewarmRoom = WiiRenderer_prewarmRoom,
    .drawTiled = NULL,

    .createSurface = WiiRenderer_createSurface,
    .surfaceExists = WiiRenderer_surfaceExists,
    .setSurfaceTarget = WiiRenderer_setSurfaceTarget,
    .resetSurfaceTarget = WiiRenderer_resetSurfaceTarget,

    .getSurfaceWidth = WiiRenderer_getSurfaceWidth,
    .getSurfaceHeight = WiiRenderer_getSurfaceHeight,

    .drawSurface = WiiRenderer_drawSurface,
    .drawSurfacePart = WiiRenderer_drawSurfacePart,
    .drawSurfaceStretched = WiiRenderer_drawSurfaceStretched,

    .surfaceResize = WiiRenderer_surfaceResize,
    .surfaceFree = WiiRenderer_surfaceFree,
    .surfaceCopy = WiiRenderer_surfaceCopy,

    .drawTiledPart = WiiRenderer_drawTiledPart
};

Renderer* WiiRenderer_create(void) {
    WiiRenderer* renderer = safeCalloc(1, sizeof(WiiRenderer));

    renderer->base.vtable = &WiiRendererVtable;
    renderer->base.drawColor = 0xFFFFFF;
    renderer->base.drawAlpha = 1.0f;
    renderer->base.drawFont = -1;

    return (Renderer*) renderer;
}