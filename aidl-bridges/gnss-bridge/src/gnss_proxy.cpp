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

#define LOG_TAG "ubtouch-gnss-bridge"

#include "gnss_proxy.h"
#include <algorithm>
#include <cstring>
#include <chrono>

using namespace aidl::android::hardware::gnss;

namespace ubtouch {
namespace gnss {

static void gnssDeathCallback(void* cookie) {
    if (!cookie) return;
    auto* proxy = static_cast<GnssProxy*>(cookie);
    ALOGW("GNSS HAL binder died! Scheduling reconnection...");
    proxy->onBinderDied();
}

// -----------------------------------------------------------------------------
// AIDL IGnssCallback Implementation
// -----------------------------------------------------------------------------

class GnssCallbackImpl : public BnGnssCallback {
public:
    explicit GnssCallbackImpl(GnssProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus gnssLocationCb(const GnssLocation& location) override {
        proxy_->onLocationReceived(location);
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus gnssStatusCb(GnssStatusValue status) override {
        proxy_->onStatusReceived(static_cast<int32_t>(status));
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus gnssSvStatusCb(const std::vector<IGnssCallback::GnssSvInfo>& svInfo) override {
        proxy_->onSvStatusReceived(svInfo);
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus gnssNmeaCb(int64_t timestamp, const std::string& nmea) override {
        proxy_->onNmeaReceived(timestamp, nmea);
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus gnssSetCapabilitiesCb(int capabilities) override {
        proxy_->onCapabilitiesReceived(capabilities);
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus gnssAcquireWakelockCb() override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus gnssReleaseWakelockCb() override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus gnssRequestTimeCb() override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus gnssRequestLocationCb(bool /*independentFromGnss*/, bool /*isUserEmergency*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus gnssSetSystemInfoCb(const IGnssCallback::GnssSystemInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }

private:
    GnssProxy* proxy_;
};

// -----------------------------------------------------------------------------
// GnssProxy Implementation
// -----------------------------------------------------------------------------

GnssProxy::GnssProxy(const std::string& socket_path, const std::string& service_instance)
    : socket_path_(socket_path), service_instance_(service_instance) {
    death_recipient_ = ndk::ScopedAIBinder_DeathRecipient(
        AIBinder_DeathRecipient_new(gnssDeathCallback));
}

GnssProxy::~GnssProxy() {
    stop();
}

bool GnssProxy::start() {
    running_ = true;

    if (!connectToGnss()) {
        ALOGW("Initial connection to GNSS HAL failed. Reconnect scheduled asynchronously.");
    }

    server_fd_ = bridge::SocketHelper::createServerSocket(socket_path_.c_str());
    if (server_fd_ < 0) {
        ALOGE("Failed to create GNSS server socket at %s", socket_path_.c_str());
        return false;
    }

    accept_thread_ = std::thread(&GnssProxy::run, this);
    return true;
}

void GnssProxy::stop() {
    if (!running_.exchange(false)) return;

    if (is_navigating_) {
        cmdStop(-1, 0);
    }

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

    disconnectGnss();
}

void GnssProxy::run() {
    ALOGI("GnssProxy accept loop running on %s", socket_path_.c_str());
    while (running_) {
        int client_fd = bridge::SocketHelper::acceptClient(server_fd_);
        if (client_fd < 0) {
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        ALOGI("Ubuntu Touch GNSS client connected (fd=%d)", client_fd);
        addClient(client_fd);
        client_threads_.emplace_back(&GnssProxy::clientLoop, this, client_fd);
    }
}

void GnssProxy::addClient(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    client_fds_.push_back(fd);
}

void GnssProxy::removeClient(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = std::find(client_fds_.begin(), client_fds_.end(), fd);
    if (it != client_fds_.end()) {
        close(fd);
        client_fds_.erase(it);
        ALOGI("Ubuntu Touch GNSS client disconnected (fd=%d)", fd);
    }
}

bool GnssProxy::connectToGnss() {
    std::string fullName = std::string(IGnss::descriptor) + "/" + service_instance_;
    ALOGI("Connecting to GNSS AIDL service: %s...", fullName.c_str());

    AIBinder* binder = AServiceManager_waitForService(fullName.c_str());
    if (!binder) {
        ALOGW("Waiting for GNSS service failed");
        return false;
    }

    gnss_ = IGnss::fromBinder(ndk::SpAIBinder(binder));
    if (!gnss_) {
        ALOGE("Failed to cast IGnss interface");
        return false;
    }

    auto cb = ndk::SharedRefBase::make<GnssCallbackImpl>(this);
    auto status = gnss_->setCallback(cb);
    if (!status.isOk()) {
        ALOGE("IGnss setCallback failed: %d", status.getServiceSpecificError());
        return false;
    }

    AIBinder_linkToDeath(binder, death_recipient_.get(), this);
    ALOGI("Connected to AIDL IGnss successfully!");
    return true;
}

void GnssProxy::disconnectGnss() {
    gnss_.reset();
}

void GnssProxy::onBinderDied() {
    disconnectGnss();
    std::thread([this]() {
        int retries = 0;
        while (running_ && retries < 15) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            ALOGI("Reconnecting to GNSS HAL (attempt %d)...", ++retries);
            if (connectToGnss()) {
                ALOGI("Reconnected to GNSS HAL!");
                if (is_navigating_ && gnss_) {
                    gnss_->start();
                }
                break;
            }
        }
    }).detach();
}

void GnssProxy::broadcast(uint32_t cmd_id, const void* data, size_t size) {
    bridge::MessageHeader hdr;
    hdr.magic = bridge::PROTOCOL_MAGIC;
    hdr.version = bridge::PROTOCOL_VERSION;
    hdr.message_type = static_cast<uint16_t>(bridge::MessageType::INDICATION);
    hdr.command_id = cmd_id;
    hdr.sequence = 0;
    hdr.status = static_cast<int32_t>(bridge::BridgeStatus::OK);
    hdr.payload_size = static_cast<uint32_t>(size);

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int fd : client_fds_) {
        bridge::SocketHelper::sendMessage(fd, hdr, data);
    }
}

void GnssProxy::onLocationReceived(const GnssLocation& location) {
    LocationData loc{};
    loc.gnss_location_flags = location.gnssLocationFlags;
    loc.latitude_degrees = location.latitudeDegrees;
    loc.longitude_degrees = location.longitudeDegrees;
    loc.altitude_meters = location.altitudeMeters;
    loc.speed_mps = location.speedMetersPerSec;
    loc.bearing_degrees = location.bearingDegrees;
    loc.horizontal_accuracy_meters = location.horizontalAccuracyMeters;
    loc.vertical_accuracy_meters = location.verticalAccuracyMeters;
    loc.speed_accuracy_mps = location.speedAccuracyMetersPerSecond;
    loc.bearing_accuracy_degrees = location.bearingAccuracyDegrees;
    loc.timestamp_ms = location.timestampMillis;
    loc.elapsed_realtime_ns = location.elapsedRealtime.flags;

    {
        std::lock_guard<std::mutex> lock(location_mutex_);
        last_location_ = loc;
        has_location_fix_ = true;
    }

    broadcast(bridge::gnss_cmd::IND_LOCATION, &loc, sizeof(loc));
}

void GnssProxy::onStatusReceived(int32_t status) {
    bridge::BufferWriter writer;
    writer.write<int32_t>(status);
    broadcast(bridge::gnss_cmd::IND_STATUS, writer.buffer(), writer.size());
}

void GnssProxy::onSvStatusReceived(const std::vector<IGnssCallback::GnssSvInfo>& svInfo) {
    bridge::BufferWriter writer;
    writer.write<uint32_t>(static_cast<uint32_t>(svInfo.size()));
    for (const auto& sv : svInfo) {
        writer.write<int32_t>(sv.svid);
        writer.write<int32_t>(static_cast<int32_t>(sv.constellation));
        writer.write<float>(sv.cnrDbHz);
        writer.write<float>(sv.elevationDegrees);
        writer.write<float>(sv.azimuthDegrees);
        writer.write<float>(sv.carrierFrequencyHz);
        writer.write<int32_t>(sv.svFlag);
    }
    broadcast(bridge::gnss_cmd::IND_SV_STATUS, writer.buffer(), writer.size());
}

void GnssProxy::onNmeaReceived(int64_t timestamp_ms, const std::string& nmea) {
    bridge::BufferWriter writer;
    writer.write<int64_t>(timestamp_ms);
    writer.writeString(nmea);
    broadcast(bridge::gnss_cmd::IND_NMEA, writer.buffer(), writer.size());
}

void GnssProxy::onCapabilitiesReceived(int32_t capabilities) {
    bridge::BufferWriter writer;
    writer.write<int32_t>(capabilities);
    broadcast(bridge::gnss_cmd::IND_CAPABILITIES, writer.buffer(), writer.size());
}

void GnssProxy::clientLoop(int client_fd) {
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

void GnssProxy::handleClientMessage(int client_fd, const bridge::MessageHeader& hdr,
                                    const std::vector<uint8_t>& payload) {
    bridge::BufferReader reader(payload);

    switch (hdr.command_id) {
        case bridge::gnss_cmd::START:
            cmdStart(client_fd, hdr.sequence);
            break;
        case bridge::gnss_cmd::STOP:
            cmdStop(client_fd, hdr.sequence);
            break;
        case bridge::gnss_cmd::GET_LOCATION:
            cmdGetLocation(client_fd, hdr.sequence);
            break;
        case bridge::gnss_cmd::INJECT_TIME:
            cmdInjectTime(client_fd, hdr.sequence, reader);
            break;
        case bridge::gnss_cmd::INJECT_LOCATION:
            cmdInjectLocation(client_fd, hdr.sequence, reader);
            break;
        case bridge::gnss_cmd::DELETE_AIDING_DATA:
            cmdDeleteAidingData(client_fd, hdr.sequence, reader);
            break;
        case bridge::gnss_cmd::SET_POSITION_MODE:
            cmdSetPositionMode(client_fd, hdr.sequence, reader);
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

void GnssProxy::cmdStart(int client_fd, uint32_t seq) {
    ALOGI("Starting GNSS navigation...");
    bridge::BridgeStatus status = bridge::BridgeStatus::OK;

    if (gnss_) {
        auto st = gnss_->start();
        if (st.isOk()) {
            is_navigating_ = true;
        } else {
            ALOGE("IGnss::start() returned error %d", st.getServiceSpecificError());
            status = bridge::BridgeStatus::ERR_HAL_ERROR;
        }
    } else {
        status = bridge::BridgeStatus::ERR_NOT_CONNECTED;
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::gnss_cmd::START, seq,
        static_cast<int32_t>(status), 0
    };
    bridge::SocketHelper::sendMessage(client_fd, resp);
}

void GnssProxy::cmdStop(int client_fd, uint32_t seq) {
    ALOGI("Stopping GNSS navigation...");
    bridge::BridgeStatus status = bridge::BridgeStatus::OK;

    if (gnss_) {
        auto st = gnss_->stop();
        if (st.isOk()) {
            is_navigating_ = false;
        } else {
            status = bridge::BridgeStatus::ERR_HAL_ERROR;
        }
    }

    if (client_fd >= 0) {
        bridge::MessageHeader resp{
            bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
            static_cast<uint16_t>(bridge::MessageType::RESPONSE),
            bridge::gnss_cmd::STOP, seq,
            static_cast<int32_t>(status), 0
        };
        bridge::SocketHelper::sendMessage(client_fd, resp);
    }
}

void GnssProxy::cmdGetLocation(int client_fd, uint32_t seq) {
    std::lock_guard<std::mutex> lock(location_mutex_);

    if (!has_location_fix_) {
        bridge::MessageHeader resp{
            bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
            static_cast<uint16_t>(bridge::MessageType::RESPONSE),
            bridge::gnss_cmd::GET_LOCATION, seq,
            static_cast<int32_t>(bridge::BridgeStatus::ERR_GENERIC), 0
        };
        bridge::SocketHelper::sendMessage(client_fd, resp);
        return;
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::gnss_cmd::GET_LOCATION, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK),
        sizeof(LocationData)
    };
    bridge::SocketHelper::sendMessage(client_fd, resp, &last_location_);
}

void GnssProxy::cmdInjectTime(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    int64_t timeMs = 0;
    int64_t timeRefMs = 0;
    int32_t uncertaintyMs = 0;
    reader.read(timeMs);
    reader.read(timeRefMs);
    reader.read(uncertaintyMs);

    if (gnss_) {
        gnss_->injectTime(timeMs, timeRefMs, uncertaintyMs);
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::gnss_cmd::INJECT_TIME, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK), 0
    };
    bridge::SocketHelper::sendMessage(client_fd, resp);
}

void GnssProxy::cmdInjectLocation(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    GnssLocation loc;
    reader.read(loc.latitudeDegrees);
    reader.read(loc.longitudeDegrees);
    reader.read(loc.horizontalAccuracyMeters);

    if (gnss_) {
        gnss_->injectBestLocation(loc);
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::gnss_cmd::INJECT_LOCATION, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK), 0
    };
    bridge::SocketHelper::sendMessage(client_fd, resp);
}

void GnssProxy::cmdDeleteAidingData(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    int32_t aidingFlags = 0;
    reader.read(aidingFlags);

    if (gnss_) {
        gnss_->deleteAidingData(static_cast<IGnss::GnssAidingData>(aidingFlags));
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::gnss_cmd::DELETE_AIDING_DATA, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK), 0
    };
    bridge::SocketHelper::sendMessage(client_fd, resp);
}

void GnssProxy::cmdSetPositionMode(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    IGnss::GnssPositionMode mode{};
    reader.read(mode.mode);
    reader.read(mode.recurrence);
    reader.read(mode.minIntervalMs);
    reader.read(mode.preferredAccuracyMeters);
    reader.read(mode.preferredTimeMs);

    if (gnss_) {
        gnss_->setPositionMode(mode);
    }

    bridge::MessageHeader resp{
        bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
        static_cast<uint16_t>(bridge::MessageType::RESPONSE),
        bridge::gnss_cmd::SET_POSITION_MODE, seq,
        static_cast<int32_t>(bridge::BridgeStatus::OK), 0
    };
    bridge::SocketHelper::sendMessage(client_fd, resp);
}

} // namespace gnss
} // namespace ubtouch
