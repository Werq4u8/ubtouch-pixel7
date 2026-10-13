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

#ifndef UBTIAL_RADIO_PROXY_H_
#define UBTIAL_RADIO_PROXY_H_

#include "bridge_protocol.h"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/binder_auto_utils.h>

#include <aidl/android/hardware/radio/voice/IRadioVoice.h>
#include <aidl/android/hardware/radio/voice/BnRadioVoiceResponse.h>
#include <aidl/android/hardware/radio/voice/BnRadioVoiceIndication.h>

#include <aidl/android/hardware/radio/network/IRadioNetwork.h>
#include <aidl/android/hardware/radio/network/BnRadioNetworkResponse.h>
#include <aidl/android/hardware/radio/network/BnRadioNetworkIndication.h>

#include <aidl/android/hardware/radio/sim/IRadioSim.h>
#include <aidl/android/hardware/radio/sim/BnRadioSimResponse.h>
#include <aidl/android/hardware/radio/sim/BnRadioSimIndication.h>

#include <aidl/android/hardware/radio/data/IRadioData.h>
#include <aidl/android/hardware/radio/data/BnRadioDataResponse.h>
#include <aidl/android/hardware/radio/data/BnRadioDataIndication.h>

#include <aidl/android/hardware/radio/modem/IRadioModem.h>
#include <aidl/android/hardware/radio/modem/BnRadioModemResponse.h>
#include <aidl/android/hardware/radio/modem/BnRadioModemIndication.h>

#include <aidl/android/hardware/radio/messaging/IRadioMessaging.h>
#include <aidl/android/hardware/radio/messaging/BnRadioMessagingResponse.h>
#include <aidl/android/hardware/radio/messaging/BnRadioMessagingIndication.h>

#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

namespace ubtouch {
namespace radio {

/**
 * Tracks an in-flight transaction requested by an Ubuntu Touch client.
 */
struct PendingTransaction {
    int client_fd;
    uint32_t client_seq;
    uint32_t command_id;
    std::chrono::steady_clock::time_point timestamp;
};

class RadioProxy {
public:
    RadioProxy(const std::string& socket_path, const std::string& slot_name = "slot1");
    ~RadioProxy();

    // Lifecycle
    bool start();
    void stop();
    void run();

    // Unix domain socket request handlers
    void handleClientMessage(int client_fd, const bridge::MessageHeader& hdr,
                             const std::vector<uint8_t>& payload);

    // Callbacks invoked by AIDL Response implementations
    void notifyVoiceResponse(int32_t serial, bridge::BridgeStatus status,
                             const std::vector<uint8_t>& payload);
    void notifyNetworkResponse(int32_t serial, bridge::BridgeStatus status,
                               const std::vector<uint8_t>& payload);
    void notifySimResponse(int32_t serial, bridge::BridgeStatus status,
                           const std::vector<uint8_t>& payload);
    void notifyDataResponse(int32_t serial, bridge::BridgeStatus status,
                            const std::vector<uint8_t>& payload);
    void notifyModemResponse(int32_t serial, bridge::BridgeStatus status,
                             const std::vector<uint8_t>& payload);
    void notifyMessagingResponse(int32_t serial, bridge::BridgeStatus status,
                                 const std::vector<uint8_t>& payload);

    // Callbacks invoked by AIDL Indication implementations
    void broadcastIndication(uint32_t ind_id, const std::vector<uint8_t>& payload);

    // Death callback trigger
    void onBinderDied(const std::string& service_name);

private:
    // AIDL connection management
    bool connectToAidlServices();
    void disconnectAidlServices();
    void registerDeathRecipients();

    // Serial allocator for AIDL calls
    int32_t allocateSerial(int client_fd, uint32_t client_seq, uint32_t command_id);
    bool findAndRemovePending(int32_t serial, PendingTransaction& out_trans);

    // Client connection management
    void addClient(int fd);
    void removeClient(int fd);
    void clientLoop(int client_fd);

    // Specific command execution
    void cmdDial(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdHangup(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdAnswer(int client_fd, uint32_t seq);
    void cmdGetSignalStrength(int client_fd, uint32_t seq);
    void cmdGetOperator(int client_fd, uint32_t seq);
    void cmdGetSimStatus(int client_fd, uint32_t seq);
    void cmdSendSms(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdSetupDataCall(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdDeactivateDataCall(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdGetNetworkType(int client_fd, uint32_t seq);
    void cmdSetNetworkType(int client_fd, uint32_t seq, bridge::BufferReader& reader);
    void cmdSetRadioPower(int client_fd, uint32_t seq, bridge::BufferReader& reader);

    std::string socket_path_;
    std::string slot_name_;
    std::atomic<bool> running_{false};
    int server_fd_{-1};

    // AIDL Interfaces
    std::shared_ptr<aidl::android::hardware::radio::voice::IRadioVoice> radio_voice_;
    std::shared_ptr<aidl::android::hardware::radio::network::IRadioNetwork> radio_network_;
    std::shared_ptr<aidl::android::hardware::radio::sim::IRadioSim> radio_sim_;
    std::shared_ptr<aidl::android::hardware::radio::data::IRadioData> radio_data_;
    std::shared_ptr<aidl::android::hardware::radio::modem::IRadioModem> radio_modem_;
    std::shared_ptr<aidl::android::hardware::radio::messaging::IRadioMessaging> radio_messaging_;

    // Death recipient
    ndk::ScopedAIBinder_DeathRecipient death_recipient_;

    // Client tracking
    std::mutex clients_mutex_;
    std::vector<int> client_fds_;

    // Pending transactions map
    std::mutex pending_mutex_;
    std::atomic<int32_t> next_serial_{1};
    std::unordered_map<int32_t, PendingTransaction> pending_transactions_;

    // Threads
    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
};

} // namespace radio
} // namespace ubtouch

#endif // UBTIAL_RADIO_PROXY_H_
