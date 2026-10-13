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

#define LOG_TAG "ubtouch-radio-bridge"

#include "radio_proxy.h"
#include <android/binder_ibinder.h>
#include <algorithm>
#include <poll.h>

using namespace aidl::android::hardware::radio;

namespace ubtouch {
namespace radio {

static void binderDeathCallback(void* cookie) {
    if (!cookie) return;
    auto* proxy = static_cast<RadioProxy*>(cookie);
    ALOGW("Radio HAL binder died! Scheduling reconnection...");
    proxy->onBinderDied("RadioHAL");
}

// -----------------------------------------------------------------------------
// AIDL Voice Response & Indication Callbacks
// -----------------------------------------------------------------------------

class RadioVoiceResponseImpl : public voice::BnRadioVoiceResponse {
public:
    explicit RadioVoiceResponseImpl(RadioProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus dialResponse(const RadioResponseInfo& info) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(static_cast<int32_t>(info.error));
        proxy_->notifyVoiceResponse(info.serial,
            info.error == RadioError::NONE ? bridge::BridgeStatus::OK : bridge::BridgeStatus::ERR_HAL_ERROR,
            writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus hangupResponse(const RadioResponseInfo& info) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(static_cast<int32_t>(info.error));
        proxy_->notifyVoiceResponse(info.serial,
            info.error == RadioError::NONE ? bridge::BridgeStatus::OK : bridge::BridgeStatus::ERR_HAL_ERROR,
            writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus acceptCallResponse(const RadioResponseInfo& info) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(static_cast<int32_t>(info.error));
        proxy_->notifyVoiceResponse(info.serial,
            info.error == RadioError::NONE ? bridge::BridgeStatus::OK : bridge::BridgeStatus::ERR_HAL_ERROR,
            writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus getCurrentCallsResponse(const RadioResponseInfo& info,
                                              const std::vector<voice::Call>& calls) override {
        bridge::BufferWriter writer;
        writer.write<uint32_t>(static_cast<uint32_t>(calls.size()));
        for (const auto& call : calls) {
            writer.write<int32_t>(static_cast<int32_t>(call.state));
            writer.write<int32_t>(call.index);
            writer.write<int32_t>(call.toa);
            writer.write<uint8_t>(call.isMpty ? 1 : 0);
            writer.write<uint8_t>(call.isMT ? 1 : 0);
            writer.write<uint8_t>(call.isVoice ? 1 : 0);
            writer.writeString(call.number);
            writer.writeString(call.name);
        }
        proxy_->notifyVoiceResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus emergencyDialResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus explicitCallTransferResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getCallForwardStatusResponse(const RadioResponseInfo& /*info*/, const std::vector<voice::CallForwardInfo>& /*callForwardInfos*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getCallWaitingResponse(const RadioResponseInfo& /*info*/, bool /*enable*/, int32_t /*serviceClass*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getClipResponse(const RadioResponseInfo& /*info*/, voice::ClipStatus /*status*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getClirResponse(const RadioResponseInfo& /*info*/, int32_t /*n*/, int32_t /*m*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getMuteResponse(const RadioResponseInfo& /*info*/, bool /*enable*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getPreferredVoicePrivacyResponse(const RadioResponseInfo& /*info*/, bool /*enable*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getTtyModeResponse(const RadioResponseInfo& /*info*/, voice::TtyMode /*mode*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus handleStkCallSetupRequestFromSimResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus hangupConnectionResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus hangupWaitingOrBackgroundResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus hangupForegroundResumeBackgroundResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus rejectCallResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus sendBurstDtmfResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus sendCdmaFeatureCodeResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus sendDtmfResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus sendUssdResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus cancelPendingUssdResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus separateConnectionResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus setCallForwardResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus setCallWaitingResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus setClirResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus setMuteResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus setPreferredVoicePrivacyResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus setTtyModeResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus startDtmfResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus stopDtmfResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus switchWaitingOrHoldingAndActiveResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus conferenceResponse(const RadioResponseInfo& /*info*/) override {
        return ndk::ScopedAStatus::ok();
    }

private:
    RadioProxy* proxy_;
};

class RadioVoiceIndicationImpl : public voice::BnRadioVoiceIndication {
public:
    explicit RadioVoiceIndicationImpl(RadioProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus callStateChanged(RadioIndicationType /*type*/) override {
        bridge::BufferWriter writer;
        writer.write<uint32_t>(1); // Indication flag
        proxy_->broadcastIndication(bridge::radio_cmd::IND_CALL_STATE_CHANGED, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus callRing(RadioIndicationType /*type*/, bool /*isGsm*/, const voice::CdmaSignalInfoRecord& /*record*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus cdmaCallWaiting(RadioIndicationType /*type*/, const voice::CdmaCallWaiting& /*callWaitingRecord*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus cdmaInfoRec(RadioIndicationType /*type*/, const std::vector<voice::CdmaInformationRecord>& /*records*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus cdmaOtaProvisionStatus(RadioIndicationType /*type*/, voice::CdmaOtaProvisionStatus /*status*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus currentEmergencyNumberList(RadioIndicationType /*type*/, const std::vector<voice::EmergencyNumber>& /*emergencyNumberList*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus enterEmergencyCallbackMode(RadioIndicationType /*type*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus exitEmergencyCallbackMode(RadioIndicationType /*type*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus indicateRingbackTone(RadioIndicationType /*type*/, bool /*start*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus onSupplementaryServiceIndication(RadioIndicationType /*type*/, const voice::StkCcUnsolSsResult& /*ss*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus onUssd(RadioIndicationType /*type*/, voice::UssdModeType modeType, const std::string& msg) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(static_cast<int32_t>(modeType));
        writer.writeString(msg);
        proxy_->broadcastIndication(bridge::radio_cmd::IND_USSD_RECEIVED, writer.data());
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus srvccStateNotify(RadioIndicationType /*type*/, voice::SrvccState /*state*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus stkCallControlAlphaInfo(RadioIndicationType /*type*/, const std::string& /*alpha*/) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus stkCallSetup(RadioIndicationType /*type*/, int64_t /*timeout*/) override {
        return ndk::ScopedAStatus::ok();
    }

private:
    RadioProxy* proxy_;
};

// -----------------------------------------------------------------------------
// AIDL Network Response & Indication Callbacks
// -----------------------------------------------------------------------------

class RadioNetworkResponseImpl : public network::BnRadioNetworkResponse {
public:
    explicit RadioNetworkResponseImpl(RadioProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus getSignalStrengthResponse(const RadioResponseInfo& info,
                                                const network::SignalStrength& sig) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(sig.gsm.signalStrength);
        writer.write<int32_t>(sig.gsm.bitErrorRate);
        writer.write<int32_t>(sig.lte.signalStrength);
        writer.write<int32_t>(sig.lte.rsrp);
        writer.write<int32_t>(sig.lte.rsrq);
        writer.write<int32_t>(sig.lte.rssnr);
        writer.write<int32_t>(sig.nr.ssRsrp);
        writer.write<int32_t>(sig.nr.ssRsrq);
        writer.write<int32_t>(sig.nr.ssSinr);
        proxy_->notifyNetworkResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus getOperatorResponse(const RadioResponseInfo& info,
                                          const std::string& longName,
                                          const std::string& shortName,
                                          const std::string& numeric) override {
        bridge::BufferWriter writer;
        writer.writeString(longName);
        writer.writeString(shortName);
        writer.writeString(numeric);
        proxy_->notifyNetworkResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus getAllowedNetworkTypesBitmapResponse(const RadioResponseInfo& info,
                                                           int32_t networkTypeBitmap) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(networkTypeBitmap);
        proxy_->notifyNetworkResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus setAllowedNetworkTypesBitmapResponse(const RadioResponseInfo& info) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(static_cast<int32_t>(info.error));
        proxy_->notifyNetworkResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    // Default implementations for remaining interface methods
    ndk::ScopedAStatus getAvailableBandModesResponse(const RadioResponseInfo& /*info*/, const std::vector<network::RadioBandMode>& /*bandModes*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getAvailableNetworksResponse(const RadioResponseInfo& /*info*/, const std::vector<network::OperatorInfo>& /*networkInfos*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getBarringInfoResponse(const RadioResponseInfo& /*info*/, const network::CellIdentity& /*cellIdentity*/, const std::vector<network::BarringInfo>& /*barringInfos*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getCdmaRoamingPreferenceResponse(const RadioResponseInfo& /*info*/, network::CdmaRoamingType /*type*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getCellInfoListResponse(const RadioResponseInfo& /*info*/, const std::vector<network::CellInfo>& /*cellInfo*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getDataRegistrationStateResponse(const RadioResponseInfo& /*info*/, const network::RegStateResult& /*dataRegResponse*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getImsRegistrationStateResponse(const RadioResponseInfo& /*info*/, bool /*isRegister*/, network::RadioTechnologyFamily /*ratFamily*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getNetworkSelectionModeResponse(const RadioResponseInfo& /*info*/, bool /*manual*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getSystemSelectionChannelsResponse(const RadioResponseInfo& /*info*/, const std::vector<network::RadioAccessSpecifier>& /*specifiers*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getVoiceRadioTechnologyResponse(const RadioResponseInfo& /*info*/, network::RadioTechnology /*rat*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getVoiceRegistrationStateResponse(const RadioResponseInfo& /*info*/, const network::RegStateResult& /*voiceRegResponse*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus isNrDualConnectivityEnabledResponse(const RadioResponseInfo& /*info*/, bool /*isEnabled*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setBandModeResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setBarringPasswordResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setCdmaRoamingPreferenceResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setCellInfoListRateResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setIndicationFilterResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setLinkCapacityReportingCriteriaResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setLocationUpdatesResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setNetworkSelectionModeAutomaticResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setNetworkSelectionModeManualResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setNrDualConnectivityStateResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setSignalStrengthReportingCriteriaResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setSuppServiceNotificationsResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setSystemSelectionChannelsResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus startNetworkScanResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus stopNetworkScanResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus supplyNetworkDepersonalizationResponse(const RadioResponseInfo& /*info*/, int32_t /*remainingRetries*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setUsageSettingResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getUsageSettingResponse(const RadioResponseInfo& /*info*/, network::UsageSetting /*usageSetting*/) override { return ndk::ScopedAStatus::ok(); }

private:
    RadioProxy* proxy_;
};

class RadioNetworkIndicationImpl : public network::BnRadioNetworkIndication {
public:
    explicit RadioNetworkIndicationImpl(RadioProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus currentSignalStrength(RadioIndicationType /*type*/,
                                            const network::SignalStrength& sig) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(sig.gsm.signalStrength);
        writer.write<int32_t>(sig.lte.signalStrength);
        writer.write<int32_t>(sig.lte.rsrp);
        writer.write<int32_t>(sig.nr.ssRsrp);
        proxy_->broadcastIndication(bridge::radio_cmd::IND_SIGNAL_STRENGTH, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus networkStateChanged(RadioIndicationType /*type*/) override {
        bridge::BufferWriter writer;
        writer.write<uint32_t>(1);
        proxy_->broadcastIndication(bridge::radio_cmd::IND_NETWORK_STATE_CHANGED, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus barringInfoChanged(RadioIndicationType /*type*/, const network::CellIdentity& /*cellIdentity*/, const std::vector<network::BarringInfo>& /*barringInfos*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus cdmaPrlChanged(RadioIndicationType /*type*/, int32_t /*version*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus cellInfoList(RadioIndicationType /*type*/, const std::vector<network::CellInfo>& /*records*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus currentEmergencyNumberList(RadioIndicationType /*type*/, const std::vector<voice::EmergencyNumber>& /*emergencyNumberList*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus currentLinkCapacityEstimate(RadioIndicationType /*type*/, const network::LinkCapacityEstimate& /*lce*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus currentPhysicalChannelConfigs(RadioIndicationType /*type*/, const std::vector<network::PhysicalChannelConfig>& /*configs*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus imsNetworkStateChanged(RadioIndicationType /*type*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus networkScanResult(RadioIndicationType /*type*/, const network::NetworkScanResult& /*result*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus nitzTimeReceived(RadioIndicationType /*type*/, const std::string& /*nitzTime*/, int64_t /*receivedTimeMs*/, int64_t /*ageMs*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus registrationFailed(RadioIndicationType /*type*/, const network::CellIdentity& /*cellIdentity*/, const std::string& /*chosenPlmn*/, int32_t /*domain*/, int32_t /*causeCode*/, int32_t /*additionalCauseCode*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus restrictedStateChanged(RadioIndicationType /*type*/, network::PhoneRestrictedState /*state*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus suppSvcNotify(RadioIndicationType /*type*/, const network::SuppSvcNotification& /*suppSvc*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus voiceRadioTechChanged(RadioIndicationType /*type*/, network::RadioTechnology /*rat*/) override { return ndk::ScopedAStatus::ok(); }

private:
    RadioProxy* proxy_;
};

// -----------------------------------------------------------------------------
// AIDL SIM Response & Indication Callbacks
// -----------------------------------------------------------------------------

class RadioSimResponseImpl : public sim::BnRadioSimResponse {
public:
    explicit RadioSimResponseImpl(RadioProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus getIccCardStatusResponse(const RadioResponseInfo& info,
                                               const sim::CardStatus& cardStatus) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(static_cast<int32_t>(cardStatus.cardState));
        writer.write<int32_t>(static_cast<int32_t>(cardStatus.universalPinState));
        writer.write<int32_t>(cardStatus.gsmUmtsSubscriptionAppIndex);
        writer.write<int32_t>(cardStatus.imsSubscriptionAppIndex);
        writer.write<uint32_t>(static_cast<uint32_t>(cardStatus.applications.size()));
        for (const auto& app : cardStatus.applications) {
            writer.write<int32_t>(static_cast<int32_t>(app.appType));
            writer.write<int32_t>(static_cast<int32_t>(app.appState));
            writer.writeString(app.aid);
            writer.writeString(app.appLabel);
        }
        writer.writeString(cardStatus.atr);
        writer.writeString(cardStatus.iccid);
        proxy_->notifySimResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus areUiccApplicationsEnabledResponse(const RadioResponseInfo& /*info*/, bool /*enabled*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus changeIccPin2ForAppResponse(const RadioResponseInfo& /*info*/, int32_t /*remainingRetries*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus changeIccPinForAppResponse(const RadioResponseInfo& /*info*/, int32_t /*remainingRetries*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus enableUiccApplicationsResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getAllowedCarriersResponse(const RadioResponseInfo& /*info*/, const sim::CarrierRestrictions& /*carrierRestrictions*/, sim::SimLockMultiSimPolicy /*multiSimPolicy*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getCdmaSubscriptionResponse(const RadioResponseInfo& /*info*/, const std::string& /*mdn*/, const std::string& /*hSid*/, const std::string& /*hNid*/, const std::string& /*min*/, const std::string& /*prl*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getCdmaSubscriptionSourceResponse(const RadioResponseInfo& /*info*/, sim::CdmaSubscriptionSource /*source*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getFacilityLockForAppResponse(const RadioResponseInfo& /*info*/, int32_t /*response*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getImsiForAppResponse(const RadioResponseInfo& info, const std::string& imsi) override {
        bridge::BufferWriter writer;
        writer.writeString(imsi);
        proxy_->notifySimResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getSimPhonebookCapacityResponse(const RadioResponseInfo& /*info*/, const sim::PhonebookCapacity& /*pbCapacity*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getSimPhonebookRecordsResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus iccCloseLogicalChannelResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus iccCloseLogicalChannelWithSessionInfoResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus iccIoForAppResponse(const RadioResponseInfo& /*info*/, const sim::IccIoResult& /*iccIo*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus iccOpenLogicalChannelResponse(const RadioResponseInfo& /*info*/, int32_t /*channelId*/, const std::vector<uint8_t>& /*selectResponse*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus iccTransmitApduBasicChannelResponse(const RadioResponseInfo& /*info*/, const sim::IccIoResult& /*iccIo*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus iccTransmitApduLogicalChannelResponse(const RadioResponseInfo& /*info*/, const sim::IccIoResult& /*iccIo*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus reportStkServiceIsRunningResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus requestIccSimAuthenticationResponse(const RadioResponseInfo& /*info*/, const sim::IccIoResult& /*iccIo*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus sendEnvelopeResponse(const RadioResponseInfo& /*info*/, const std::string& /*commandResponse*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus sendEnvelopeWithStatusResponse(const RadioResponseInfo& /*info*/, const sim::IccIoResult& /*iccIo*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus sendTerminalResponseToSimResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setAllowedCarriersResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setCarrierInfoForImsiEncryptionResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setCdmaSubscriptionSourceResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setFacilityLockForAppResponse(const RadioResponseInfo& /*info*/, int32_t /*retry*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setUiccSubscriptionResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus supplyIccPin2ForAppResponse(const RadioResponseInfo& /*info*/, int32_t /*remainingRetries*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus supplyIccPinForAppResponse(const RadioResponseInfo& /*info*/, int32_t /*remainingRetries*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus supplyIccPuk2ForAppResponse(const RadioResponseInfo& /*info*/, int32_t /*remainingRetries*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus supplyIccPukForAppResponse(const RadioResponseInfo& /*info*/, int32_t /*remainingRetries*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus supplySimDepersonalizationResponse(const RadioResponseInfo& /*info*/, sim::PersoSubstate /*persoType*/, int32_t /*remainingRetries*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus updateSimPhonebookRecordsResponse(const RadioResponseInfo& /*info*/, int32_t /*updatedRecordIndex*/) override { return ndk::ScopedAStatus::ok(); }

private:
    RadioProxy* proxy_;
};

class RadioSimIndicationImpl : public sim::BnRadioSimIndication {
public:
    explicit RadioSimIndicationImpl(RadioProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus simStatusChanged(RadioIndicationType /*type*/) override {
        bridge::BufferWriter writer;
        writer.write<uint32_t>(1);
        proxy_->broadcastIndication(bridge::radio_cmd::IND_SIM_STATUS_CHANGED, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus carrierInfoForImsiEncryption(RadioIndicationType /*type*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus cdmaSubscriptionSourceChanged(RadioIndicationType /*type*/, sim::CdmaSubscriptionSource /*source*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus simPhonebookChanged(RadioIndicationType /*type*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus simPhonebookRecordsReceived(RadioIndicationType /*type*/, sim::PbReceivedStatus /*status*/, const std::vector<sim::PhonebookRecordInfo>& /*records*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus simRefresh(RadioIndicationType /*type*/, const sim::SimRefreshResult& /*refreshResult*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus stkEventNotify(RadioIndicationType /*type*/, const std::string& /*cmd*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus stkProactiveCommand(RadioIndicationType /*type*/, const std::string& /*cmd*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus stkSessionEnd(RadioIndicationType /*type*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus subscriptionStatusChanged(RadioIndicationType /*type*/, bool /*activate*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus uiccApplicationsEnablementChanged(RadioIndicationType /*type*/, bool /*enabled*/) override { return ndk::ScopedAStatus::ok(); }

private:
    RadioProxy* proxy_;
};

// -----------------------------------------------------------------------------
// AIDL Data Response & Indication Callbacks
// -----------------------------------------------------------------------------

class RadioDataResponseImpl : public data::BnRadioDataResponse {
public:
    explicit RadioDataResponseImpl(RadioProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus setupDataCallResponse(const RadioResponseInfo& info,
                                            const data::SetupDataCallResult& dcResult) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(static_cast<int32_t>(dcResult.cause));
        writer.write<int32_t>(dcResult.suggestedRetryTimeMs);
        writer.write<int32_t>(dcResult.cid);
        writer.write<int32_t>(dcResult.active);
        writer.write<int32_t>(static_cast<int32_t>(dcResult.type));
        writer.writeString(dcResult.ifname);
        writer.write<uint32_t>(static_cast<uint32_t>(dcResult.addresses.size()));
        for (const auto& addr : dcResult.addresses) {
            writer.writeString(addr.address);
        }
        writer.write<uint32_t>(static_cast<uint32_t>(dcResult.dnses.size()));
        for (const auto& dns : dcResult.dnses) {
            writer.writeString(dns);
        }
        writer.write<uint32_t>(static_cast<uint32_t>(dcResult.gateways.size()));
        for (const auto& gw : dcResult.gateways) {
            writer.writeString(gw);
        }
        proxy_->notifyDataResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus deactivateDataCallResponse(const RadioResponseInfo& info) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(static_cast<int32_t>(info.error));
        proxy_->notifyDataResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus getDataCallListResponse(const RadioResponseInfo& info,
                                              const std::vector<data::SetupDataCallResult>& dcList) override {
        bridge::BufferWriter writer;
        writer.write<uint32_t>(static_cast<uint32_t>(dcList.size()));
        for (const auto& dcResult : dcList) {
            writer.write<int32_t>(dcResult.cid);
            writer.write<int32_t>(dcResult.active);
            writer.writeString(dcResult.ifname);
        }
        proxy_->notifyDataResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus allocatePduSessionIdResponse(const RadioResponseInfo& /*info*/, int32_t /*id*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus cancelHandoverResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getDataRegistrationStateResponse(const RadioResponseInfo& /*info*/, const data::DataRegStateResult& /*dataRegResponse*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getSlicingConfigResponse(const RadioResponseInfo& /*info*/, const data::SlicingConfig& /*slicingConfig*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus releasePduSessionIdResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setDataAllowedResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setDataProfileResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setDataThrottlingResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setInitialAttachApnResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus startHandoverResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus startKeepaliveResponse(const RadioResponseInfo& /*info*/, const data::KeepaliveStatus& /*status*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus stopKeepaliveResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }

private:
    RadioProxy* proxy_;
};

class RadioDataIndicationImpl : public data::BnRadioDataIndication {
public:
    explicit RadioDataIndicationImpl(RadioProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus dataCallListChanged(RadioIndicationType /*type*/,
                                          const std::vector<data::SetupDataCallResult>& dcList) override {
        bridge::BufferWriter writer;
        writer.write<uint32_t>(static_cast<uint32_t>(dcList.size()));
        for (const auto& dcResult : dcList) {
            writer.write<int32_t>(dcResult.cid);
            writer.write<int32_t>(dcResult.active);
            writer.writeString(dcResult.ifname);
        }
        proxy_->broadcastIndication(bridge::radio_cmd::IND_DATA_CALL_LIST_CHANGED, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus currentDataCallList(RadioIndicationType /*type*/, const std::vector<data::SetupDataCallResult>& /*dcList*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus keepaliveStatus(RadioIndicationType /*type*/, const data::KeepaliveStatus& /*status*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus pduSessionIdAllocation(RadioIndicationType /*type*/, int32_t /*pduSessionId*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus slicingConfigChanged(RadioIndicationType /*type*/, const data::SlicingConfig& /*slicingConfig*/) override { return ndk::ScopedAStatus::ok(); }

private:
    RadioProxy* proxy_;
};

// -----------------------------------------------------------------------------
// AIDL Modem Response & Indication Callbacks
// -----------------------------------------------------------------------------

class RadioModemResponseImpl : public modem::BnRadioModemResponse {
public:
    explicit RadioModemResponseImpl(RadioProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus setRadioPowerResponse(const RadioResponseInfo& info) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(static_cast<int32_t>(info.error));
        proxy_->notifyModemResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus getBasebandVersionResponse(const RadioResponseInfo& info,
                                                 const std::string& version) override {
        bridge::BufferWriter writer;
        writer.writeString(version);
        proxy_->notifyModemResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus enableModemResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getDeviceIdentityResponse(const RadioResponseInfo& /*info*/, const std::string& /*imei*/, const std::string& /*imeisv*/, const std::string& /*esn*/, const std::string& /*meid*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getHardwareConfigResponse(const RadioResponseInfo& /*info*/, const std::vector<modem::HardwareConfig>& /*config*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getModemActivityInfoResponse(const RadioResponseInfo& /*info*/, const modem::ActivityStatsInfo& /*activityInfo*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getModemStackStatusResponse(const RadioResponseInfo& /*info*/, bool /*isEnabled*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getRadioCapabilityResponse(const RadioResponseInfo& /*info*/, const modem::RadioCapability& /*rc*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus nvReadItemResponse(const RadioResponseInfo& /*info*/, const std::string& /*result*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus nvResetConfigResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus nvWriteCdmaPrlResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus nvWriteItemResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus requestShutdownResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus sendDeviceStateResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setRadioCapabilityResponse(const RadioResponseInfo& /*info*/, const modem::RadioCapability& /*rc*/) override { return ndk::ScopedAStatus::ok(); }

private:
    RadioProxy* proxy_;
};

class RadioModemIndicationImpl : public modem::BnRadioModemIndication {
public:
    explicit RadioModemIndicationImpl(RadioProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus radioStateChanged(RadioIndicationType /*type*/, modem::RadioState radioState) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(static_cast<int32_t>(radioState));
        proxy_->broadcastIndication(bridge::radio_cmd::IND_RADIO_STATE_CHANGED, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus hardwareConfigChanged(RadioIndicationType /*type*/, const std::vector<modem::HardwareConfig>& /*configs*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus modemReset(RadioIndicationType /*type*/, const std::string& /*reason*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus radioCapabilityIndication(RadioIndicationType /*type*/, const modem::RadioCapability& /*rc*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus rilConnected(RadioIndicationType /*type*/) override { return ndk::ScopedAStatus::ok(); }

private:
    RadioProxy* proxy_;
};

// -----------------------------------------------------------------------------
// AIDL Messaging Response & Indication Callbacks
// -----------------------------------------------------------------------------

class RadioMessagingResponseImpl : public messaging::BnRadioMessagingResponse {
public:
    explicit RadioMessagingResponseImpl(RadioProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus sendSmsResponse(const RadioResponseInfo& info,
                                      const messaging::SendSmsResult& sms) override {
        bridge::BufferWriter writer;
        writer.write<int32_t>(sms.messageRef);
        writer.writeString(sms.ackPdu);
        writer.write<int32_t>(sms.errorCode);
        proxy_->notifyMessagingResponse(info.serial, bridge::BridgeStatus::OK, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus acknowledgeIncomingGsmSmsWithPduResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus acknowledgeLastIncomingCdmaSmsResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus acknowledgeLastIncomingGsmSmsResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus deleteSmsOnRuimResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus deleteSmsOnSimResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getCdmaBroadcastConfigResponse(const RadioResponseInfo& /*info*/, const std::vector<messaging::CdmaBroadcastSmsConfigInfo>& /*configs*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getGsmBroadcastConfigResponse(const RadioResponseInfo& /*info*/, const std::vector<messaging::GsmBroadcastSmsConfigInfo>& /*configs*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getSmscAddressResponse(const RadioResponseInfo& /*info*/, const std::string& /*smsc*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus reportSmsMemoryStatusResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus sendCdmaSmsExpectMoreResponse(const RadioResponseInfo& /*info*/, const messaging::SendSmsResult& /*sms*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus sendCdmaSmsResponse(const RadioResponseInfo& /*info*/, const messaging::SendSmsResult& /*sms*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus sendImsSmsResponse(const RadioResponseInfo& /*info*/, const messaging::SendSmsResult& /*sms*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus sendSmsExpectMoreResponse(const RadioResponseInfo& /*info*/, const messaging::SendSmsResult& /*sms*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setCdmaBroadcastActivationResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setCdmaBroadcastConfigResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setGsmBroadcastActivationResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setGsmBroadcastConfigResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setSmscAddressResponse(const RadioResponseInfo& /*info*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus writeSmsToRuimResponse(const RadioResponseInfo& /*info*/, int32_t /*index*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus writeSmsToSimResponse(const RadioResponseInfo& /*info*/, int32_t /*index*/) override { return ndk::ScopedAStatus::ok(); }

private:
    RadioProxy* proxy_;
};

class RadioMessagingIndicationImpl : public messaging::BnRadioMessagingIndication {
public:
    explicit RadioMessagingIndicationImpl(RadioProxy* proxy) : proxy_(proxy) {}

    ndk::ScopedAStatus newSms(RadioIndicationType /*type*/, const std::vector<uint8_t>& pdu) override {
        bridge::BufferWriter writer;
        writer.write<uint32_t>(static_cast<uint32_t>(pdu.size()));
        writer.writeBytes(pdu.data(), pdu.size());
        proxy_->broadcastIndication(bridge::radio_cmd::IND_NEW_SMS, writer.data());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus cdmaNewSms(RadioIndicationType /*type*/, const messaging::CdmaSmsMessage& /*msg*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus cdmaRuimSmsStorageFull(RadioIndicationType /*type*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus newBroadcastSms(RadioIndicationType /*type*/, const std::vector<uint8_t>& /*data*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus newSmsOnSim(RadioIndicationType /*type*/, int32_t /*recordIndex*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus newSmsStatusReport(RadioIndicationType /*type*/, const std::vector<uint8_t>& /*pdu*/) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus simSmsStorageFull(RadioIndicationType /*type*/) override { return ndk::ScopedAStatus::ok(); }

private:
    RadioProxy* proxy_;
};

// -----------------------------------------------------------------------------
// RadioProxy Implementation
// -----------------------------------------------------------------------------

RadioProxy::RadioProxy(const std::string& socket_path, const std::string& slot_name)
    : socket_path_(socket_path), slot_name_(slot_name) {
    death_recipient_ = ndk::ScopedAIBinder_DeathRecipient(
        AIBinder_DeathRecipient_new(binderDeathCallback));
}

RadioProxy::~RadioProxy() {
    stop();
}

bool RadioProxy::start() {
    running_ = true;

    if (!connectToAidlServices()) {
        ALOGE("Initial connection to AIDL radio services failed. Will retry asynchronously.");
    }

    server_fd_ = bridge::SocketHelper::createServerSocket(socket_path_.c_str());
    if (server_fd_ < 0) {
        ALOGE("Failed to create server socket at %s", socket_path_.c_str());
        return false;
    }

    accept_thread_ = std::thread(&RadioProxy::run, this);
    return true;
}

void RadioProxy::stop() {
    if (!running_.exchange(false)) return;

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
        if (th.joinable()) {
            th.join();
        }
    }
    client_threads_.clear();

    disconnectAidlServices();
}

void RadioProxy::run() {
    ALOGI("RadioProxy accept loop started on %s", socket_path_.c_str());
    while (running_) {
        int client_fd = bridge::SocketHelper::acceptClient(server_fd_);
        if (client_fd < 0) {
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        ALOGI("Ubuntu Touch client connected (fd=%d)", client_fd);
        addClient(client_fd);
        client_threads_.emplace_back(&RadioProxy::clientLoop, this, client_fd);
    }
}

void RadioProxy::addClient(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    client_fds_.push_back(fd);
}

void RadioProxy::removeClient(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = std::find(client_fds_.begin(), client_fds_.end(), fd);
    if (it != client_fds_.end()) {
        close(fd);
        client_fds_.erase(it);
        ALOGI("Ubuntu Touch client disconnected (fd=%d)", fd);
    }
}

bool RadioProxy::connectToAidlServices() {
    std::string voiceName = std::string(voice::IRadioVoice::descriptor) + "/" + slot_name_;
    std::string networkName = std::string(network::IRadioNetwork::descriptor) + "/" + slot_name_;
    std::string simName = std::string(sim::IRadioSim::descriptor) + "/" + slot_name_;
    std::string dataName = std::string(data::IRadioData::descriptor) + "/" + slot_name_;
    std::string modemName = std::string(modem::IRadioModem::descriptor) + "/" + slot_name_;
    std::string messagingName = std::string(messaging::IRadioMessaging::descriptor) + "/" + slot_name_;

    ALOGI("Connecting to AIDL services with slot=%s...", slot_name_.c_str());

    // Connect to Voice
    AIBinder* bVoice = AServiceManager_waitForService(voiceName.c_str());
    if (bVoice) {
        radio_voice_ = voice::IRadioVoice::fromBinder(ndk::SpAIBinder(bVoice));
        if (radio_voice_) {
            auto voiceResp = ndk::SharedRefBase::make<RadioVoiceResponseImpl>(this);
            auto voiceInd = ndk::SharedRefBase::make<RadioVoiceIndicationImpl>(this);
            radio_voice_->setResponseFunctions(voiceResp, voiceInd);
            AIBinder_linkToDeath(bVoice, death_recipient_.get(), this);
            ALOGI("Connected to IRadioVoice");
        }
    } else {
        ALOGW("Waiting for %s failed", voiceName.c_str());
    }

    // Connect to Network
    AIBinder* bNet = AServiceManager_waitForService(networkName.c_str());
    if (bNet) {
        radio_network_ = network::IRadioNetwork::fromBinder(ndk::SpAIBinder(bNet));
        if (radio_network_) {
            auto netResp = ndk::SharedRefBase::make<RadioNetworkResponseImpl>(this);
            auto netInd = ndk::SharedRefBase::make<RadioNetworkIndicationImpl>(this);
            radio_network_->setResponseFunctions(netResp, netInd);
            AIBinder_linkToDeath(bNet, death_recipient_.get(), this);
            ALOGI("Connected to IRadioNetwork");
        }
    }

    // Connect to SIM
    AIBinder* bSim = AServiceManager_waitForService(simName.c_str());
    if (bSim) {
        radio_sim_ = sim::IRadioSim::fromBinder(ndk::SpAIBinder(bSim));
        if (radio_sim_) {
            auto simResp = ndk::SharedRefBase::make<RadioSimResponseImpl>(this);
            auto simInd = ndk::SharedRefBase::make<RadioSimIndicationImpl>(this);
            radio_sim_->setResponseFunctions(simResp, simInd);
            AIBinder_linkToDeath(bSim, death_recipient_.get(), this);
            ALOGI("Connected to IRadioSim");
        }
    }

    // Connect to Data
    AIBinder* bData = AServiceManager_waitForService(dataName.c_str());
    if (bData) {
        radio_data_ = data::IRadioData::fromBinder(ndk::SpAIBinder(bData));
        if (radio_data_) {
            auto dataResp = ndk::SharedRefBase::make<RadioDataResponseImpl>(this);
            auto dataInd = ndk::SharedRefBase::make<RadioDataIndicationImpl>(this);
            radio_data_->setResponseFunctions(dataResp, dataInd);
            AIBinder_linkToDeath(bData, death_recipient_.get(), this);
            ALOGI("Connected to IRadioData");
        }
    }

    // Connect to Modem
    AIBinder* bModem = AServiceManager_waitForService(modemName.c_str());
    if (bModem) {
        radio_modem_ = modem::IRadioModem::fromBinder(ndk::SpAIBinder(bModem));
        if (radio_modem_) {
            auto modemResp = ndk::SharedRefBase::make<RadioModemResponseImpl>(this);
            auto modemInd = ndk::SharedRefBase::make<RadioModemIndicationImpl>(this);
            radio_modem_->setResponseFunctions(modemResp, modemInd);
            AIBinder_linkToDeath(bModem, death_recipient_.get(), this);
            ALOGI("Connected to IRadioModem");
        }
    }

    // Connect to Messaging
    AIBinder* bMsg = AServiceManager_waitForService(messagingName.c_str());
    if (bMsg) {
        radio_messaging_ = messaging::IRadioMessaging::fromBinder(ndk::SpAIBinder(bMsg));
        if (radio_messaging_) {
            auto msgResp = ndk::SharedRefBase::make<RadioMessagingResponseImpl>(this);
            auto msgInd = ndk::SharedRefBase::make<RadioMessagingIndicationImpl>(this);
            radio_messaging_->setResponseFunctions(msgResp, msgInd);
            AIBinder_linkToDeath(bMsg, death_recipient_.get(), this);
            ALOGI("Connected to IRadioMessaging");
        }
    }

    return (radio_voice_ && radio_network_ && radio_sim_ && radio_data_ && radio_modem_);
}

void RadioProxy::disconnectAidlServices() {
    radio_voice_.reset();
    radio_network_.reset();
    radio_sim_.reset();
    radio_data_.reset();
    radio_modem_.reset();
    radio_messaging_.reset();
}

void RadioProxy::onBinderDied(const std::string& service_name) {
    ALOGW("Service %s died, cleaning up and initiating reconnect loop...", service_name.c_str());
    disconnectAidlServices();

    std::thread([this]() {
        int retries = 0;
        while (running_ && retries < 20) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            ALOGI("Reconnecting to AIDL radio services (attempt %d)...", ++retries);
            if (connectToAidlServices()) {
                ALOGI("Successfully reconnected to AIDL radio services!");
                break;
            }
        }
    }).detach();
}

int32_t RadioProxy::allocateSerial(int client_fd, uint32_t client_seq, uint32_t command_id) {
    int32_t serial = next_serial_.fetch_add(1);
    if (serial <= 0) serial = next_serial_.fetch_add(1);

    std::lock_guard<std::mutex> lock(pending_mutex_);
    PendingTransaction trans;
    trans.client_fd = client_fd;
    trans.client_seq = client_seq;
    trans.command_id = command_id;
    trans.timestamp = std::chrono::steady_clock::now();
    pending_transactions_[serial] = trans;

    return serial;
}

bool RadioProxy::findAndRemovePending(int32_t serial, PendingTransaction& out_trans) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    auto it = pending_transactions_.find(serial);
    if (it == pending_transactions_.end()) return false;
    out_trans = it->second;
    pending_transactions_.erase(it);
    return true;
}

void RadioProxy::notifyVoiceResponse(int32_t serial, bridge::BridgeStatus status,
                                    const std::vector<uint8_t>& payload) {
    PendingTransaction trans;
    if (!findAndRemovePending(serial, trans)) return;

    bridge::MessageHeader hdr;
    hdr.magic = bridge::PROTOCOL_MAGIC;
    hdr.version = bridge::PROTOCOL_VERSION;
    hdr.message_type = static_cast<uint16_t>(bridge::MessageType::RESPONSE);
    hdr.command_id = trans.command_id;
    hdr.sequence = trans.client_seq;
    hdr.status = static_cast<int32_t>(status);
    hdr.payload_size = static_cast<uint32_t>(payload.size());

    bridge::SocketHelper::sendMessage(trans.client_fd, hdr, payload.data());
}

void RadioProxy::notifyNetworkResponse(int32_t serial, bridge::BridgeStatus status,
                                      const std::vector<uint8_t>& payload) {
    notifyVoiceResponse(serial, status, payload);
}

void RadioProxy::notifySimResponse(int32_t serial, bridge::BridgeStatus status,
                                  const std::vector<uint8_t>& payload) {
    notifyVoiceResponse(serial, status, payload);
}

void RadioProxy::notifyDataResponse(int32_t serial, bridge::BridgeStatus status,
                                   const std::vector<uint8_t>& payload) {
    notifyVoiceResponse(serial, status, payload);
}

void RadioProxy::notifyModemResponse(int32_t serial, bridge::BridgeStatus status,
                                    const std::vector<uint8_t>& payload) {
    notifyVoiceResponse(serial, status, payload);
}

void RadioProxy::notifyMessagingResponse(int32_t serial, bridge::BridgeStatus status,
                                        const std::vector<uint8_t>& payload) {
    notifyVoiceResponse(serial, status, payload);
}

void RadioProxy::broadcastIndication(uint32_t ind_id, const std::vector<uint8_t>& payload) {
    bridge::MessageHeader hdr;
    hdr.magic = bridge::PROTOCOL_MAGIC;
    hdr.version = bridge::PROTOCOL_VERSION;
    hdr.message_type = static_cast<uint16_t>(bridge::MessageType::INDICATION);
    hdr.command_id = ind_id;
    hdr.sequence = 0;
    hdr.status = static_cast<int32_t>(bridge::BridgeStatus::OK);
    hdr.payload_size = static_cast<uint32_t>(payload.size());

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int fd : client_fds_) {
        bridge::SocketHelper::sendMessage(fd, hdr, payload.data());
    }
}

void RadioProxy::clientLoop(int client_fd) {
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

void RadioProxy::handleClientMessage(int client_fd, const bridge::MessageHeader& hdr,
                                    const std::vector<uint8_t>& payload) {
    bridge::BufferReader reader(payload);

    switch (hdr.command_id) {
        case bridge::radio_cmd::DIAL:
            cmdDial(client_fd, hdr.sequence, reader);
            break;
        case bridge::radio_cmd::HANGUP:
            cmdHangup(client_fd, hdr.sequence, reader);
            break;
        case bridge::radio_cmd::ANSWER:
            cmdAnswer(client_fd, hdr.sequence);
            break;
        case bridge::radio_cmd::GET_SIGNAL_STRENGTH:
            cmdGetSignalStrength(client_fd, hdr.sequence);
            break;
        case bridge::radio_cmd::GET_OPERATOR:
            cmdGetOperator(client_fd, hdr.sequence);
            break;
        case bridge::radio_cmd::GET_SIM_STATUS:
            cmdGetSimStatus(client_fd, hdr.sequence);
            break;
        case bridge::radio_cmd::SEND_SMS:
            cmdSendSms(client_fd, hdr.sequence, reader);
            break;
        case bridge::radio_cmd::SETUP_DATA_CALL:
            cmdSetupDataCall(client_fd, hdr.sequence, reader);
            break;
        case bridge::radio_cmd::DEACTIVATE_DATA_CALL:
            cmdDeactivateDataCall(client_fd, hdr.sequence, reader);
            break;
        case bridge::radio_cmd::GET_NETWORK_TYPE:
            cmdGetNetworkType(client_fd, hdr.sequence);
            break;
        case bridge::radio_cmd::SET_NETWORK_TYPE:
            cmdSetNetworkType(client_fd, hdr.sequence, reader);
            break;
        case bridge::radio_cmd::SET_RADIO_POWER:
            cmdSetRadioPower(client_fd, hdr.sequence, reader);
            break;
        default: {
            ALOGW("Unknown radio command id: 0x%04X", hdr.command_id);
            bridge::MessageHeader err_hdr = hdr;
            err_hdr.message_type = static_cast<uint16_t>(bridge::MessageType::RESPONSE);
            err_hdr.status = static_cast<int32_t>(bridge::BridgeStatus::ERR_NOT_SUPPORTED);
            err_hdr.payload_size = 0;
            bridge::SocketHelper::sendMessage(client_fd, err_hdr, nullptr);
            break;
        }
    }
}

void RadioProxy::cmdDial(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    std::string address;
    int32_t clirMode = 0;
    if (!reader.readString(address) || !reader.read(clirMode)) {
        return;
    }

    if (!radio_voice_) {
        bridge::MessageHeader resp{bridge::PROTOCOL_MAGIC, bridge::PROTOCOL_VERSION,
            static_cast<uint16_t>(bridge::MessageType::RESPONSE), bridge::radio_cmd::DIAL, seq,
            static_cast<int32_t>(bridge::BridgeStatus::ERR_NOT_CONNECTED), 0};
        bridge::SocketHelper::sendMessage(client_fd, resp);
        return;
    }

    voice::Dial dialInfo;
    dialInfo.address = address;
    dialInfo.clir = clirMode;

    int32_t serial = allocateSerial(client_fd, seq, bridge::radio_cmd::DIAL);
    radio_voice_->dial(serial, dialInfo);
}

void RadioProxy::cmdHangup(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    int32_t gsmIndex = 1;
    reader.read(gsmIndex);

    if (!radio_voice_) return;
    int32_t serial = allocateSerial(client_fd, seq, bridge::radio_cmd::HANGUP);
    radio_voice_->hangup(serial, gsmIndex);
}

void RadioProxy::cmdAnswer(int client_fd, uint32_t seq) {
    if (!radio_voice_) return;
    int32_t serial = allocateSerial(client_fd, seq, bridge::radio_cmd::ANSWER);
    radio_voice_->acceptCall(serial);
}

void RadioProxy::cmdGetSignalStrength(int client_fd, uint32_t seq) {
    if (!radio_network_) return;
    int32_t serial = allocateSerial(client_fd, seq, bridge::radio_cmd::GET_SIGNAL_STRENGTH);
    radio_network_->getSignalStrength(serial);
}

void RadioProxy::cmdGetOperator(int client_fd, uint32_t seq) {
    if (!radio_network_) return;
    int32_t serial = allocateSerial(client_fd, seq, bridge::radio_cmd::GET_OPERATOR);
    radio_network_->getOperator(serial);
}

void RadioProxy::cmdGetSimStatus(int client_fd, uint32_t seq) {
    if (!radio_sim_) return;
    int32_t serial = allocateSerial(client_fd, seq, bridge::radio_cmd::GET_SIM_STATUS);
    radio_sim_->getIccCardStatus(serial);
}

void RadioProxy::cmdSendSms(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    std::string smscPdu;
    std::string pdu;
    if (!reader.readString(smscPdu) || !reader.readString(pdu)) return;

    if (!radio_messaging_) return;

    messaging::GsmSmsMessage msg;
    msg.smscPdu = smscPdu;
    msg.pdu = pdu;

    int32_t serial = allocateSerial(client_fd, seq, bridge::radio_cmd::SEND_SMS);
    radio_messaging_->sendSms(serial, msg);
}

void RadioProxy::cmdSetupDataCall(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    int32_t accessNetwork = 0;
    std::string apn;
    int32_t authType = 0;
    std::string user, password;

    reader.read(accessNetwork);
    reader.readString(apn);
    reader.read(authType);
    reader.readString(user);
    reader.readString(password);

    if (!radio_data_) return;

    data::DataProfile profile;
    profile.profileId = 0;
    profile.apn = apn;
    profile.protocol = data::PdpProtocolType::IP;
    profile.roamingProtocol = data::PdpProtocolType::IP;
    profile.authType = static_cast<data::ApnAuthType>(authType);
    profile.user = user;
    profile.password = password;
    profile.type = 1; // Default
    profile.enabled = true;

    int32_t serial = allocateSerial(client_fd, seq, bridge::radio_cmd::SETUP_DATA_CALL);
    radio_data_->setupDataCall(serial, static_cast<AccessNetwork>(accessNetwork), profile,
                              false, data::DataRequestReason::NORMAL, {}, {});
}

void RadioProxy::cmdDeactivateDataCall(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    int32_t cid = 0;
    int32_t reason = 0;
    reader.read(cid);
    reader.read(reason);

    if (!radio_data_) return;

    int32_t serial = allocateSerial(client_fd, seq, bridge::radio_cmd::DEACTIVATE_DATA_CALL);
    radio_data_->deactivateDataCall(serial, cid, static_cast<data::DataRequestReason>(reason));
}

void RadioProxy::cmdGetNetworkType(int client_fd, uint32_t seq) {
    if (!radio_network_) return;
    int32_t serial = allocateSerial(client_fd, seq, bridge::radio_cmd::GET_NETWORK_TYPE);
    radio_network_->getAllowedNetworkTypesBitmap(serial);
}

void RadioProxy::cmdSetNetworkType(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    int32_t bitmap = 0;
    reader.read(bitmap);

    if (!radio_network_) return;
    int32_t serial = allocateSerial(client_fd, seq, bridge::radio_cmd::SET_NETWORK_TYPE);
    radio_network_->setAllowedNetworkTypesBitmap(serial, bitmap);
}

void RadioProxy::cmdSetRadioPower(int client_fd, uint32_t seq, bridge::BufferReader& reader) {
    uint8_t powerOn = 1;
    reader.read(powerOn);

    if (!radio_modem_) return;
    int32_t serial = allocateSerial(client_fd, seq, bridge::radio_cmd::SET_RADIO_POWER);
    radio_modem_->setRadioPower(serial, powerOn != 0, false, false);
}

} // namespace radio
} // namespace ubtouch
