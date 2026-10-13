/*
 * Copyright (C) 2024 The Ubuntu Touch Pixel 7 Porting Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef UBTIAL_AUDIO_PROXY_H_
#define UBTIAL_AUDIO_PROXY_H_

#include "bridge_protocol.h"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/binder_auto_utils.h>

#include <aidl/android/hardware/audio/core/IModule.h>
#include <aidl/android/hardware/audio/core/IStreamOut.h>
#include <aidl/android/hardware/audio/core/IStreamIn.h>
#include <aidl/android/hardware/audio/core/IStreamCallback.h>
#include <aidl/android/hardware/audio/core/BnStreamCallback.h>

#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <memory>

namespace ubtouch {
namespace audio {

// Audio direction enum
enum class AudioDirection : uint32_t {
    OUT = 0, // Playback (Speaker, Headset, Earpiece)
    IN  = 1  // Capture (Microphone)
};

// Low-latency shared memory audio ring buffer control block
#pragma pack(push, 1)
struct AudioShmHeader {
    uint32_t magic;           // 0x55424155 ("UBAU")
    uint32_t direction;       // AudioDirection (OUT=0, IN=1)
    uint32_t sample_rate;     // Default: 48000 Hz
    uint32_t channels;        // Default: 2 (stereo)
    uint32_t format;          // 0 = PCM 16-bit, 1 = Float 32-bit
    uint32_t buffer_size;     // Size of audio data payload ring buffer
    std::atomic<uint32_t> write_offset;
    std::atomic<uint32_t> read_offset;
    std::atomic<uint32_t> underrun_count;
    std::atomic<uint32_t> overrun_count;
};
#pragma pack(pop)

constexpr uint32_t AUDIO_SHM_MAGIC = 0x55424155;
constexpr size_t AUDIO_RING_BUFFER_SIZE = 64 * 1024; // 64 KB (~340ms at 48kHz 16-bit stereo)
constexpr size_t AUDIO_SHM_TOTAL_SIZE = sizeof(AudioShmHeader) + AUDIO_RING_BUFFER_SIZE;

class AudioProxy {
public:
    explicit AudioProxy(const std::string& socket_path,
                        const std::string& module_name = "default");
    ~AudioProxy();

    bool start();
    void stop();
    void run();

    void handleClientMessage(int client_fd, const bridge::MessageHeader& hdr,
                             const std::vector<uint8_t>& payload);

    void onBinderDied();

private:
    bool connectToModule();
    void disconnectModule();

    bool setupSharedMemory(AudioDirection direction, int& out_fd, uint8_t*& out_base);
    void cleanupSharedMemory(int& fd, uint8_t*& base);

    void addClient(int fd);
    void removeClient(int fd);
    void clientLoop(int client_fd);

    // Audio command handlers
    void cmdOpenStream(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdCloseStream(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdWriteAudio(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdReadAudio(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdSetVolume(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdGetVolume(int client_fd, uint32_t seq);
    void cmdSetMute(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdGetMute(int client_fd, uint32_t seq);
    void cmdSetRouting(int client_fd, uint32_t seq, bridge::BufferReader& reader);

    // Realtime background audio pump threads
    void playbackPumpLoop();
    void capturePumpLoop();

    std::string socket_path_;
    std::string module_name_;
    std::atomic<bool> running_{false};
    int server_fd_{-1};

    // AIDL Audio Core
    std::shared_ptr<aidl::android::hardware::audio::core::IModule> audio_module_;
    std::shared_ptr<aidl::android::hardware::audio::core::IStreamOut> stream_out_;
    std::shared_ptr<aidl::android::hardware::audio::core::IStreamIn> stream_in_;
    ndk::ScopedAIBinder_DeathRecipient death_recipient_;

    // Playback Shared Memory & Thread
    int out_shm_fd_{-1};
    uint8_t* out_shm_base_{nullptr};
    std::atomic<bool> playback_active_{false};
    std::thread playback_thread_;

    // Capture Shared Memory & Thread
    int in_shm_fd_{-1};
    uint8_t* in_shm_base_{nullptr};
    std::atomic<bool> capture_active_{false};
    std::thread capture_thread_;

    // Volume & Mute state
    std::mutex audio_state_mutex_;
    float current_volume_{1.0f};
    bool is_muted_{false};
    int32_t current_routing_{1}; // 1 = Speaker, 2 = Earpiece, 3 = Wired Headset, 4 = BT SCO

    // Client connection tracking
    std::mutex clients_mutex_;
    std::vector<int> client_fds_;

    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
};

} // namespace audio
} // namespace ubtouch

#endif // UBTIAL_AUDIO_PROXY_H_
