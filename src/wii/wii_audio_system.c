/* #include "wii_audio_system.h"

#include "../data_win.h"
#include "../utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <aesndlib.h>
#include <ogc/lwp.h>
#include <ogc/message.h>

#include "../../vendor/stb/vorbis/stb_vorbis.c"

static void WiiAudio_resetInstance(WiiSoundInstance* inst);
static WiiSoundInstance* WiiAudio_findFreeSlot(WiiAudioSystem* wii);
static WiiSoundInstance* WiiAudio_findInstanceById(WiiAudioSystem* wii, int32_t instanceId);

static DataWin* WiiAudio_mainDataWin(WiiAudioSystem* wii)
{
    return arrlen(wii->base.audioGroups)
        ? wii->base.audioGroups[0]
        : NULL;
}

static void WiiAudio_voiceCallback(AESNDPB* pb, u32 state)
{
    WiiSoundInstance* inst = AESND_GetVoiceUserData(pb);

    if (inst == NULL)
        return;

    if (state != VOICE_STATE_STREAM)
        return;

    void* buffer;

    if (MQ_Receive(inst->messageQueue, &buffer, MQ_MSG_NOBLOCK))
    {
        AESND_SetVoiceBuffer(
            pb,
            buffer,
            inst->streamBufferSize
        );
    }
}

static void WiiAudio_resetInstance(WiiSoundInstance* inst)
{
    if (inst->voice)
    {
        AESND_SetVoiceStop(inst->voice, true);
        AESND_FreeVoice(inst->voice);
    }

    if (inst->vorbisStream)
        stb_vorbis_close(inst->vorbisStream);

    if (inst->threadRunning)
    {
        MQ_Close(inst->messageQueue);
        LWP_JoinThread(inst->thread, NULL);
    }

    free(inst->decodeBuffer);

    free(inst->streamBuffers[0]);
    free(inst->streamBuffers[1]);

    free(inst->threadStack);

    memset(inst, 0, sizeof(*inst));
}

static WiiSoundInstance* WiiAudio_findInstanceById(
    WiiAudioSystem* wii,
    int32_t id)
{
    int slot = id - WII_SOUND_INSTANCE_ID_BASE;

    if (slot < 0)
        return NULL;

    if (slot >= MAX_WII_SOUND_INSTANCES)
        return NULL;

    WiiSoundInstance* inst = &wii->instances[slot];

    if (!inst->active)
        return NULL;

    if (inst->instanceId != id)
        return NULL;

    return inst;
}

static WiiSoundInstance* WiiAudio_findFreeSlot(
    WiiAudioSystem* wii)
{
    repeat(MAX_WII_SOUND_INSTANCES, i)
    {
        if (!wii->instances[i].active)
            return &wii->instances[i];
    }

    WiiSoundInstance* best = NULL;

    repeat(MAX_WII_SOUND_INSTANCES, i)
    {
        WiiSoundInstance* inst = &wii->instances[i];

        if (inst->loop)
            continue;

        if (best == NULL)
            best = inst;
        else if (inst->priority < best->priority)
            best = inst;
    }

    if (best)
        WiiAudio_resetInstance(best);

    return best;
}

static void WiiAudioSystem_init(
    AudioSystem* audio,
    DataWin* dataWin,
    FileSystem* fileSystem)
{
    WiiAudioSystem* wii = (WiiAudioSystem*)audio;

    arrput(wii->base.audioGroups, dataWin);

    wii->fileSystem = fileSystem;

    wii->masterGain = 1.0f;

    wii->decodedSounds =
        safeCalloc(dataWin->sond.count,
                   sizeof(WiiDecodedSound));

    wii->loadedGroups =
        safeCalloc(
            dataWin->agrp.count
                ? dataWin->agrp.count
                : 1,
            sizeof(bool));

    AESND_Init();

    wii->initialized = true;
}

static void WiiAudioSystem_destroy(AudioSystem* audio)
{
    WiiAudioSystem* wii = (WiiAudioSystem*)audio;

    repeat(MAX_WII_SOUND_INSTANCES, i)
        WiiAudio_resetInstance(&wii->instances[i]);

    if (wii->decodedSounds)
    {
        DataWin* dw = WiiAudio_mainDataWin(wii);

        if (dw)
        {
            repeat(dw->sond.count, i)
                free(wii->decodedSounds[i].samples);
        }

        free(wii->decodedSounds);
    }

    free(wii->loadedGroups);

    arrfree(wii->base.audioGroups);

    AESND_Reset();

    free(wii);
}

WiiAudioSystem* WiiAudioSystem_create(void)
{
    WiiAudioSystem* audio =
        safeCalloc(1, sizeof(WiiAudioSystem));

    audio->masterGain = 1.0f;

    audio->base.vtable = &WiiAudioSystemVtable;

    return audio;
}

 */