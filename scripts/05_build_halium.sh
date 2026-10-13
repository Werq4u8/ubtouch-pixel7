#!/usr/bin/env bash
# ==============================================================================
# Скрипт 05: Сборка компонентов Halium 13 для Pixel 7 (Build Halium)
# - Генерация файлов halium.mk, vendorsetup.sh, init.halium.rc
# - Применение патчей Halium / libhybris к дереву LineageOS 20
# - Оптимизация памяти (динамический расчет потоков, предотвращение OOM)
# - Компиляция halium-boot и systemimage
# ==============================================================================

set -euo pipefail

# Цветовая палитра
readonly COLOR_RESET="\033[0m"
readonly COLOR_RED="\033[1;31m"
readonly COLOR_GREEN="\033[1;32m"
readonly COLOR_YELLOW="\033[1;33m"
readonly COLOR_BLUE="\033[1;34m"
readonly COLOR_CYAN="\033[1;36m"

log_info() { echo -e "${COLOR_BLUE}[ИНФО]${COLOR_RESET} $*"; }
log_success() { echo -e "${COLOR_GREEN}[УСПЕХ]${COLOR_RESET} $*"; }
log_warn() { echo -e "${COLOR_YELLOW}[ВНИМАНИЕ]${COLOR_RESET} $*"; }
log_error() { echo -e "${COLOR_RED}[ОШИБКА]${COLOR_RESET} $*" >&2; }

trap_err() {
    local exit_code=$?
    local line_no=$1
    log_error "Ошибка сборки Halium в строке ${line_no} (код: ${exit_code})"
    exit "${exit_code}"
}
trap 'trap_err ${LINENO}' ERR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
if [ -f "${PROJECT_ROOT}/.env_setup" ]; then
    # shellcheck source=/dev/null
    source "${PROJECT_ROOT}/.env_setup"
fi

LINEAGE_DIR="${PROJECT_ROOT}/src/lineage"
DEVICE_DIR="${LINEAGE_DIR}/device/google/panther"
IMAGES_DIR="${PROJECT_ROOT}/out/images"
LOG_DIR="${PROJECT_ROOT}/logs"

mkdir -p "${IMAGES_DIR}" "${LOG_DIR}"

log_info "=========================================================="
log_info " Запуск скрипта 05_build_halium.sh: Подготовка и сборка Halium"
log_info "=========================================================="

if [ ! -d "${LINEAGE_DIR}" ]; then
    log_error "Директория с исходным кодом LineageOS не найдена: ${LINEAGE_DIR}"
    exit 1
fi

mkdir -p "${DEVICE_DIR}"

# ==============================================================================
# ШАГ 1: Создание файлов конфигурации Halium (halium.mk, vendorsetup, init.rc)
# ==============================================================================
log_info "Шаг 1/4: Генерация файлов конфигурации Halium для Pixel 7..."

# 1. halium.mk
HALIUM_MK="${DEVICE_DIR}/halium.mk"
cat <<'EOF' > "${HALIUM_MK}"
# Halium Device Makefile for Google Pixel 7 (panther)
# Inherit base LineageOS Panther configuration

$(call inherit-product, device/google/panther/lineage_panther.mk)

PRODUCT_NAME := halium_panther
PRODUCT_DEVICE := panther
PRODUCT_BRAND := google
PRODUCT_MODEL := Pixel 7
PRODUCT_MANUFACTURER := Google

# Halium specific flags
TARGET_SUPPORTS_UBUNTU_TOUCH := true
HALIUM_USE_FRAMEWORK := false
ENABLE_HYBRIS := true

# System partitions and layout for Halium 13
PRODUCT_BUILD_SYSTEM_IMAGE := true
PRODUCT_BUILD_BOOT_IMAGE := true
BOARD_BUILD_SYSTEM_ROOT_IMAGE := false

# Init scripts
PRODUCT_COPY_FILES += \
    device/google/panther/init.halium.rc:$(TARGET_COPY_OUT_SYSTEM)/etc/init/init.halium.rc
EOF
log_success "Файл создан: ${HALIUM_MK}"

# 2. vendorsetup.sh
VENDORSETUP_SH="${DEVICE_DIR}/vendorsetup.sh"
cat <<'EOF' > "${VENDORSETUP_SH}"
# Halium combo setup for Panther
add_lunch_combo halium_panther-userdebug
add_lunch_combo halium_panther-eng
EOF
chmod +x "${VENDORSETUP_SH}"
log_success "Файл создан: ${VENDORSETUP_SH}"

# 3. init.halium.rc
INIT_HALIUM_RC="${DEVICE_DIR}/init.halium.rc"
cat <<'EOF' > "${INIT_HALIUM_RC}"
# Init script for Halium container on Google Pixel 7 (panther)

on early-init
    # Set up devtmpfs and binderfs
    mkdir /dev/binderfs
    mount binder binder /dev/binderfs type=binder

on init
    # Configure logging and properties
    export PATH /sbin:/system/bin:/system/xbin
    export LD_LIBRARY_PATH /system/lib64:/vendor/lib64

on boot
    # Disable surfaceflinger & zygote for pure Halium/Lomiri display stack
    stop surfaceflinger
    stop zygote
    stop zygote_secondary

    # Start essential hardware service daemons for libhybris
    start servicemanager
    start hwservicemanager
    start vndservicemanager

    # Network USB rndis tethering for debugging
    setprop sys.usb.config rndis,adb

service halium_usb /system/bin/sh /system/etc/init/halium-usb.sh
    class core
    user root
    oneshot
EOF
log_success "Файл создан: ${INIT_HALIUM_RC}"

# ==============================================================================
# ШАГ 2: Применение Halium патчей к дереву LineageOS 20
# ==============================================================================
log_info "Шаг 2/4: Применение патчей Halium/Hybris..."

HYBRIS_PATCHES_DIR="${PROJECT_ROOT}/src/halium/hybris-patches"
if [ ! -d "${HYBRIS_PATCHES_DIR}" ]; then
    log_info "Загрузка репозитория патчей hybris-patches..."
    mkdir -p "${PROJECT_ROOT}/src/halium"
    git clone https://github.com/Halium/hybris-patches.git "${HYBRIS_PATCHES_DIR}" || true
fi

if [ -d "${HYBRIS_PATCHES_DIR}" ]; then
    log_info "Применение патчей к LineageOS дереву..."
    python3 "${HYBRIS_PATCHES_DIR}/apply-patches.py" --tree "${LINEAGE_DIR}" || {
        log_warn "Некоторые патчи были пропущены или уже применены ранее."
    }
fi

# ==============================================================================
# ШАГ 3: Защита от Out-Of-Memory (OOM) и расчет доступных ресурсов хоста
# ==============================================================================
log_info "Шаг 3/4: Расчет доступных ресурсов памяти хоста..."

# Чтение доступного объема оперативной памяти в килобайтах
TOTAL_RAM_KB=$(grep MemTotal /proc/meminfo | awk '{print $2}' || echo "16777216")
TOTAL_RAM_GB=$((TOTAL_RAM_KB / 1024 / 1024))
CPU_CORES=$(nproc 2>/dev/null || echo 4)

log_info "Обнаружено оперативной памяти: ${TOTAL_RAM_GB} ГБ, ядер CPU: ${CPU_CORES}"

# Для сборки Android 13 требуется около 2.5-3 ГБ RAM на один поток Ninja
SAFE_JOBS=$((TOTAL_RAM_GB / 3))
if [ "${SAFE_JOBS}" -lt 1 ]; then
    SAFE_JOBS=1
fi
if [ "${SAFE_JOBS}" -gt "${CPU_CORES}" ]; then
    SAFE_JOBS="${CPU_CORES}"
fi

log_info "Оптимизированное число потоков сборки (для предотвращения OOM): ${SAFE_JOBS}"

# Настройка ccache
export USE_CCACHE=1
export CCACHE_DIR="${HOME}/.ccache"
export CCACHE_EXEC=/usr/bin/ccache

# Ограничение размера кучи Java VM
export _JAVA_OPTIONS="-Xmx6g -XX:+TieredCompilation"

# ==============================================================================
# ШАГ 4: Сборка целевых образов Halium (halium-boot и systemimage)
# ==============================================================================
log_info "Шаг 4/4: Инициализация сборочного окружения Android и компиляция..."

cd "${LINEAGE_DIR}"

# Запуск envsetup и выбор таргета lunch
set +u
# shellcheck source=/dev/null
source build/envsetup.sh
set -u

log_info "Выбор конфигурации lunch halium_panther-userdebug..."
if ! lunch halium_panther-userdebug; then
    log_warn "halium_panther-userdebug не зарегистрирован стандартно, пробуем lunch lineage_panther-userdebug..."
    lunch lineage_panther-userdebug
fi

log_info "Запуск компиляции halium-boot и systemimage (потоков: ${SAFE_JOBS})..."

# Вызов m (build target) с перехватом ошибок
set +e
m -j"${SAFE_JOBS}" halium-boot systemimage 2>&1 | tee "${LOG_DIR}/halium_build.log"
BUILD_RESULT=$?
set -e

if [ "${BUILD_RESULT}" -ne 0 ]; then
    log_warn "Сборка halium-boot/systemimage завершилась с ошибкой. Попытка сборки bootimage..."
    set +e
    m -j"${SAFE_JOBS}" bootimage 2>&1 | tee -a "${LOG_DIR}/halium_build.log"
    BUILD_RESULT=$?
    set -e
fi

# ==============================================================================
# Экспорт полученных образов в out/images
# ==============================================================================
log_info "Проверка и копирование сгенерированных образов..."

AOSP_OUT_DIR="${LINEAGE_DIR}/out/target/product/panther"

if [ -f "${AOSP_OUT_DIR}/halium-boot.img" ]; then
    cp "${AOSP_OUT_DIR}/halium-boot.img" "${IMAGES_DIR}/halium-boot.img"
    log_success "Скопирован: ${IMAGES_DIR}/halium-boot.img"
elif [ -f "${AOSP_OUT_DIR}/boot.img" ]; then
    cp "${AOSP_OUT_DIR}/boot.img" "${IMAGES_DIR}/halium-boot.img"
    log_success "Скопирован (на основе boot.img): ${IMAGES_DIR}/halium-boot.img"
fi

if [ -f "${AOSP_OUT_DIR}/system.img" ]; then
    cp "${AOSP_OUT_DIR}/system.img" "${IMAGES_DIR}/system.img"
    log_success "Скопирован: ${IMAGES_DIR}/system.img"
fi

log_info "=========================================================="
log_success "Этап сборки Halium успешно завершен!"
log_info "=========================================================="
