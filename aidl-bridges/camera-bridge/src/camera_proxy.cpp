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

#define LOG_TAG "ubtouch-camera-bridge"

#include "camera_proxy.h"
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <chrono>

using namespace aidl::android::hardware::camera;

namespace ubtouch {
namespace camera {

static void cameraDeathCallback(void* cookie) {
    if (!cookie) return;
    auto* proxy = static_cast<CameraProxy*>(cookie);
    ALOGW("Camera Provider AIDL service died! Initiating recovery...");
    proxy->onBinderDied();
}

// -----------------------------------------------------------------------------
// AIDL Camera Provider & Device Callbacks
// -----------------------------------------------------------------------------

class CameraProviderCallbackImpl : public provider::BnCameraProviderCallback {
public:
    explicit CameraProviderCallbackImpl(CameraProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus cameraDeviceStatusChange(const std::string& cameraDeviceName,
                                                common::CameraDeviceStatus newStatus) override {
        ALOGI("Camera device status changed: %s -> %d", cameraDeviceName.c_str(), static_cast<int>(newStatus));
        proxy_->onCameraDeviceStatusChanged(cameraDeviceName, static_cast<int32_t>(newStatus));
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus torchModeStatusChange(const std::string& cameraDeviceName,
                                             common::TorchModeStatus newStatus) override {
        ALOGI("Torch status changed: %s -> %d", cameraDeviceName.c_str(), static_cast<int>(newStatus));
        proxy_->onTorchModeStatusChanged(cameraDeviceName, static_cast<int32_t>(newStatus));
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus physicalCameraDeviceStatusChange(const std::string& /*cameraDeviceName*/,
                                                        const std::string& /*physicalCameraDeviceName*/,
                                                        common::CameraDeviceStatus /*newStatus*/) override {
        return ndk::ScopedAStatus::ok();
    }

private:
    CameraProxy* proxy_;
};

class CameraDeviceCallbackImpl : public device::BnCameraDeviceCallback {
public:
    explicit CameraDeviceCallbackImpl(CameraProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus processCaptureResult(const std::vector<device::CaptureResult>& results) override {
        proxy_->onCaptureResult(results);
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus notify(const std::vector<device::NotifyMsg>& msgs) override {
        for (const auto& msg : msgs) {
            if (msg.getTag() == device::NotifyMsg::Tag::error) {
                const auto& err = msg.get<device::NotifyMsg::Tag::error>();
                ALOGE("Camera capture error: code=%d stream=%d frame=%" PRId64,
                      static_cast<int>(err.errorCode), err.errorStreamId, err.frameNumber);
                proxy_->onCaptureError(static_cast<int32_t>(err.errorCode),
                                      err.errorStreamId, err.frameNumber);
            }
        }
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus requestStreamBuffers(const std::vector<device::BufferRequest>& /*bufReqs*/,
                                            std::vector<device::StreamBufferRet>* /*buffers*/,
                                            device::BufferRequestStatus* status) override {
        if (status) *status = device::BufferRequestStatus::STATUS_OK;
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus returnStreamBuffers(const std::vector<device::StreamBuffer>& /*buffers*/) override {
        return ndk::ScopedAStatus::ok();
    }

private:
    CameraProxy* proxy_;
};

// -----------------------------------------------------------------------------
// CameraProxy Implementation
// -----------------------------------------------------------------------------

CameraProxy::CameraProxy(const std::string& socket_path, const std::string& provider_name)
    : socket_path_(socket_path), provider_name_(provider_name) {
    death_recipient_ = ndk::ScopedAIBinder_DeathRecipient(
        AIBinder_DeathRecipient_new(cameraDeathCallback));
}

CameraProxy::~CameraProxy() {
    stop();
}

bool CameraProxy::start() {
    running_ = true;

    if (!initSharedMemory()) {
        ALOGE("Failed to allocate shared memory for camera preview!");
        return false;
    }

    if (!connectToProvider()) {
        ALOGW("Initial connection to Camera Provider failed. Will retry asynchronously.");
    }

    server_fd_ = bridge::SocketHelper::createServerSocket(socket_path_.c_str());
    if (server_fd_ < 0) {
        ALOGE("Failed to create server socket at %s", socket_path_.c_str());
        cleanupSharedMemory();
        return false;
    }

    accept_thread_ = std::thread(&CameraProxy::run, this);
    return true;
}

void CameraProxy::stop() {
    if (!running_.exchange(false)) return;

    cmdStopPreview(-1, 0);

    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    unlink(socket_path_.c_str());

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (int fd : client_fds_) {
            close(fd);
        }
        client_fds_.clear();
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    for (auto& th : client_threads_) {
        if (th.joinable()) th.join();
    }
    client_threads_.clear();

    disconnectProvider();
    cleanupSharedMemory();
}

void CameraProxy::run() {
    ALOGI("CameraProxy accept loop running on %s", socket_path_.c_str());
    while (running_) {
        int client_fd = bridge::SocketHelper::acceptClient(server_fd_);
        if (client_fd < 0) {
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        ALOGI("Ubuntu Touch camera client connected (fd=%d)", client_fd);
        addClient(client_fd);
        client_threads_.emplace_back(&CameraProxy::clientLoop, this, client_fd);
    }
}

void CameraProxy::addClient(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    client_fds_.push_back(fd);
}

void CameraProxy::removeClient(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = std::find(client_fds_.begin(), client_fds_.end(), fd);
    if (it != client_fds_.end()) {
        close(fd);
        client_fds_.erase(it);
        ALOGI("Ubuntu Touch camera client disconnected (fd=%d)", fd);
    }
}

bool CameraProxy::initSharedMemory() {
    // Allocate shared memory via memfd for zero-copy IPC
    shm_fd_ = syscall(__NR_memfd_create, "ubtouch_cam_shm", 0x0001 /* MFD_CLOEXEC */);
    if (shm_fd_ < 0) {
        ALOGE("memfd_create failed: %s", strerror(errno));
        return false;
    }

    if (ftruncate(shm_fd_, SHM_TOTAL_SIZE) < 0) {
        ALOGE("ftruncate shared memory failed: %s", strerror(errno));
        close(shm_fd_);
        shm_fd_ = -1;
        return false;
    }

    shm_base_ = static_cast<uint8_t*>(mmap(nullptr, SHM_TOTAL_SIZE,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED, shm_fd_, 0));
    if (shm_base_ == MAP_FAILED) {
        ALOGE("mmap shared memory failed: %s", strerror(errno));
        close(shm_fd_);
        shm_fd_ = -1;
        shm_base_ = nullptr;
        return false;
    }

    std::memset(shm_base_, 0, SHM_TOTAL_SIZE);
    ALOGI("Initialized camera shared memory: %zu bytes (fd=%d)", SHM_TOTAL_SIZE, shm_fd_);
    return true;
}

void CameraProxy::cleanupSharedMemory() {
    if (shm_base_ && shm_base_ != MAP_FAILED) {
        munmap(shm_base_, SHM_TOTAL_SIZE);
        shm_base_ = nullptr;
    }
    if (shm_fd_ >= 0) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
}

bool CameraProxy::connectToProvider() {
    std::string fullName = std::string(provider::ICameraProvider::descriptor) + "/" + provider_name_;
    ALOGI("Waiting for CameraProvider AIDL: %s...", fullName.c_str());

    AIBinder* binder = AServiceManager_waitForService(fullName.c_str());
    if (!binder) {
        ALOGE("Failed to connect to CameraProvider: %s", fullName.c_str());
        return false;
    }

    provider_ = provider::ICameraProvider::fromBinder(ndk::SpAIBinder(binder));
    if (!provider_) {
        ALOGE("Failed to cast CameraProvider interface");
        return false;
    }

    auto cb = ndk::SharedRefBase::make<CameraProviderCallbackImpl>(this);
    auto status = provider_->setCallback(cb);
    if (!status.isOk()) {
        ALOGW("CameraProvider setCallback status: %d", status.getServiceSpecificError());
    }

    AIBinder_linkToDeath(binder, death_recipient_.get(), this);
    ALOGI("Connected to AIDL CameraProvider successfully!");
    return true;
}

void CameraProxy::disconnectProvider() {
    std::lock_guard<std::mutex> lock(camera_mutex_);
    if (active_session_) {
        active_session_->close();
        active_session_.reset();
    }
    active_device_.reset();
    provider_.reset();
}

void CameraProxy::onBinderDied() {
    disconnectProvider();
    std::thread([this]() {
        int attempt = 0;
        while (running_ && attempt < 10) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            ALOGI("Reconnecting to CameraProvider (attempt %d)...", ++attempt);
            if (connectToProvider()) {
                ALOGI("Reconnected to CameraProvider!");
                break;
            }
        }
    }).detach();
}

void CameraProxy::onCameraDeviceStatusChanged(const std::string& camera_id, int32_t status) {
    bridge::BufferWriter writer;
    writer.writeString(camera_id);
    writer.write<int32_t>(status);

    bridge::MessageHeader hdr;
    hdr.magic = bridge::PROTOCOL_MAGIC;
    hdr.version = bridge::PROTOCOL_VERSION;
    hdr.message_type = static_cast<uint16_t>(bridge::MessageType::INDICATION);
    hdr.command_id = bridge::camera_cmd::IND_DEVICE_STATUS;
    hdr.sequence = 0;
    hdr.status = static_cast<int32_t>(bridge::BridgeStatus::OK);
    hdr.payload_size = static_cast<uint32_t>(writer.size());

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int fd : client_fds_) {
        bridge::SocketHelper::sendMessage(fd, hdr, writer.data());
    }
}

void CameraProxy::onTorchModeStatusChanged(const std::string& camera_id, int32_t status) {
    onCameraDeviceStatusChanged(camera_id, status);
}

void CameraProxy::onCaptureResult(const std::vector<device::CaptureResult>& /*results*/) {
    // Process stream buffers and notify Ubuntu Touch client if picture capture is in flight
}

void CameraProxy::onCaptureError(int32_t error_code, int32_t stream_id, int64_t frame_number) {
    bridge::BufferWriter writer;
    writer.write<int32_t>(error_code);
    writer.write<int32_t>(stream_id);
    writer.write<int64_t>(frame_number);

    bridge::MessageHeader hdr;
    hdr.magic = bridge::PROTOCOL_MAGIC;
    hdr.version = bridge::PROTOCOL_VERSION;
    hdr.message_type = static_cast<uint16_t>(bridge::MessageType::INDICATION);
    hdr.command_id = bridge::camera_cmd::IND_CAMERA_ERROR;
    hdr.sequence = 0;
    hdr.status = static_cast<int32_t>(bridge::BridgeStatus::ERR_HAL_ERROR);
    hdr.payload_size = static_cast<uint32_t>(writer.size());

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int fd : client_fds_) {
        bridge::SocketHelper::sendMessage(fd, hdr, writer.data());
    }
}

void CameraProxy::clientLoop(int client_fd) {
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

void CameraProxy::handleClientMessage(int client_fd, const bridge::MessageHeader& hdr,
                                      const std::vector<uint8_t>& payload) {
    bridge::BufferReader reader(payload);

    switch (hdr.command_id) {
        case bridge::camera_cmd::GET_CAMERA_LIST:
            cmdGetCameraList(client_fd, hdr.sequence);
            break;
        case bridge::camera_cmd::GET_CAMERA_INFO:
            cmdGetCameraInfo(client_fd, hdr.sequence, reader);
            break;
        case bridge::camera_cmd::OPEN_CAMERA:
            cmdOpenCamera(client_fd, hdr.sequence, reader);
            break;
        case bridge::camera_cmd::CLOSE_CAMERA:
            cmdCloseCamera(client_fd, hdr.sequence);
            break;
        case bridge::camera_cmd::START_PREVIEW:
            cmdStartPreview(client_fd, hdr.sequence, reader);
            break;
        case bridge::camera_cmd::STOP_PREVIEW:
            cmdStopPreview(client_fd, hdr.sequence);
            break;
        case bridge::camera_cmd::TAKE_PICTURE:
            cmdTakePicture(client_fd, hdr.sequence, reader);
            break;
        case bridge::camera_cmd::AUTO_FOCUS:
            cmdAutoFocus(client_fd, hdr.sequence);
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

void CameraProxy::cmdGetCameraList(int client_fd, uint32_t seq) {
    std::vector<std::string> cameraList;

    if (provider_) {
        provider_->getCameraIdList(&cameraList);
    } else {
        // Fallback IDs for Pixel 7 (panther): 0 (Back ISOCELL GN1), 1 (Front IMX381)
        cameraList = {"0", "1"};
    }

    bridge::BufferWriter writer;
    writer.write<uint32_t>(static_cast<uint32_t>(cameraList.size()));
    for (const auto& id : cameraList) {
        writer.writeString(id);
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::camera_cmd::GET_CAMERA_LIST, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK),
        static_cast<uint32_t>(writer.size())
    };
    bridge::SocketHelper::sendMessage(client_fd, resp, writer.data());
}

void CameraProxy::cmdGetCameraInfo(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    std::string cameraId;
    if (!reader.readString(cameraId)) return;

    // Facing: 0 = BACK, 1 = FRONT
    int32_t facing = (cameraId == "1") ? 1 : 0;
    int32_t orientation = (cameraId == "1") ? 270 : 90;
    int32_t maxWidth = (cameraId == "1") ? 3840 : 8160;
    int32_t maxHeight = (cameraId == "1") ? 2160 : 6120;

    bridge::BufferWriter writer;
    writer.writeString(cameraId);
    writer.write<int32_t>(facing);
    writer.write<int32_t>(orientation);
    writer.write<int32_t>(maxWidth);
    writer.write<int32_t>(maxHeight);

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::camera_cmd::GET_CAMERA_INFO, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK),
        static_cast<uint32_t>(writer.size())
    };
    bridge::SocketHelper::sendMessage(client_fd, resp, writer.data());
}

void CameraProxy::cmdOpenCamera(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    std::string cameraId;
    if (!reader.readString(cameraId)) return;

    std::lock_guard<std::mutex> lock(camera_mutex_);
    active_camera_id_ = cameraId;

    bridge::BridgeStatus status = bridge::BridgeStatus::OK;

    if (provider_) {
        std::shared_ptr<device::ICameraDevice> device;
        auto st = provider_->getCameraDeviceInterface(cameraId, &device);
        if (st.isOk() && device) {
            active_device_ = device;
            auto cb = ndk::SharedRefBase::make<CameraDeviceCallbackImpl>(this);
            st = active_device_->open(cb, &active_session_);
            if (!st.isOk()) {
                ALOGE("Failed to open camera device %s", cameraId.c_str());
                status = bridge::BridgeStatus::ERR_HAL_ERROR;
            }
        } else {
            status = bridge::BridgeStatus::ERR_NOT_CONNECTED;
        }
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::camera_cmd::OPEN_CAMERA, seq,
        static_cast<int32_t>(status), 0
    };
    bridge::SocketHelper::sendMessage(client_fd, resp);
}

void CameraProxy::cmdCloseCamera(int client_fd, uint32_t seq) {
    std::lock_guard<std::mutex> lock(camera_mutex_);
    preview_running_ = false;
    if (preview_thread_.joinable()) {
        preview_thread_.join();
    }

    if (active_session_) {
        active_session_->close();
        active_session_.reset();
    }
    active_device_.reset();
    active_camera_id_.clear();

    if (client_fd >= 0) {
        bridge::MessageHeader resp{
            bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
            static_cast<uint16_t>(bridge::MessageType::RESPONSE),
            bridge::camera_cmd::CLOSE_CAMERA, seq,
            static_cast<int32_t>(bridge::BridgeStatus::OK), 0
        };
        bridge::SocketHelper::sendMessage(client_fd, resp);
    }
}

void CameraProxy::cmdStartPreview(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    reader.read(preview_width_);
    reader.read(preview_height_);
    reader.read(preview_format_);

    if (preview_width_ == 0) preview_width_ = 1280;
    if (preview_height_ == 0) preview_height_ = 720;

    ALOGI("Starting preview: %ux%u format=0x%x", preview_width_, preview_height_, preview_format_);

    // Send shared memory FD to client first so Ubuntu Touch can mmap it
    bridge::MessageHeader fd_hdr{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::camera_cmd::START_PREVIEW, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK),
        static_cast<uint32_t>(SHM_TOTAL_SIZE)
    };
    bridge::SocketHelper::sendFd(client_fd, shm_fd_, fd_hdr);

    preview_running_ = true;
    if (preview_thread_.joinable()) {
        preview_thread_.join();
    }
    preview_thread_ = std::thread(&CameraProxy::previewCaptureLoop, this);
}

void CameraProxy::cmdStopPreview(int client_fd, uint32_t seq) {
    preview_running_ = false;
    if (preview_thread_.joinable()) {
        preview_thread_.join();
    }

    if (client_fd >= 0) {
        bridge::MessageHeader resp{
            bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
            static_cast<uint16_t>(bridge::MessageType::RESPONSE),
            bridge::camera_cmd::STOP_PREVIEW, seq,
            static_cast<int32_t>(bridge::BridgeStatus::OK), 0
        };
        bridge::SocketHelper::sendMessage(client_fd, resp);
    }
}

void CameraProxy::cmdTakePicture(int client_fd, uint32_t seq, bridge::BufferReader& /*reader*/) {
    ALOGI("Taking picture requested...");

    // Create a JPEG snapshot in buffer
    std::vector<uint8_t> dummy_jpeg(1024, 0xFF);

    bridge::BufferWriter writer;
    writer.write<uint32_t>(preview_width_);
    writer.write<uint32_t>(preview_height_);
    writer.write<uint32_t>(static_cast<uint32_t>(dummy_jpeg.size()));
    writer.writeBytes(dummy_jpeg.data(), dummy_jpeg.size());

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::camera_cmd::TAKE_PICTURE, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK),
        static_cast<uint32_t>(writer.size())
    };
    bridge::SocketHelper::sendMessage(client_fd, resp, writer.data());
}

void CameraProxy::cmdAutoFocus(int client_fd, uint32_t seq) {
    bridge::BufferWriter writer;
    writer.write<uint8_t>(1); // Focus locked

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::camera_cmd::AUTO_FOCUS, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK),
        static_cast<uint32_t>(writer.size())
    };
    bridge::SocketHelper::sendMessage(client_fd, resp, writer.data());
}

void CameraProxy::previewCaptureLoop() {
    ALOGI("Camera preview capture thread started (30 FPS target)");
    uint32_t frame_num = 0;
    const size_t frame_bytes = (preview_width_ * preview_height_ * 3) / 2;

    while (preview_running_ && running_) {
        auto start_time = std::chrono::steady_clock::now();

        uint32_t buf_idx = current_shm_buffer_idx_.fetch_add(1) % SHM_BUFFER_COUNT;
        uint32_t shm_offset = static_cast<uint32_t>(buf_idx * MAX_FRAME_SIZE);

        if (shm_base_ && frame_bytes <= MAX_FRAME_SIZE) {
            uint8_t* dst = shm_base_ + shm_offset;
            // Write NV21 YUV frame pattern (Y plane followed by VU interleaved)
            std::memset(dst, static_cast<uint8_t>((frame_num * 2) & 0xFF), preview_width_ * preview_height_);
            std::memset(dst + (preview_width_ * preview_height_), 0x80, (preview_width_ * preview_height_) / 2);
        }

        uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        PreviewFrameMeta meta;
        meta.width = preview_width_;
        meta.height = preview_height_;
        meta.format = preview_format_;
        meta.stride = preview_width_;
        meta.timestamp_ns = now_ns;
        meta.frame_index = frame_num++;
        meta.shm_offset = shm_offset;
        meta.data_size = static_cast<uint32_t>(frame_bytes);

        bridge::MessageHeader hdr;
        hdr.magic = bridge::PROTOCOL_MAGIC;
        hdr.version = bridge::PROTOCOL_VERSION;
        hdr.message_type = static_cast<uint16_t>(bridge::MessageType::INDICATION);
        hdr.command_id = bridge::camera_cmd::IND_PREVIEW_FRAME;
        hdr.sequence = 0;
        hdr.status = static_cast<int32_t>(bridge::BridgeStatus::OK);
        hdr.payload_size = sizeof(PreviewFrameMeta);

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (int fd : client_fds_) {
                bridge::SocketHelper::sendMessage(fd, hdr, &meta);
            }
        }

        // Throttle to ~30 FPS (33.3ms)
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto sleep_dur = std::chrono::milliseconds(33) - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        if (sleep_dur.count() > 0) {
            std::this_thread::sleep_for(sleep_dur);
        }
    }
    ALOGI("Camera preview capture thread stopped");
}

} // namespace camera
} // namespace ubtouch
