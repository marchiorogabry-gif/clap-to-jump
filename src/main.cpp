#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <thread>
#include <atomic>
#include <cmath>

#ifdef GEODE_IS_ANDROID
#include <aaudio/AAudio.h>
#endif

using namespace geode::prelude;

static std::atomic<bool> g_shouldJump{false};
static std::atomic<bool> g_micRunning{false};
static std::thread g_micThread;

#ifdef GEODE_IS_ANDROID
void microphoneLoop() {
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return;

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setSampleRate(builder, 16000);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

    AAudioStream* stream = nullptr;
    if (AAudioStreamBuilder_openStream(builder, &stream) != AAUDIO_OK) {
        AAudioStreamBuilder_delete(builder);
        return;
    }

    AAudioStreamBuilder_delete(builder);

    if (AAudioStream_requestStart(stream) != AAUDIO_OK) {
        AAudioStream_close(stream);
        return;
    }

    const int BUFFER_SIZE = 1024;
    int16_t buffer[BUFFER_SIZE];

    while (g_micRunning) {
        int32_t framesRead = AAudioStream_read(stream, buffer, BUFFER_SIZE, 10000000);
        if (framesRead > 0) {
            double sum = 0.0;
            for (int i = 0; i < framesRead; i++) sum += (double)buffer[i] * buffer[i];
            double rms = sqrt(sum / framesRead) / 32768.0;
            float sensitivity = Mod::get()->getSettingValue<double>("sensitivity");
            if (rms > sensitivity) g_shouldJump = true;
        }
    }

    AAudioStream_requestStop(stream);
    AAudioStream_close(stream);
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
