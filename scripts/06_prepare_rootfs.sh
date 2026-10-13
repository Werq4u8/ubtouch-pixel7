#!/usr/bin/env bash
# ==============================================================================
# Скрипт 06: Подготовка и сборка Ubuntu Touch Rootfs (Prepare Rootfs)
# - Загрузка официального arm64 rootfs UBports (Focal 20.04 с откатом на Xenial)
# - Распаковка и интеграция оверлеев устройства (rootfs/overlays/)
# - Конфигурация параметров экрана (GRID_UNIT_PX=18), сессии Lomiri и Wi-Fi
# - Интеграция скомпилированных модулей ядра 5.10
# - Упаковка в сжатый SquashFS образ (rootfs.img) и финальный zip-архив
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
    log_error "Ошибка подготовки rootfs в строке ${line_no} (код: ${exit_code})"
    exit "${exit_code}"
}
trap 'trap_err ${LINENO}' ERR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
if [ -f "${PROJECT_ROOT}/.env_setup" ]; then
    # shellcheck source=/dev/null
    source "${PROJECT_ROOT}/.env_setup"
fi

DOWNLOAD_DIR="${PROJECT_ROOT}/rootfs/download"
EXTRACT_DIR="${PROJECT_ROOT}/rootfs/extracted"
OVERLAYS_DIR="${PROJECT_ROOT}/rootfs/overlays"
IMAGES_DIR="${PROJECT_ROOT}/out/images"
LOG_DIR="${PROJECT_ROOT}/logs"

mkdir -p "${DOWNLOAD_DIR}" "${IMAGES_DIR}" "${LOG_DIR}"

log_info "=========================================================="
log_info " Запуск скрипта 06_prepare_rootfs.sh: Подготовка Ubuntu Touch Rootfs"
log_info "=========================================================="

# ==============================================================================
# ШАГ 1: Загрузка базового arm64 rootfs UBports
# ==============================================================================
log_info "Шаг 1/5: Проверка и загрузка архива Ubuntu Touch Rootfs (arm64)..."

# Официальные зеркала UBports CI
FOCAL_ROOTFS_URL="https://ci.ubports.com/job/rootfs/job/rootfs-focal-arm64/lastSuccessfulBuild/artifact/ubuntu-touch-android9plus-rootfs-arm64.tar.gz"
FOCAL_FALLBACK_URL="https://images.ubports.com/ubuntu-touch-focal-arm64.tar.gz"
XENIAL_ROOTFS_URL="https://ci.ubports.com/job/rootfs/job/rootfs-xenial-arm64/lastSuccessfulBuild/artifact/ubuntu-touch-android9plus-rootfs-arm64.tar.gz"

ROOTFS_ARCHIVE="${DOWNLOAD_DIR}/ubuntu-touch-rootfs-arm64.tar.gz"

download_rootfs() {
    log_info "Попытка загрузки Ubuntu Touch Focal (20.04) Rootfs..."
    if curl -fL --progress-bar "${FOCAL_ROOTFS_URL}" -o "${ROOTFS_ARCHIVE}"; then
        log_success "Focal rootfs успешно загружен."
        return 0
    fi

    log_warn "Не удалось загрузить с основного сервера Focal. Пробуем зеркало: ${FOCAL_FALLBACK_URL}"
    if curl -fL --progress-bar "${FOCAL_FALLBACK_URL}" -o "${ROOTFS_ARCHIVE}"; then
        log_success "Focal rootfs успешно загружен с зеркала."
        return 0
    fi

    log_warn "Focal недоступен, выполняем откат (fallback) на Xenial 16.04..."
    if curl -fL --progress-bar "${XENIAL_ROOTFS_URL}" -o "${ROOTFS_ARCHIVE}"; then
        log_success "Xenial rootfs успешно загружен."
        return 0
    fi

    log_error "Не удалось скачать rootfs ни с одного источника!"
    return 1
}

if [ ! -f "${ROOTFS_ARCHIVE}" ]; then
    download_rootfs
else
    log_info "Используется ранее загруженный архив: ${ROOTFS_ARCHIVE}"
fi

# ==============================================================================
# ШАГ 2: Распаковка rootfs
# ==============================================================================
log_info "Шаг 2/5: Распаковка базового rootfs..."

# Требуется очистить директорию для исключения конфликтов
if [ -d "${EXTRACT_DIR}" ]; then
    log_info "Очистка предыдущей распаковки ${EXTRACT_DIR}..."
    rm -rf "${EXTRACT_DIR}"
fi
mkdir -p "${EXTRACT_DIR}"

log_info "Извлечение архива (может занять 1-2 минуты)..."
tar -xzf "${ROOTFS_ARCHIVE}" -C "${EXTRACT_DIR}"
log_success "Базовый rootfs успешно распакован."

# ==============================================================================
# ШАГ 3: Наложение оверлеев Pixel 7 (overlays)
# ==============================================================================
log_info "Шаг 3/5: Интеграция конфигурационных файлов для Pixel 7..."

if [ -d "${OVERLAYS_DIR}" ]; then
    log_info "Копирование оверлеев из ${OVERLAYS_DIR} в корень rootfs..."
    cp -r "${OVERLAYS_DIR}/"* "${EXTRACT_DIR}/" 2>/dev/null || true
fi

# Настройка имени хоста
echo "pixel7-ubtouch" > "${EXTRACT_DIR}/etc/hostname"

# Настройка параметров сессии Lomiri / Wayland
mkdir -p "${EXTRACT_DIR}/etc/ubuntu-touch-session.d"
cat <<'EOF' > "${EXTRACT_DIR}/etc/ubuntu-touch-session.d/panther.conf"
# Pixel 7 display parameters
GRID_UNIT_PX=18
QT_QPA_PLATFORM=ubuntumirclient
FORM_FACTOR=phone
NATIVE_ORIENTATION=0
PRIMARY_MONITOR=DSI-1
EOF

# Настройка Wi-Fi и сети
mkdir -p "${EXTRACT_DIR}/etc/NetworkManager/conf.d"
cat <<'EOF' > "${EXTRACT_DIR}/etc/NetworkManager/conf.d/10-panther-wifi.conf"
[keyfile]
unmanaged-devices=interface-name:p2p*;interface-name:dummy*;interface-name:rmnet*
[device]
wifi.scan-rand-mac-address=no
wifi.backend=wpa_supplicant
EOF

# ==============================================================================
# ШАГ 4: Интеграция модулей ядра 5.10
# ==============================================================================
log_info "Шаг 4/5: Интеграция модулей ядра в rootfs..."

MODULES_ARCHIVE="${IMAGES_DIR}/pixel7-kernel-modules.tar.gz"
if [ -f "${MODULES_ARCHIVE}" ]; then
    log_info "Распаковка модулей ядра из ${MODULES_ARCHIVE}..."
    tar -xzf "${MODULES_ARCHIVE}" -C "${EXTRACT_DIR}"
    log_success "Модули ядра успешно добавлены в /lib/modules."
else
    log_warn "Архив модулей ядра не найден (${MODULES_ARCHIVE}). Пропускаем интеграцию модулей."
fi

# ==============================================================================
# ШАГ 5: Сборка SquashFS образа rootfs.img и финальная упаковка
# ==============================================================================
log_info "Шаг 5/5: Упаковка rootfs в SquashFS (rootfs.img)..."

ROOTFS_IMG_OUT="${IMAGES_DIR}/rootfs.img"
rm -f "${ROOTFS_IMG_OUT}"

if ! command -v mksquashfs >/dev/null 2>&1; then
    log_error "Утилита mksquashfs не установлена! Запустите 01_setup_env.sh"
    exit 1
fi

log_info "Выполнение сжатия mksquashfs (алгоритм xz)..."
mksquashfs "${EXTRACT_DIR}" "${ROOTFS_IMG_OUT}" \
    -comp xz \
    -Xbcj arm \
    -b 1048576 \
    -noappend \
    -processors "$(nproc)"

log_success "Образ rootfs.img успешно создан: ${ROOTFS_IMG_OUT}"

# Проверка наличия всех ключевых образов и сборка общего ZIP-архива
FINAL_ZIP="${IMAGES_DIR}/pixel7-ubtouch-images.zip"
log_info "Создание финального архива образов для прошивки: ${FINAL_ZIP}..."

cd "${IMAGES_DIR}"
zip -j "${FINAL_ZIP}" \
    "${ROOTFS_IMG_OUT}" \
    boot.img \
    halium-boot.img \
    system.img 2>/dev/null || {
        log_warn "Некоторые дополнительные образы еще не собраны. Упаковываем имеющиеся файлы..."
        zip -j "${FINAL_ZIP}" "${ROOTFS_IMG_OUT}" *.img 2>/dev/null || true
    }

log_info "=========================================================="
log_success "Rootfs успешно подготовлен!"
log_info "Готовый образ: ${ROOTFS_IMG_OUT}"
log_info "Финальный архив для прошивки: ${FINAL_ZIP}"
log_info "=========================================================="
