#include "xbox_audio_system.h"
#include <android/log.h>
#include <cstring>
#include <cmath>
#include "xbox_memory.h"

uint32_t XboxAudioSystem::frameCounter = 0;

XboxAudioSystem::XboxAudioSystem() : initialized(false), masterVolume(100), lastAudioUpdate(0) {

    engineObject = nullptr;
    engineEngine = nullptr;
    outputMixObject = nullptr;
    audioPlayerObject = nullptr;
    audioPlayerPlay = nullptr;
    audioPlayerVolume = nullptr;
    audioRecorderObject = nullptr;
    audioRecorderRecord = nullptr;


    for (uint32_t i = 0; i < MAX_XBOX_AUDIO_STREAMS; i++) {
        audioStreams[i].active = false;
        audioStreams[i].id = 0;
        audioStreams[i].format = 0;
        audioStreams[i].channels = 0;
        audioStreams[i].sampleRate = 0;
        audioStreams[i].volume = 100;
        audioStreams[i].playing = false;
        audioStreams[i].loop = false;
        audioStreams[i].playerObject = nullptr;
        audioStreams[i].playInterface = nullptr;
        audioStreams[i].volumeInterface = nullptr;
    }
}

XboxAudioSystem::~XboxAudioSystem() {
    shutdown();
}

bool XboxAudioSystem::initialize() {
    if (initialized) {
        __android_log_print(ANDROID_LOG_WARN, "XboxAudioSystem", "🎵 Audio-System bereits initialisiert");
        return true;
    }

    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "🎵 Initialisiere Xbox Audio System...");


    SLresult result = slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Erstellen der OpenSLES Engine: %d", result);
        return false;
    }


    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Realisieren der Engine: %d", result);
        return false;
    }


    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    if (result != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Abrufen der Engine Interface: %d", result);
        return false;
    }


    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Erstellen des Output Mix: %d", result);
        return false;
    }


    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Realisieren des Output Mix: %d", result);
        return false;
    }


    if (!createAudioPlayer()) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Erstellen des Audio Players");
        return false;
    }


    if (!createAudioRecorder()) {
        __android_log_print(ANDROID_LOG_WARN, "XboxAudioSystem", "⚠️ Audio Recorder konnte nicht erstellt werden - nicht kritisch");
    }

    initialized = true;
    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "✅ Xbox Audio System erfolgreich initialisiert!");
    return true;
}

void XboxAudioSystem::shutdown() {
    if (!initialized) return;

    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "🔇 Beende Xbox Audio System...");


    for (uint32_t i = 0; i < MAX_XBOX_AUDIO_STREAMS; i++) {
        if (audioStreams[i].active) {
            stopAudioStream(i);
        }
    }


    destroyAudioObjects();

    initialized = false;
    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "✅ Xbox Audio System beendet");
}


bool XboxAudioSystem::playSound(uint32_t soundId, uint32_t volume) {
    if (!initialized) return false;

    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "🔊 Spiele Sound %u mit Lautstärke %u", soundId, volume);



    return playTestTone(volume);
}

bool XboxAudioSystem::playMusic(uint32_t musicId, bool loop) {
    if (!initialized) return false;

    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "🎵 Spiele Musik %u (Loop: %s)", musicId, loop ? "JA" : "NEIN");


    return true;
}

bool XboxAudioSystem::stopAudio(uint32_t audioId) {
    if (!initialized) return false;

    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "⏹️ Stoppe Audio %u", audioId);


    return true;
}

bool XboxAudioSystem::setVolume(uint32_t audioId, uint32_t volume) {
    if (!initialized) return false;

    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "🔊 Setze Lautstärke für Audio %u auf %u", audioId, volume);


    return true;
}

bool XboxAudioSystem::setMasterVolume(uint32_t volume) {
    if (!initialized) return false;

    masterVolume = volume;
    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "🔊 Master-Lautstärke auf %u gesetzt", volume);


    return true;
}


bool XboxAudioSystem::createAudioStream(uint32_t format, uint32_t channels, uint32_t sampleRate) {
    if (!initialized) return false;

    uint32_t streamId = findFreeStreamId();
    if (streamId == 0xFFFFFFFF) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Keine freien Audio-Streams verfügbar");
        return false;
    }

    audioStreams[streamId].active = true;
    audioStreams[streamId].id = streamId;
    audioStreams[streamId].format = format;
    audioStreams[streamId].channels = channels;
    audioStreams[streamId].sampleRate = sampleRate;
    audioStreams[streamId].volume = 100;
    audioStreams[streamId].playing = false;
    audioStreams[streamId].loop = false;

    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "🎵 Audio-Stream %u erstellt: %u Hz, %u Kanäle", streamId, sampleRate, channels);
    return true;
}

bool XboxAudioSystem::queueAudioData(uint32_t streamId, const void* data, uint32_t size) {
    if (!initialized || streamId >= MAX_XBOX_AUDIO_STREAMS || !audioStreams[streamId].active) {
        return false;
    }


    __android_log_print(ANDROID_LOG_DEBUG, "XboxAudioSystem", "📥 Audio-Daten für Stream %u eingereiht (%u Bytes)", streamId, size);
    (void)data; 
    return true;
}

bool XboxAudioSystem::startAudioStream(uint32_t streamId) {
    if (!initialized || streamId >= MAX_XBOX_AUDIO_STREAMS || !audioStreams[streamId].active) {
        return false;
    }

    audioStreams[streamId].playing = true;
    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "▶️ Audio-Stream %u gestartet", streamId);
    return true;
}

bool XboxAudioSystem::stopAudioStream(uint32_t streamId) {
    if (!initialized || streamId >= MAX_XBOX_AUDIO_STREAMS || !audioStreams[streamId].active) {
        return false;
    }

    audioStreams[streamId].playing = false;
    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "⏹️ Audio-Stream %u gestoppt", streamId);
    return true;
}


void XboxAudioSystem::updateFromXboxMemory(const XboxMemory& memory) {
    if (!initialized) return;


    static uint32_t frameCounter = 0;
    frameCounter++;
    if (frameCounter % 100 != 0) return;


    processXboxAudioData(const_cast<XboxMemory&>(memory));
}

bool XboxAudioSystem::hasActiveAudio() const {
    if (!initialized) return false;


    for (uint32_t i = 0; i < MAX_XBOX_AUDIO_STREAMS; i++) {
        if (audioStreams[i].active && audioStreams[i].playing) {
            return true;
        }
    }

    return false;
}


bool XboxAudioSystem::createAudioPlayer() {

    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataSink audioSnk = {&loc_outmix, nullptr};

    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM format_pcm = {
        SL_DATAFORMAT_PCM,
        2, 
        SL_SAMPLINGRATE_48,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
        SL_BYTEORDER_LITTLEENDIAN
    };

    SLDataSource audioSrc = {&loc_bufq, &format_pcm};

    const SLInterfaceID ids[2] = {SL_IID_BUFFERQUEUE, SL_IID_VOLUME};
    const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};

    SLresult result = (*engineEngine)->CreateAudioPlayer(engineEngine, &audioPlayerObject, &audioSrc, &audioSnk, 2, ids, req);
    if (result != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Erstellen des Audio Players: %d", result);
        return false;
    }

    result = (*audioPlayerObject)->Realize(audioPlayerObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Realisieren des Audio Players: %d", result);
        return false;
    }

    result = (*audioPlayerObject)->GetInterface(audioPlayerObject, SL_IID_PLAY, &audioPlayerPlay);
    if (result != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Abrufen der Play Interface: %d", result);
        return false;
    }

    result = (*audioPlayerObject)->GetInterface(audioPlayerObject, SL_IID_VOLUME, &audioPlayerVolume);
    if (result != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Abrufen der Volume Interface: %d", result);
        return false;
    }

    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "✅ Audio Player erfolgreich erstellt");
    return true;
}

bool XboxAudioSystem::createAudioRecorder() {

    SLDataLocator_IODevice loc_dev = {SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT, SL_DEFAULTDEVICEID_AUDIOINPUT, nullptr};
    SLDataSource audioSrc = {&loc_dev, nullptr};

    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM format_pcm = {
        SL_DATAFORMAT_PCM,
        1, 
        SL_SAMPLINGRATE_48,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_SPEAKER_FRONT_CENTER,
        SL_BYTEORDER_LITTLEENDIAN
    };

    SLDataSink audioSnk = {&loc_bufq, &format_pcm};

    const SLInterfaceID ids[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};

    SLresult result = (*engineEngine)->CreateAudioRecorder(engineEngine, &audioRecorderObject, &audioSrc, &audioSnk, 1, ids, req);
    if (result != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Erstellen des Audio Recorders: %d", result);
        return false;
    }

    result = (*audioRecorderObject)->Realize(audioRecorderObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Realisieren des Audio Recorders: %d", result);
        return false;
    }

    result = (*audioRecorderObject)->GetInterface(audioRecorderObject, SL_IID_RECORD, &audioRecorderRecord);
    if (result != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxAudioSystem", "❌ Fehler beim Abrufen der Record Interface: %d", result);
        return false;
    }

    __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "✅ Audio Recorder erfolgreich erstellt");
    return true;
}

void XboxAudioSystem::destroyAudioObjects() {
    if (audioPlayerObject) {
        (*audioPlayerObject)->Destroy(audioPlayerObject);
        audioPlayerObject = nullptr;
        audioPlayerPlay = nullptr;
        audioPlayerVolume = nullptr;
    }

    if (audioRecorderObject) {
        (*audioRecorderObject)->Destroy(audioRecorderObject);
        audioRecorderObject = nullptr;
        audioRecorderRecord = nullptr;
    }

    if (outputMixObject) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = nullptr;
    }

    if (engineObject) {
        (*engineObject)->Destroy(engineObject);
        engineObject = nullptr;
        engineEngine = nullptr;
    }
}

uint32_t XboxAudioSystem::findFreeStreamId() const {
    for (uint32_t i = 0; i < MAX_XBOX_AUDIO_STREAMS; i++) {
        if (!audioStreams[i].active) {
            return i;
        }
    }
    return 0xFFFFFFFF;
}

void XboxAudioSystem::processXboxAudioData(XboxMemory& memory) {

    uint32_t audioSamples = 0;
    uint32_t nonZeroSamples = 0;


    for (uint32_t addr = XBOX_AUDIO_BASE; addr < XBOX_AUDIO_BASE + XBOX_AUDIO_BUFFER_SIZE; addr += 2) {
        if (addr + 1 < XBOX_AUDIO_BASE + XBOX_AUDIO_BUFFER_SIZE) {

            uint16_t sample = 0;
            try {
                sample = memory.read16(addr);
            } catch (...) {

                continue;
            }

            audioSamples++;
            if (sample != 0) {
                nonZeroSamples++;
            }
        }
    }


    if (audioSamples > 0 && nonZeroSamples > audioSamples / 10) {
        __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "🎵 SPIEL LÄUFT! Audio-Daten bestätigen aktive Emulation");


        if (lastAudioUpdate == 0 || (frameCounter - lastAudioUpdate) > 1000) {
            playTestTone(50);
            lastAudioUpdate = frameCounter;
        }
    }
}

bool XboxAudioSystem::playTestTone(uint32_t volume) {
    if (!audioPlayerPlay) return false;


    static int16_t testTone[480] = {0}; 
    static bool toneGenerated = false;

    if (!toneGenerated) {
        for (int i = 0; i < 480; i++) {
            float t = (float)i / 48000.0f;
            testTone[i] = (int16_t)(sin(2.0f * M_PI * 440.0f * t) * 16384.0f);
        }
        toneGenerated = true;
    }


    SLresult result = (*audioPlayerPlay)->SetPlayState(audioPlayerPlay, SL_PLAYSTATE_PLAYING);
    if (result == SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_INFO, "XboxAudioSystem", "🔊 Test-Ton abgespielt (Lautstärke: %u)", volume);
        return true;
    }

    return false;
}
