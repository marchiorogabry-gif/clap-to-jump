#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <thread>
#include <atomic>

#ifdef GEODE_IS_ANDROID
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#endif

using namespace geode::prelude;

static std::atomic<bool> g_shouldJump{false};
static std::atomic<bool> g_micRunning{false};
static std::thread g_micThread;

#ifdef GEODE_IS_ANDROID
void microphoneLoop() {
    SLObjectItf engineObj = nullptr;
    SLEngineItf engine = nullptr;

    SLresult result = slCreateEngine(&engineObj, 0, nullptr, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS || !engineObj) return;

    result = (*engineObj)->Realize(engineObj, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) { (*engineObj)->Destroy(engineObj); return; }

    result = (*engineObj)->GetInterface(engineObj, SL_IID_ENGINE, &engine);
    if (result != SL_RESULT_SUCCESS) { (*engineObj)->Destroy(engineObj); return; }

    SLDataLocator_IODevice micLocator = {SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT, SL_DEFAULTDEVICEID_AUDIOINPUT, nullptr};
    SLDataSource audioSrc = {&micLocator, nullptr};
    SLDataLocator_AndroidSimpleBufferQueue bufQueue = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM format = {SL_DATAFORMAT_PCM, 1, SL_SAMPLINGRATE_16, SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN};
    SLDataSink audioSnk = {&bufQueue, &format};

    const SLInterfaceID ids[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean req[] = {SL_BOOLEAN_TRUE};

    SLObjectItf recorderObj = nullptr;
    result = (*engine)->CreateAudioRecorder(engine, &recorderObj, &audioSrc, &audioSnk, 1, ids, req);
    if (result != SL_RESULT_SUCCESS || !recorderObj) { (*engineObj)->Destroy(engineObj); return; }

    result = (*recorderObj)->Realize(recorderObj, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) { (*recorderObj)->Destroy(recorderObj); (*engineObj)->Destroy(engineObj); return; }

    SLRecordItf recorder = nullptr;
    result = (*recorderObj)->GetInterface(recorderObj, SL_IID_RECORD, &recorder);
    if (result != SL_RESULT_SUCCESS) { (*recorderObj)->Destroy(recorderObj); (*engineObj)->Destroy(engineObj); return; }

    SLAndroidSimpleBufferQueueItf recBufQueue = nullptr;
    result = (*recorderObj)->GetInterface(recorderObj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &recBufQueue);
    if (result != SL_RESULT_SUCCESS) { (*recorderObj)->Destroy(recorderObj); (*engineObj)->Destroy(engineObj); return; }

    const int BUFFER_SIZE = 1024;
    int16_t buffer[BUFFER_SIZE] = {0};

    (*recBufQueue)->Enqueue(recBufQueue, buffer, BUFFER_SIZE * sizeof(int16_t));
    (*recorder)->SetRecordState(recorder, SL_RECORDSTATE_RECORDING);

    while (g_micRunning) {
        SLAndroidSimpleBufferQueueState state;
        (*recBufQueue)->GetState(recBufQueue, &state);
        if (state.count == 0) {
            double sum = 0.0;
            for (int i = 0; i < BUFFER_SIZE; i++) sum += (double)buffer[i] * buffer[i];
            double rms = sqrt(sum / BUFFER_SIZE) / 32768.0;
            float sensitivity = Mod::get()->getSettingValue<double>("sensitivity");
            if (rms > sensitivity) g_shouldJump = true;
            (*recBufQueue)->Enqueue(recBufQueue, buffer, BUFFER_SIZE * sizeof(int16_t));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    (*recorder)->SetRecordState(recorder, SL_RECORDSTATE_STOPPED);
    (*recorderObj)->Destroy(recorderObj);
    (*engineObj)->Destroy(engineObj);
}
#endif

class $modify(PlayLayer) {
    struct Fields {
        float m_releaseTimer = 0.f;
        bool m_shouldRelease = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
#ifdef GEODE_IS_ANDROID
        g_micRunning = true;
        g_shouldJump = false;
        if (g_micThread.joinable()) g_micThread.join();
        g_micThread = std::thread(microphoneLoop);
#endif
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);
#ifdef GEODE_IS_ANDROID
        if (g_shouldJump.exchange(false)) {
            if (m_player1) {
                m_player1->pushButton(PlayerButton::Jump);
                m_fields->m_shouldRelease = true;
                m_fields->m_releaseTimer = 0.05f;
            }
        }
        if (m_fields->m_shouldRelease) {
            m_fields->m_releaseTimer -= dt;
            if (m_fields->m_releaseTimer <= 0.f) {
                m_fields->m_shouldRelease = false;
                if (m_player1) m_player1->releaseButton(PlayerButton::Jump);
            }
        }
#endif
    }

    void onQuit() {
#ifdef GEODE_IS_ANDROID
        g_micRunning = false;
        if (g_micThread.joinable()) g_micThread.join();
#endif
        PlayLayer::onQuit();
    }
};
