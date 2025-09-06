#ifndef XBOX_AUDIO_SYSTEM_H
#define XBOX_AUDIO_SYSTEM_H

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <vector>
#include <cstdint>


class XboxMemory;


class XboxAudioSystem {
public:
    XboxAudioSystem();
    ~XboxAudioSystem();

    bool initialize();
    void shutdown();
    bool isInitialized() const { return initialized; }


    bool playSound(uint32_t soundId, uint32_t volume = 100);
    bool playMusic(uint32_t musicId, bool loop = false);
    bool stopAudio(uint32_t audioId);
    bool setVolume(uint32_t audioId, uint32_t volume);
    bool setMasterVolume(uint32_t volume);


    bool createAudioStream(uint32_t format, uint32_t channels, uint32_t sampleRate);
    bool queueAudioData(uint32_t streamId, const void* data, uint32_t size);
    bool startAudioStream(uint32_t streamId);
    bool stopAudioStream(uint32_t streamId);


    void updateFromXboxMemory(const XboxMemory& memory);
    bool hasActiveAudio() const;

private:

    SLObjectItf engineObject;
    SLEngineItf engineEngine;
    SLObjectItf outputMixObject;


    SLObjectItf audioPlayerObject;
    SLPlayItf audioPlayerPlay;
    SLVolumeItf audioPlayerVolume;


    SLObjectItf audioRecorderObject;
    SLRecordItf audioRecorderRecord;


    struct XboxAudioStream {
        bool active;
        uint32_t id;
        uint32_t format;
        uint32_t channels;
        uint32_t sampleRate;
        uint32_t volume;
        bool playing;
        bool loop;
        std::vector<int16_t> buffer;
        SLObjectItf playerObject;
        SLPlayItf playInterface;
        SLVolumeItf volumeInterface;
    };

    static const uint32_t MAX_XBOX_AUDIO_STREAMS = 16;
    XboxAudioStream audioStreams[MAX_XBOX_AUDIO_STREAMS];


    static const uint32_t XBOX_AUDIO_BASE = 0xFE000000;
    static const uint32_t XBOX_AUDIO_BUFFER_SIZE = 48000 * 2 * 2; 


    bool initialized;
    uint32_t masterVolume;
    uint32_t lastAudioUpdate;


    bool createAudioPlayer();
    bool createAudioRecorder();
    void destroyAudioObjects();
    uint32_t findFreeStreamId() const;
    void processXboxAudioData(XboxMemory& memory);
    bool playTestTone(uint32_t volume);

    static uint32_t frameCounter;
};

#endif 
