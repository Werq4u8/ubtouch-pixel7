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

#ifndef UBTIAL_BRIDGE_PROTOCOL_H_
#define UBTIAL_BRIDGE_PROTOCOL_H_

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __ANDROID__
#include <log/log.h>
#else
#include <cstdio>
#define ALOGV(fmt, ...) printf("[VERBOSE] " fmt "\n", ##__VA_ARGS__)
#define ALOGD(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define ALOGI(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define ALOGW(fmt, ...) fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)
#define ALOGE(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#endif

namespace ubtouch {
namespace bridge {

// Magic bytes identifying bridge protocol packets: "UBTP" (Ubuntu Touch Port)
constexpr uint32_t PROTOCOL_MAGIC = 0x55425450;
constexpr uint16_t PROTOCOL_VERSION = 1;

// Default socket directories and paths
constexpr const char* DEFAULT_SOCKET_DIR = "/dev/socket";
constexpr const char* RADIO_SOCKET_PATH = "/dev/socket/ubtouch-radio";
constexpr const char* CAMERA_SOCKET_PATH = "/dev/socket/ubtouch-camera";
constexpr const char* AUDIO_SOCKET_PATH = "/dev/socket/ubtouch-audio";
constexpr const char* GNSS_SOCKET_PATH = "/dev/socket/ubtouch-gnss";

/**
 * Message Type Enumeration
 */
enum class MessageType : uint16_t {
    REQUEST     = 1, // Client -> Daemon command
    RESPONSE    = 2, // Daemon -> Client direct reply to a request
    INDICATION  = 3, // Daemon -> Client asynchronous event from HAL
    EVENT       = 4, // General system or lifecycle notification
    ERROR_RESP  = 5  // Error response
};

/**
 * Common Error Codes
 */
enum class BridgeStatus : int32_t {
    OK                  = 0,
    ERR_GENERIC         = -1,
    ERR_INVALID_ARG     = -2,
    ERR_NOT_CONNECTED   = -3,
    ERR_TIMEOUT         = -4,
    ERR_NOT_SUPPORTED   = -5,
    ERR_HAL_ERROR       = -6,
    ERR_SOCKET          = -7,
    ERR_NO_MEMORY       = -8,
    ERR_BUSY            = -9,
    ERR_PERMISSION      = -10,
    ERR_SERVICE_DIED    = -11
};

#pragma pack(push, 1)

/**
 * Standard protocol header prepended to every message over Unix domain socket.
 */
struct MessageHeader {
    uint32_t magic;          // PROTOCOL_MAGIC (0x55425450)
    uint16_t version;        // PROTOCOL_VERSION (1)
    uint16_t message_type;   // MessageType (cast to uint16_t)
    uint32_t command_id;     // Domain-specific command or indication ID
    uint32_t sequence;       // Transaction sequence number for correlation
    int32_t  status;         // BridgeStatus error/success code
    uint32_t payload_size;   // Size of the payload following this header
};

#pragma pack(pop)

static_assert(sizeof(MessageHeader) == 24, "MessageHeader packing mismatch");

/**
 * Domain-specific Command IDs
 */
namespace radio_cmd {
    constexpr uint32_t DIAL                 = 0x0101;
    constexpr uint32_t HANGUP               = 0x0102;
    constexpr uint32_t ANSWER               = 0x0103;
    constexpr uint32_t GET_SIGNAL_STRENGTH  = 0x0104;
    constexpr uint32_t GET_OPERATOR         = 0x0105;
    constexpr uint32_t GET_SIM_STATUS       = 0x0106;
    constexpr uint32_t SEND_SMS             = 0x0107;
    constexpr uint32_t SETUP_DATA_CALL      = 0x0108;
    constexpr uint32_t DEACTIVATE_DATA_CALL = 0x0109;
    constexpr uint32_t GET_NETWORK_TYPE     = 0x010A;
    constexpr uint32_t SET_NETWORK_TYPE     = 0x010B;
    constexpr uint32_t SET_RADIO_POWER      = 0x010C;
    constexpr uint32_t GET_RADIO_POWER      = 0x010D;
    constexpr uint32_t GET_CALL_LIST        = 0x010E;
    constexpr uint32_t SEND_USSD            = 0x010F;
    constexpr uint32_t CANCEL_USSD          = 0x0110;

    // Asynchronous Indications (HAL -> Ubuntu Touch)
    constexpr uint32_t IND_CALL_STATE_CHANGED     = 0x0181;
    constexpr uint32_t IND_SIGNAL_STRENGTH        = 0x0182;
    constexpr uint32_t IND_NETWORK_STATE_CHANGED  = 0x0183;
    constexpr uint32_t IND_SIM_STATUS_CHANGED     = 0x0184;
    constexpr uint32_t IND_NEW_SMS                = 0x0185;
    constexpr uint32_t IND_DATA_CALL_LIST_CHANGED = 0x0186;
    constexpr uint32_t IND_RADIO_STATE_CHANGED    = 0x0187;
    constexpr uint32_t IND_USSD_RECEIVED          = 0x0188;
}

namespace camera_cmd {
    constexpr uint32_t GET_CAMERA_LIST      = 0x0201;
    constexpr uint32_t GET_CAMERA_INFO      = 0x0202;
    constexpr uint32_t OPEN_CAMERA          = 0x0203;
    constexpr uint32_t CLOSE_CAMERA         = 0x0204;
    constexpr uint32_t START_PREVIEW        = 0x0205;
    constexpr uint32_t STOP_PREVIEW         = 0x0206;
    constexpr uint32_t TAKE_PICTURE         = 0x0207;
    constexpr uint32_t SET_PARAMETERS       = 0x0208;
    constexpr uint32_t GET_PARAMETERS       = 0x0209;
    constexpr uint32_t AUTO_FOCUS           = 0x020A;
    constexpr uint32_t CANCEL_AUTO_FOCUS    = 0x020B;

    // Indications
    constexpr uint32_t IND_PREVIEW_FRAME    = 0x0281;
    constexpr uint32_t IND_PICTURE_READY    = 0x0282;
    constexpr uint32_t IND_AUTOFOCUS_RESULT = 0x0283;
    constexpr uint32_t IND_CAMERA_ERROR     = 0x0284;
    constexpr uint32_t IND_DEVICE_STATUS    = 0x0285;
}

namespace audio_cmd {
    constexpr uint32_t OPEN_STREAM          = 0x0301;
    constexpr uint32_t CLOSE_STREAM         = 0x0302;
    constexpr uint32_t WRITE_AUDIO          = 0x0303;
    constexpr uint32_t READ_AUDIO           = 0x0304;
    constexpr uint32_t SET_VOLUME           = 0x0305;
    constexpr uint32_t GET_VOLUME           = 0x0306;
    constexpr uint32_t SET_MUTE             = 0x0307;
    constexpr uint32_t GET_MUTE             = 0x0308;
    constexpr uint32_t SET_PARAMETERS       = 0x0309;
    constexpr uint32_t GET_PARAMETERS       = 0x030A;
    constexpr uint32_t SET_AUDIO_MODE       = 0x030B;
    constexpr uint32_t SET_ROUTING          = 0x030C;

    // Indications
    constexpr uint32_t IND_ROUTING_CHANGED  = 0x0381;
    constexpr uint32_t IND_BUFFER_UNDERRUN  = 0x0382;
    constexpr uint32_t IND_AUDIO_ERROR      = 0x0383;
}

namespace gnss_cmd {
    constexpr uint32_t START                = 0x0401;
    constexpr uint32_t STOP                 = 0x0402;
    constexpr uint32_t GET_LOCATION         = 0x0403;
    constexpr uint32_t INJECT_TIME          = 0x0404;
    constexpr uint32_t INJECT_LOCATION      = 0x0405;
    constexpr uint32_t DELETE_AIDING_DATA   = 0x0406;
    constexpr uint32_t SET_POSITION_MODE    = 0x0407;

    // Indications
    constexpr uint32_t IND_LOCATION         = 0x0481;
    constexpr uint32_t IND_STATUS           = 0x0482;
    constexpr uint32_t IND_SV_STATUS        = 0x0483;
    constexpr uint32_t IND_NMEA             = 0x0484;
    constexpr uint32_t IND_CAPABILITIES     = 0x0485;
}

/**
 * Binary Serialization Helpers
 */
class BufferWriter {
public:
    BufferWriter() = default;
    explicit BufferWriter(size_t reserve_bytes) {
        data_.reserve(reserve_bytes);
    }

    template <typename T>
    void write(const T& val) {
        static_assert(std::is_trivially_copyable<T>::value, "Type must be trivially copyable");
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&val);
        data_.insert(data_.end(), p, p + sizeof(T));
    }

    void writeString(const std::string& str) {
        uint32_t len = static_cast<uint32_t>(str.size());
        write(len);
        if (len > 0) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(str.data());
            data_.insert(data_.end(), p, p + len);
        }
    }

    void writeBytes(const void* src, size_t len) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
        data_.insert(data_.end(), p, p + len);
    }

    const std::vector<uint8_t>& data() const { return data_; }
    size_t size() const { return data_.size(); }
    const uint8_t* buffer() const { return data_.data(); }

private:
    std::vector<uint8_t> data_;
};

class BufferReader {
public:
    BufferReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), offset_(0) {}

    explicit BufferReader(const std::vector<uint8_t>& vec)
        : data_(vec.data()), size_(vec.size()), offset_(0) {}

    template <typename T>
    bool read(T& out_val) {
        static_assert(std::is_trivially_copyable<T>::value, "Type must be trivially copyable");
        if (offset_ + sizeof(T) > size_) return false;
        std::memcpy(&out_val, data_ + offset_, sizeof(T));
        offset_ += sizeof(T);
        return true;
    }

    bool readString(std::string& out_str) {
        uint32_t len = 0;
        if (!read(len)) return false;
        if (offset_ + len > size_) return false;
        out_str.assign(reinterpret_cast<const char*>(data_ + offset_), len);
        offset_ += len;
        return true;
    }

    bool readBytes(void* dst, size_t len) {
        if (offset_ + len > size_) return false;
        std::memcpy(dst, data_ + offset_, len);
        offset_ += len;
        return true;
    }

    size_t remaining() const { return size_ > offset_ ? size_ - offset_ : 0; }
    size_t offset() const { return offset_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t offset_;
};

/**
 * Socket Communication Helpers
 */
class SocketHelper {
public:
    static int createServerSocket(const char* socket_path, int backlog = 8) {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            ALOGE("Failed to create UNIX domain socket: %s", strerror(errno));
            return -1;
        }

        // Unlink previous socket file if it exists
        unlink(socket_path);

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

        if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            ALOGE("Failed to bind socket to '%s': %s", socket_path, strerror(errno));
            close(fd);
            return -1;
        }

        // Set permissions so Ubuntu Touch (user phablet / system) can connect
        chmod(socket_path, 0666);

        if (listen(fd, backlog) < 0) {
            ALOGE("Failed to listen on socket '%s': %s", socket_path, strerror(errno));
            close(fd);
            return -1;
        }

        ALOGI("Socket listening on %s (fd=%d)", socket_path, fd);
        return fd;
    }

    static int acceptClient(int server_fd) {
        struct sockaddr_un client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept4(server_fd, reinterpret_cast<struct sockaddr*>(&client_addr),
                                &addr_len, SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ALOGE("Failed to accept client connection: %s", strerror(errno));
            }
            return -1;
        }
        return client_fd;
    }

    static int connectClient(const char* socket_path) {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return -1;

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

        if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd);
            return -1;
        }
        return fd;
    }

    static bool sendAll(int fd, const void* data, size_t total) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        size_t written = 0;
        while (written < total) {
            ssize_t n = send(fd, p + written, total - written, MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) continue;
                ALOGE("send() error on fd %d: %s", fd, strerror(errno));
                return false;
            }
            if (n == 0) return false;
            written += n;
        }
        return true;
    }

    static bool recvAll(int fd, void* buffer, size_t total) {
        uint8_t* p = static_cast<uint8_t*>(buffer);
        size_t bytes_read = 0;
        while (bytes_read < total) {
            ssize_t n = recv(fd, p + bytes_read, total - bytes_read, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            if (n == 0) return false; // Peer disconnected
            bytes_read += n;
        }
        return true;
    }

    static bool sendMessage(int fd, const MessageHeader& hdr, const void* payload = nullptr) {
        if (!sendAll(fd, &hdr, sizeof(MessageHeader))) return false;
        if (hdr.payload_size > 0 && payload != nullptr) {
            return sendAll(fd, payload, hdr.payload_size);
        }
        return true;
    }

    static bool recvMessage(int fd, MessageHeader& hdr, std::vector<uint8_t>& payload) {
        if (!recvAll(fd, &hdr, sizeof(MessageHeader))) return false;
        if (hdr.magic != PROTOCOL_MAGIC) {
            ALOGE("Invalid magic in received message: 0x%08X (expected 0x%08X)",
                  hdr.magic, PROTOCOL_MAGIC);
            return false;
        }
        if (hdr.payload_size > 0) {
            // Safety cap: 16 MB maximum message payload
            if (hdr.payload_size > 16 * 1024 * 1024) {
                ALOGE("Message payload exceeds safety limit: %u bytes", hdr.payload_size);
                return false;
            }
            payload.resize(hdr.payload_size);
            return recvAll(fd, payload.data(), hdr.payload_size);
        } else {
            payload.clear();
        }
        return true;
    }

    /**
     * Send file descriptor over UNIX domain socket (ancillary data / SCM_RIGHTS)
     * Used for zero-copy frame buffer / shared memory exchange
     */
    static bool sendFd(int socket_fd, int fd_to_send, const MessageHeader& hdr) {
        struct msghdr msg;
        std::memset(&msg, 0, sizeof(msg));

        struct iovec iov[1];
        iov[0].iov_base = const_cast<MessageHeader*>(&hdr);
        iov[0].iov_len = sizeof(MessageHeader);
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;

        char cmsg_buf[CMSG_SPACE(sizeof(int))];
        std::memset(cmsg_buf, 0, sizeof(cmsg_buf));
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof(cmsg_buf);

        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        *reinterpret_cast<int*>(CMSG_DATA(cmsg)) = fd_to_send;

        ssize_t sent = sendmsg(socket_fd, &msg, MSG_NOSIGNAL);
        return sent == sizeof(MessageHeader);
    }

    static int recvFd(int socket_fd, MessageHeader& hdr) {
        struct msghdr msg;
        std::memset(&msg, 0, sizeof(msg));

        struct iovec iov[1];
        iov[0].iov_base = &hdr;
        iov[0].iov_len = sizeof(MessageHeader);
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;

        char cmsg_buf[CMSG_SPACE(sizeof(int))];
        std::memset(cmsg_buf, 0, sizeof(cmsg_buf));
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof(cmsg_buf);

        ssize_t received = recvmsg(socket_fd, &msg, 0);
        if (received != sizeof(MessageHeader)) return -1;

        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            return -1;
        }

        int received_fd = *reinterpret_cast<int*>(CMSG_DATA(cmsg));
        return received_fd;
    }
};

} // namespace bridge
} // namespace ubtouch

#endif // UBTIAL_BRIDGE_PROTOCOL_H_
