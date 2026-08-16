#include "wii_file_system.h"
#include "../utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

__attribute__((weak)) void WiiFileSystem_platformBootLog(const char* message) {
    (void)message;
}

static void WiiFileSystem_bootLog(const char* message) {
    WiiFileSystem_platformBootLog(message);
}

static char* WiiFileSystem_buildFullPath(WiiFileSystem* fs, const char* relativePath) {
    if (relativePath == NULL) return NULL;

    size_t baseLen = strlen(fs->basePath);
    size_t relLen  = strlen(relativePath);

    char* fullPath = safeMalloc(baseLen + relLen + 1);
    memcpy(fullPath, fs->basePath, baseLen);
    memcpy(fullPath + baseLen, relativePath, relLen);
    fullPath[baseLen + relLen] = '\0';

    return fullPath;
}

static char* WiiFileSystem_resolvePath(FileSystem* fs, const char* relativePath) {
    return WiiFileSystem_buildFullPath((WiiFileSystem*)fs, relativePath);
}

static bool WiiFileSystem_fileExists(FileSystem* fs, const char* relativePath) {
    char* fullPath = WiiFileSystem_buildFullPath((WiiFileSystem*)fs, relativePath);

    struct stat st;
    bool exists = stat(fullPath, &st) == 0;

    free(fullPath);
    return exists;
}

static char* WiiFileSystem_readFileText(FileSystem* fs, const char* relativePath) {
    char* fullPath = WiiFileSystem_buildFullPath((WiiFileSystem*)fs, relativePath);

    FILE* file = fopen(fullPath, "rb");
    free(fullPath);

    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) {
        fclose(file);
        return NULL;
    }

    char* text = safeMalloc((size_t)size + 1);
    size_t bytesRead = fread(text, 1, (size_t)size, file);

    text[bytesRead] = '\0';
    fclose(file);

    return text;
}

static bool WiiFileSystem_writeFileText(FileSystem* fs, const char* relativePath, const char* contents) {
    char* fullPath = WiiFileSystem_buildFullPath((WiiFileSystem*)fs, relativePath);

    FILE* file = fopen(fullPath, "wb");
    free(fullPath);

    if (!file) return false;

    size_t length = strlen(contents);
    size_t written = fwrite(contents, 1, length, file);

    bool ok = (written == length);

    if (ok) {
        ok = fflush(file) == 0;
    }
    
    bool closeOk = fclose(file) == 0;

    return ok && closeOk;
}

static bool WiiFileSystem_deleteFile(FileSystem* fs, const char* relativePath) {
    char* fullPath = WiiFileSystem_buildFullPath((WiiFileSystem*)fs, relativePath);

    int result = remove(fullPath);

    free(fullPath);

    return result == 0;
}

static FileSystemVtable WiiFileSystemVtable = {
    .resolvePath    = WiiFileSystem_resolvePath,
    .fileExists     = WiiFileSystem_fileExists,
    .readFileText   = WiiFileSystem_readFileText,
    .writeFileText  = WiiFileSystem_writeFileText,
    .deleteFile     = WiiFileSystem_deleteFile,
};

WiiFileSystem* WiiFileSystem_create(const char* dataWinPath) {
    WiiFileSystem_bootLog("wii_fs: create begin");

    WiiFileSystem* fs = safeCalloc(1, sizeof(WiiFileSystem));
    fs->base.vtable = &WiiFileSystemVtable;

    const char* lastSlash = strrchr(dataWinPath, '/');

    if (lastSlash) {
        size_t dirLen = (size_t)(lastSlash - dataWinPath + 1);

        fs->basePath = safeMalloc(dirLen + 1);
        memcpy(fs->basePath, dataWinPath, dirLen);
        fs->basePath[dirLen] = '\0';
    } else {
        fs->basePath = safeStrdup("sd:/");
    }

    WiiFileSystem_bootLog("wii_fs: create end");

    return fs;
}

void WiiFileSystem_destroy(WiiFileSystem* fs) {
    if (!fs) return;

    free(fs->basePath);
    free(fs);
}