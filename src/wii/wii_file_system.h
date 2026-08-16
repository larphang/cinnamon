#pragma once

#include "../file_system.h"

typedef struct {
    FileSystem base;
    char* basePath;
} WiiFileSystem;

WiiFileSystem* WiiFileSystem_create(const char* dataWinPath);
void WiiFileSystem_destroy(WiiFileSystem* fs);
