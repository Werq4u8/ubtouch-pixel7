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

#define LOG_TAG "ubtouch-audio-bridge-main"

#include "audio_proxy.h"
#include <csignal>
#include <cstdlib>
#include <getopt.h>
#include <condition_variable>
#include <mutex>

static std::condition_variable g_shutdown_cv;
static std::mutex g_shutdown_mutex;
static bool g_shutdown_requested = false;

static void signalHandler(int sig) {
    ALOGI("Received signal %d, initiating audio shutdown...", sig);
    {
        std::lock_guard<std::mutex> lock(g_shutdown_mutex);
        g_shutdown_requested = true;
    }
    g_shutdown_cv.notify_one();
}

static void printUsage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -s, --socket <path>     UNIX domain socket path (default: %s)\n", ubtouch::bridge::AUDIO_SOCKET_PATH);
    printf("  -m, --module <name>     Audio Core module name (default: default)\n");
    printf("  -h, --help              Show this help message\n");
}

int main(int argc, char** argv) {
    std::string socket_path = ubtouch::bridge::AUDIO_SOCKET_PATH;
    std::string module_name = "default";

    static struct option long_options[] = {
        {"socket", required_argument, nullptr, 's'},
        {"module", required_argument, nullptr, 'm'},
        {"help",   no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "s:m:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 's':
                socket_path = optarg;
                break;
            case 'm':
                module_name = optarg;
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    ALOGI("Starting ubtouch-audio-bridge (Pixel 7 / panther)...");
    ALOGI("Socket path: %s, Module: %s", socket_path.c_str(), module_name.c_str());

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGHUP, signalHandler);

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    auto proxy = std::make_unique<ubtouch::audio::AudioProxy>(socket_path, module_name);
    if (!proxy->start()) {
        ALOGE("Failed to start AudioProxy daemon!");
        return 1;
    }

    ALOGI("AudioProxy daemon is running.");

    {
        std::unique_lock<std::mutex> lock(g_shutdown_mutex);
        g_shutdown_cv.wait(lock, [] { return g_shutdown_requested; });
    }

    ALOGI("Shutting down AudioProxy daemon...");
    proxy->stop();
    ALOGI("AudioProxy daemon terminated cleanly.");

    return 0;
}
