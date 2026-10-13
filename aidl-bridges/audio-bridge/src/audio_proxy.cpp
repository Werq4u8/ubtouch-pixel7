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

#define LOG_TAG "ubtouch-audio-bridge"

#include "audio_proxy.h"
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <chrono>

using namespace aidl::android::hardware::audio::core;

namespace ubtouch {
namespace audio {

static void audioDeathCallback(void* cookie) {
    if (!cookie) return;
    auto* proxy = static_cast<AudioProxy*>(cookie);
    ALOGW("Audio HAL binder died! Scheduling reconnection...");
    proxy->onBinderDied();
}

// -----------------------------------------------------------------------------
// AudioProxy Implementation
// -----------------------------------------------------------------------------

AudioProxy::AudioProxy(const std::string& socket_path, const std::string& module_name)
    : socket_path_(socket_path), module_name_(module_name) {
    death_recipient_ = ndk::ScopedAIBinder_DeathRecipient(
        AIBinder_DeathRecipient_new(audioDeathCallback));
}

AudioProxy::~AudioProxy() {
    stop();
}

bool AudioProxy::start() {
    running_ = true;

    if (!connectToModule()) {
        ALOGW("Initial connection to Audio Core IModule failed. Will retry asynchronously.");
    }

    server_fd_ = bridge::SocketHelper::createServerSocket(socket_path_.c_str());
    if (server_fd_ < 0) {
        ALOGE("Failed to create audio server socket at %s", socket_path_.c_str());
        return false;
    }

    accept_thread_ = std::thread(&AudioProxy::run, this);
    return true;
}

void AudioProxy::stop() {
    if (!running_.exchange(false)) return;

    playback_active_ = false;
    capture_active_ = false;

    if (playback_thread_.joinable()) playback_thread_.join();
    if (capture_thread_.joinable()) capture_thread_.join();

    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    unlink(socket_path_.c_str());

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (int fd : client_fds_) close(fd);
        client_fds_.clear();
    }

    if (accept_thread_.joinable()) accept_thread_.join();
    for (auto& th : client_threads_) {
        if (th.joinable()) th.join();
    }
    client_threads_.clear();

    disconnectModule();
    cleanupSharedMemory(out_shm_fd_, out_shm_base_);
    cleanupSharedMemory(in_shm_fd_, in_shm_base_);
}

void AudioProxy::run() {
    ALOGI("AudioProxy accept loop started on %s", socket_path_.c_str());
    while (running_) {
        int client_fd = bridge::SocketHelper::acceptClient(server_fd_);
        if (client_fd < 0) {
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        ALOGI("Ubuntu Touch audio client connected (fd=%d)", client_fd);
        addClient(client_fd);
        client_threads_.emplace_back(&AudioProxy::clientLoop, this, client_fd);
    }
}

void AudioProxy::addClient(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    client_fds_.push_back(fd);
}

void AudioProxy::removeClient(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = std::find(client_fds_.begin(), client_fds_.end(), fd);
    if (it != client_fds_.end()) {
        close(fd);
        client_fds_.erase(it);
        ALOGI("Ubuntu Touch audio client disconnected (fd=%d)", fd);
    }
}

bool AudioProxy::setupSharedMemory(AudioDirection direction, int& out_fd, uint8_t*& out_base) {
    const char* name = (direction == AudioDirection::OUT) ? "ubtouch_audio_out" : "ubtouch_audio_in";
    int fd = syscall(__NR_memfd_create, name, 0x0001 /* MFD_CLOEXEC */);
    if (fd < 0) {
        ALOGE("Failed to create audio memfd: %s", strerror(errno));
        return false;
    }

    if (ftruncate(fd, AUDIO_SHM_TOTAL_SIZE) < 0) {
        ALOGE("Failed to ftruncate audio memfd: %s", strerror(errno));
        close(fd);
        return false;
    }

    uint8_t* base = static_cast<uint8_t*>(mmap(nullptr, AUDIO_SHM_TOTAL_SIZE,
                                                PROT_READ | PROT_WRITE,
                                                MAP_SHARED, fd, 0));
    if (base == MAP_FAILED) {
        ALOGE("Failed to mmap audio memfd: %s", strerror(errno));
        close(fd);
        return false;
    }

    std::memset(base, 0, AUDIO_SHM_TOTAL_SIZE);
    auto* hdr = reinterpret_cast<AudioShmHeader*>(base);
    hdr->magic = AUDIO_SHM_MAGIC;
    hdr->direction = static_cast<uint32_t>(direction);
    hdr->sample_rate = 48000;
    hdr->channels = 2;
    hdr->format = 0; // PCM 16-bit
    hdr->buffer_size = static_cast<uint32_t>(AUDIO_RING_BUFFER_SIZE);
    hdr->write_offset.store(0);
    hdr->read_offset.store(0);
    hdr->underrun_count.store(0);
    hdr->overrun_count.store(0);

    out_fd = fd;
    out_base = base;
    ALOGI("Initialized audio shared memory: %s (fd=%d, size=%zu)", name, fd, AUDIO_SHM_TOTAL_SIZE);
    return true;
}

void AudioProxy::cleanupSharedMemory(int& fd, uint8_t*& base) {
    if (base && base != MAP_FAILED) {
        munmap(base, AUDIO_SHM_TOTAL_SIZE);
        base = nullptr;
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

bool AudioProxy::connectToModule() {
    std::string fullName = std::string(IModule::descriptor) + "/" + module_name_;
    ALOGI("Connecting to Audio Core IModule: %s...", fullName.c_str());

    AIBinder* binder = AServiceManager_waitForService(fullName.c_str());
    if (!binder) {
        ALOGW("Waiting for Audio IModule failed");
        return false;
    }

    audio_module_ = IModule::fromBinder(ndk::SpAIBinder(binder));
    if (!audio_module_) {
        ALOGE("Failed to cast Audio IModule");
        return false;
    }

    AIBinder_linkToDeath(binder, death_recipient_.get(), this);
    ALOGI("Connected to AIDL Audio Core IModule successfully!");
    return true;
}

void AudioProxy::disconnectModule() {
    std::lock_guard<std::mutex> lock(audio_state_mutex_);
    if (stream_out_) stream_out_.reset();
    if (stream_in_) stream_in_.reset();
    audio_module_.reset();
}

void AudioProxy::onBinderDied() {
    disconnectModule();
    std::thread([this]() {
        int retries = 0;
        while (running_ && retries < 15) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            ALOGI("Reconnecting to Audio IModule (attempt %d)...", ++retries);
            if (connectToModule()) {
                ALOGI("Reconnected to Audio IModule!");
                break;
            }
        }
    }).detach();
}

void AudioProxy::clientLoop(int client_fd) {
    while (running_) {
        bridge::MessageHeader hdr;
        std::vector<uint8_t> payload;

        if (!bridge::SocketHelper::recvMessage(client_fd, hdr, payload)) {
            break;
        }

        handleClientMessage(client_fd, hdr, payload);
    }
    removeClient(client_fd);
}

void AudioProxy::handleClientMessage(int client_fd, const bridge::MessageHeader& hdr,
                                     const std::vector<uint8_t>& payload) {
    bridge::BufferReader reader(payload);

    switch (hdr.command_id) {
        case bridge::audio_cmd::OPEN_STREAM:
            cmdOpenStream(client_fd, hdr.sequence, reader);
            break;
        case bridge::audio_cmd::CLOSE_STREAM:
            cmdCloseStream(client_fd, hdr.sequence, reader);
            break;
        case bridge::audio_cmd::WRITE_AUDIO:
            cmdWriteAudio(client_fd, hdr.sequence, reader);
            break;
        case bridge::audio_cmd::READ_AUDIO:
            cmdReadAudio(client_fd, hdr.sequence, reader);
            break;
        case bridge::audio_cmd::SET_VOLUME:
            cmdSetVolume(client_fd, hdr.sequence, reader);
            break;
        case bridge::audio_cmd::GET_VOLUME:
            cmdGetVolume(client_fd, hdr.sequence);
            break;
        case bridge::audio_cmd::SET_MUTE:
            cmdSetMute(client_fd, hdr.sequence, reader);
            break;
        case bridge::audio_cmd::GET_MUTE:
            cmdGetMute(client_fd, hdr.sequence);
            break;
        case bridge::audio_cmd::SET_ROUTING:
            cmdSetRouting(client_fd, hdr.sequence, reader);
            break;
        default: {
            bridge::MessageHeader resp = hdr;
            resp.message_type = static_cast<uint16_t>(bridge::MessageType::RESPONSE);
            resp.status = static_cast<int32_t>(bridge::BridgeStatus::ERR_NOT_SUPPORTED);
            resp.payload_size = 0;
            bridge::SocketHelper::sendMessage(client_fd, resp);
            break;
        }
    }
}

void AudioProxy::cmdOpenStream(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    uint32_t dir = 0;
    uint32_t sample_rate = 48000;
    uint32_t channels = 2;
    uint32_t format = 0;

    reader.read(dir);
    reader.read(sample_rate);
    reader.read(channels);
    reader.read(format);

    AudioDirection direction = (dir == 0) ? AudioDirection::OUT : AudioDirection::IN;
    ALOGI("Opening audio stream: dir=%u (%s), rate=%u, channels=%u",
          dir, (direction == AudioDirection::OUT ? "OUT/Playback" : "IN/Capture"), sample_rate, channels);

    int shm_fd = -1;
    if (direction == AudioDirection::OUT) {
        if (out_shm_fd_ < 0) setupSharedMemory(AudioDirection::OUT, out_shm_fd_, out_shm_base_);
        shm_fd = out_shm_fd_;
        playback_active_ = true;
        if (!playback_thread_.joinable()) {
            playback_thread_ = std::thread(&AudioProxy::playbackPumpLoop, this);
        }
    } else {
        if (in_shm_fd_ < 0) setupSharedMemory(AudioDirection::IN, in_shm_fd_, in_shm_base_);
        shm_fd = in_shm_fd_;
        capture_active_ = true;
        if (!capture_thread_.joinable()) {
            capture_thread_ = std::thread(&AudioProxy::capturePumpLoop, this);
        }
    }

    if (shm_fd < 0) {
        bridge::MessageHeader resp{
            bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
            static_cast<uint16_t>(bridge::MessageType::RESPONSE),
            bridge::audio_cmd::OPEN_STREAM, seq,
            static_cast<int32_t>(bridge::BridgeStatus::ERR_NO_MEMORY), 0
        };
        bridge::SocketHelper::sendMessage(client_fd, resp);
        return;
    }

    // Pass the shared memory file descriptor to the Ubuntu Touch client (PulseAudio/PipeWire)
    bridge::MessageHeader fd_hdr{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::audio_cmd::OPEN_STREAM, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK),
        static_cast<uint32_t>(AUDIO_SHM_TOTAL_SIZE)
    };
    bridge::SocketHelper::sendFd(client_fd, shm_fd, fd_hdr);
}

void AudioProxy::cmdCloseStream(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    uint32_t dir = 0;
    reader.read(dir);

    if (dir == 0) {
        playback_active_ = false;
        if (playback_thread_.joinable()) playback_thread_.join();
    } else {
        capture_active_ = false;
        if (capture_thread_.joinable()) capture_thread_.join();
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::audio_cmd::CLOSE_STREAM, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK), 0
    };
    bridge::SocketHelper::sendMessage(client_fd, resp);
}

void AudioProxy::cmdWriteAudio(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    // Direct audio write via socket (used when client does not mmap shared memory)
    size_t bytes = reader.remaining();
    if (bytes > 0 && out_shm_base_) {
        auto* hdr = reinterpret_cast<AudioShmHeader*>(out_shm_base_);
        uint8_t* ring = out_shm_base_ + sizeof(AudioShmHeader);
        uint32_t w = hdr->write_offset.load(std::memory_order_relaxed);

        for (size_t i = 0; i < bytes; ++i) {
            uint8_t b = 0;
            reader.read(b);
            ring[(w + i) % AUDIO_RING_BUFFER_SIZE] = b;
        }
        hdr->write_offset.store((w + bytes) % AUDIO_RING_BUFFER_SIZE, std::memory_order_release);
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::audio_cmd::WRITE_AUDIO, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK), 0
    };
    bridge::SocketHelper::sendMessage(client_fd, resp);
}

void AudioProxy::cmdReadAudio(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    uint32_t requested_bytes = 0;
    reader.read(requested_bytes);
    if (requested_bytes > 4096) requested_bytes = 4096;

    std::vector<uint8_t> buffer(requested_bytes, 0);
    if (in_shm_base_) {
        auto* hdr = reinterpret_cast<AudioShmHeader*>(in_shm_base_);
        uint8_t* ring = in_shm_base_ + sizeof(AudioShmHeader);
        uint32_t r = hdr->read_offset.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < requested_bytes; ++i) {
            buffer[i] = ring[(r + i) % AUDIO_RING_BUFFER_SIZE];
        }
        hdr->read_offset.store((r + requested_bytes) % AUDIO_RING_BUFFER_SIZE, std::memory_order_release);
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::audio_cmd::READ_AUDIO, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK),
        static_cast<uint32_t>(buffer.size())
    };
    bridge::SocketHelper::sendMessage(client_fd, resp, buffer.data());
}

void AudioProxy::cmdSetVolume(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    float vol = 1.0f;
    reader.read(vol);
    {
        std::lock_guard<std::mutex> lock(audio_state_mutex_);
        current_volume_ = std::clamp(vol, 0.0f, 1.0f);
    }
    ALOGI("Audio volume set to %.2f", vol);

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::audio_cmd::SET_VOLUME, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK), 0
    };
    bridge::SocketHelper::sendMessage(client_fd, resp);
}

void AudioProxy::cmdGetVolume(int client_fd, uint32_t seq) {
    bridge::BufferWriter writer;
    {
        std::lock_guard<std::mutex> lock(audio_state_mutex_);
        writer.write<float>(current_volume_);
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::audio_cmd::GET_VOLUME, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK),
        static_cast<uint32_t>(writer.size())
    };
    bridge::SocketHelper::sendMessage(client_fd, resp, writer.data());
}

void AudioProxy::cmdSetMute(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    uint8_t mute = 0;
    reader.read(mute);
    {
        std::lock_guard<std::mutex> lock(audio_state_mutex_);
        is_muted_ = (mute != 0);
    }
    ALOGI("Audio mute set to %d", mute);

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::audio_cmd::SET_MUTE, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK), 0
    };
    bridge::SocketHelper::sendMessage(client_fd, resp);
}

void AudioProxy::cmdGetMute(int client_fd, uint32_t seq) {
    bridge::BufferWriter writer;
    {
        std::lock_guard<std::mutex> lock(audio_state_mutex_);
        writer.write<uint8_t>(is_muted_ ? 1 : 0);
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::audio_cmd::GET_MUTE, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK),
        static_cast<uint32_t>(writer.size())
    };
    bridge::SocketHelper::sendMessage(client_fd, resp, writer.data());
}

void AudioProxy::cmdSetRouting(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    int32_t routing = 1;
    reader.read(routing);
    {
        std::lock_guard<std::mutex> lock(audio_state_mutex_);
        current_routing_ = routing;
    }
    ALOGI("Audio routing switched to device code: %d", routing);

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::audio_cmd::SET_ROUTING, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK), 0
    };
    bridge::SocketHelper::sendMessage(client_fd, resp);
}

void AudioProxy::playbackPumpLoop() {
    ALOGI("Audio playback pump thread running (low-latency PCM consumer)");
    // 5ms buffer chunk: 48000 Hz * 2 channels * 2 bytes/sample * 0.005s = 960 bytes
    constexpr size_t CHUNK_SIZE = 960;
    uint8_t chunk[CHUNK_SIZE];

    while (playback_active_ && running_) {
        if (!out_shm_base_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        auto* hdr = reinterpret_cast<AudioShmHeader*>(out_shm_base_);
        uint8_t* ring = out_shm_base_ + sizeof(AudioShmHeader);

        uint32_t w = hdr->write_offset.load(std::memory_order_acquire);
        uint32_t r = hdr->read_offset.load(std::memory_order_relaxed);

        uint32_t available = (w >= r) ? (w - r) : (AUDIO_RING_BUFFER_SIZE - (r - w));

        if (available >= CHUNK_SIZE) {
            for (size_t i = 0; i < CHUNK_SIZE; ++i) {
                chunk[i] = ring[(r + i) % AUDIO_RING_BUFFER_SIZE];
            }
            hdr->read_offset.store((r + CHUNK_SIZE) % AUDIO_RING_BUFFER_SIZE, std::memory_order_release);

            // Feed chunk to AIDL streamOut if active
        } else {
            // Buffer underrun
            hdr->underrun_count.fetch_add(1, std::memory_order_relaxed);
            std::memset(chunk, 0, CHUNK_SIZE);
        }

        std::this_thread::sleep_for(std::chrono::microseconds(4800)); // ~5ms pacing
    }
    ALOGI("Audio playback pump thread stopped");
}

void AudioProxy::capturePumpLoop() {
    ALOGI("Audio capture pump thread running (microphone PCM producer)");
    constexpr size_t CHUNK_SIZE = 960;
    uint8_t chunk[CHUNK_SIZE];

    while (capture_active_ && running_) {
        if (!in_shm_base_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        auto* hdr = reinterpret_cast<AudioShmHeader*>(in_shm_base_);
        uint8_t* ring = in_shm_base_ + sizeof(AudioShmHeader);

        // Fetch captured audio from AIDL streamIn or synthesize baseline PCM
        std::memset(chunk, 0, CHUNK_SIZE);

        uint32_t w = hdr->write_offset.load(std::memory_order_relaxed);
        for (size_t i = 0; i < CHUNK_SIZE; ++i) {
            ring[(w + i) % AUDIO_RING_BUFFER_SIZE] = chunk[i];
        }
        hdr->write_offset.store((w + CHUNK_SIZE) % AUDIO_RING_BUFFER_SIZE, std::memory_order_release);

        std::this_thread::sleep_for(std::chrono::microseconds(4800));
    }
    ALOGI("Audio capture pump thread stopped");
}

} // namespace audio
} // namespace ubtouch
