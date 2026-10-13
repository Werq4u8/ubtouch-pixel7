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

#ifndef UBTIAL_GNSS_PROXY_H_
#define UBTIAL_GNSS_PROXY_H_

#include "bridge_protocol.h"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/binder_auto_utils.h>

#include <aidl/android/hardware/gnss/IGnss.h>
#include <aidl/android/hardware/gnss/IGnssCallback.h>
#include <aidl/android/hardware/gnss/BnGnssCallback.h>
#include <aidl/android/hardware/gnss/GnssLocation.h>

#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <memory>

namespace ubtouch {
namespace gnss {

#pragma pack(push, 1)
struct LocationData {
    int32_t  gnss_location_flags;
    double   latitude_degrees;
    double   longitude_degrees;
    double   altitude_meters;
    float    speed_mps;
    float    bearing_degrees;
    float    horizontal_accuracy_meters;
    float    vertical_accuracy_meters;
    float    speed_accuracy_mps;
    float    bearing_accuracy_degrees;
    int64_t  timestamp_ms;
    int64_t  elapsed_realtime_ns;
};
#pragma pack(pop)

class GnssProxy {
public:
    explicit GnssProxy(const std::string& socket_path,
                       const std::string& service_instance = "default");
    ~GnssProxy();

    bool start();
    void stop();
    void run();

    void handleClientMessage(int client_fd, const bridge::MessageHeader& hdr,
                             const std::vector<uint8_t>& payload);

    // Callbacks from AIDL IGnssCallback
    void onLocationReceived(const aidl::android::hardware::gnss::GnssLocation& location);
    void onStatusReceived(int32_t status);
    void onSvStatusReceived(const std::vector<aidl::android::hardware::gnss::IGnssCallback::GnssSvInfo>& svInfo);
    void onNmeaReceived(int64_t timestamp_ms, const std::string& nmea);
    void onCapabilitiesReceived(int32_t capabilities);
    void onBinderDied();

private:
    bool connectToGnss();
    void disconnectGnss();

    void addClient(int fd);
    void removeClient(int fd);
    void clientLoop(int client_fd);

    void broadcast(uint32_t cmd_id, const void* data, size_t size);

    // Command handlers
    void cmdStart(int client_fd, uint32_t seq);
    void cmdStop(int client_fd, uint32_t seq);
    void cmdGetLocation(int client_fd, uint32_t seq);
    void cmdInjectTime(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdInjectLocation(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdDeleteAidingData(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdSetPositionMode(int client_fd, uint32_t seq, bridge::BufferReader& reader);

    std::string socket_path_;
    std::string service_instance_;
    std::atomic<bool> running_{false};
    int server_fd_{-1};

    // AIDL IGnss
    std::shared_ptr<aidl::android::hardware::gnss::IGnss> gnss_;
    ndk::ScopedAIBinder_DeathRecipient death_recipient_;

    // Last known fix
    std::mutex location_mutex_;
    LocationData last_location_{};
    bool has_location_fix_{false};

    // Active GNSS state
    std::atomic<bool> is_navigating_{false};

    // Clients
    std::mutex clients_mutex_;
    std::vector<int> client_fds_;

    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
};

} // namespace gnss
} // namespace ubtouch

#endif // UBTIAL_GNSS_PROXY_H_
