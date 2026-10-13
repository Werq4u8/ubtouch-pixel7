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

#ifndef UBTIAL_CAMERA_PROXY_H_
#define UBTIAL_CAMERA_PROXY_H_

#include "bridge_protocol.h"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/binder_auto_utils.h>

#include <aidl/android/hardware/camera/provider/ICameraProvider.h>
#include <aidl/android/hardware/camera/provider/BnCameraProviderCallback.h>
#include <aidl/android/hardware/camera/device/ICameraDevice.h>
#include <aidl/android/hardware/camera/device/ICameraDeviceSession.h>
#include <aidl/android/hardware/camera/device/BnCameraDeviceCallback.h>
#include <aidl/android/hardware/camera/device/Stream.h>
#include <aidl/android/hardware/camera/device/StreamConfiguration.h>
#include <aidl/android/hardware/camera/device/CaptureRequest.h>
#include <aidl/android/hardware/camera/device/CaptureResult.h>

#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

namespace ubtouch {
namespace camera {

// Frame descriptor passed with each preview frame indication
#pragma pack(push, 1)
struct PreviewFrameMeta {
    uint32_t width;
    uint32_t height;
    uint32_t format;       // HAL_PIXEL_FORMAT / FOURCC
    uint32_t stride;
    uint64_t timestamp_ns;
    uint32_t frame_index;
    uint32_t shm_offset;   // Offset in shared memory segment
    uint32_t data_size;
};
#pragma pack(pop)

// Shared memory ring buffer configuration
constexpr size_t SHM_BUFFER_COUNT = 4;
constexpr size_t MAX_FRAME_SIZE = 1920 * 1080 * 3 / 2; // Full HD YUV420 NV21 (approx 3MB)
constexpr size_t SHM_TOTAL_SIZE = SHM_BUFFER_COUNT * MAX_FRAME_SIZE;

class CameraProxy {
public:
    explicit CameraProxy(const std::string& socket_path,
                         const std::string& provider_name = "internal/0");
    ~CameraProxy();

    bool start();
    void stop();
    void run();

    // Client request handling
    void handleClientMessage(int client_fd, const bridge::MessageHeader& hdr,
                             const std::vector<uint8_t>& payload);

    // Callbacks from AIDL CameraProvider & CameraDevice
    void onCameraDeviceStatusChanged(const std::string& camera_id, int32_t status);
    void onTorchModeStatusChanged(const std::string& camera_id, int32_t status);
    void onCaptureResult(const std::vector<aidl::android::hardware::camera::device::CaptureResult>& results);
    void onCaptureError(int32_t error_code, int32_t stream_id, int64_t frame_number);
    void onBinderDied();

private:
    bool connectToProvider();
    void disconnectProvider();
    bool initSharedMemory();
    void cleanupSharedMemory();

    void addClient(int fd);
    void removeClient(int fd);
    void clientLoop(int client_fd);

    // Specific camera commands
    void cmdGetCameraList(int client_fd, uint32_t seq);
    void cmdGetCameraInfo(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdOpenCamera(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdCloseCamera(int client_fd, uint32_t seq);
    void cmdStartPreview(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdStopPreview(int client_fd, uint32_t seq);
    void cmdTakePicture(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdAutoFocus(int client_fd, uint32_t seq);

    // Simulated frame producer loop when camera hardware streams
    void previewCaptureLoop();

    std::string socket_path_;
    std::string provider_name_;
    std::atomic<bool> running_{false};
    int server_fd_{-1};

    // Shared memory buffer
    int shm_fd_{-1};
    uint8_t* shm_base_{nullptr};
    std::atomic<uint32_t> current_shm_buffer_idx_{0};

    // AIDL Camera Interfaces
    std::shared_ptr<aidl::android::hardware::camera::provider::ICameraProvider> provider_;
    std::shared_ptr<aidl::android::hardware::camera::device::ICameraDevice> active_device_;
    std::shared_ptr<aidl::android::hardware::camera::device::ICameraDeviceSession> active_session_;
    ndk::ScopedAIBinder_DeathRecipient death_recipient_;

    // Current camera state
    std::mutex camera_mutex_;
    std::string active_camera_id_;
    std::atomic<bool> preview_running_{false};
    uint32_t preview_width_{1280};
    uint32_t preview_height_{720};
    uint32_t preview_format_{0x11}; // HAL_PIXEL_FORMAT_YCrCb_420_SP (NV21)

    // Clients
    std::mutex clients_mutex_;
    std::vector<int> client_fds_;

    // Threads
    std::thread accept_thread_;
    std::thread preview_thread_;
    std::vector<std::thread> client_threads_;
};

} // namespace camera
} // namespace ubtouch

#endif // UBTIAL_CAMERA_PROXY_H_
