/* #pragma once

#include "../audio_system.h"

#include <gccore.h>
#include <aesndlib.h>
#include <ogc/lwp.h>
#include <ogc/message.h>

typedef struct stb_vorbis stb_vorbis;

typedef struct {
    bool loaded;

    int16_t* samples;

    uint32_t sampleCount;
    int32_t channels;
    int32_t sampleRate;
} WiiDecodedSound;

typedef struct {
    bool active;
    bool paused;
    bool loop;

    int32_t soundIndex;
    int32_t instanceId;
    int32_t priority;

    float gain;
    float targetGain;
    float startGain;

    float fadeTimeRemaining;
    float fadeTotalTime;

    float pitch;

    float sondVolume;
    float sondPitch;

    double position;

    WiiDecodedSound* decoded;

    AESNDPB* voice;

    //
    // Streaming
    //

    bool streaming;
    bool streamEof;

    stb_vorbis* vorbisStream;

    int32_t streamChannels;
    int32_t streamRate;

    int16_t* decodeBuffer;

    uint32_t decodeFrames;

    void* streamBuffers[2];
    uint32_t streamBufferSize;

    mqbox_t messageQueue;

    lwp_t thread;
    void* threadStack;
    uint32_t currentBuffer;

    bool threadRunning;

} WiiSoundInstance;

#define MAX_WII_SOUND_INSTANCES 64
#define WII_SOUND_INSTANCE_ID_BASE 100000

typedef struct {

    AudioSystem base;

    FileSystem* fileSystem;

    bool initialized;

    float masterGain;

    int32_t nextInstanceCounter;

    WiiDecodedSound* decodedSounds;

    bool* loadedGroups;

    WiiSoundInstance instances[MAX_WII_SOUND_INSTANCES];

} WiiAudioSystem;

WiiAudioSystem* WiiAudioSystem_create(void); */