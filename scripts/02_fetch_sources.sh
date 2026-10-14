#!/usr/bin/env bash
# ==============================================================================
# Скрипт 02: Загрузка исходных кодов (Fetch Sources)
# - LineageOS 20 (Android 13)
# - Device trees: panther, pantah, gs201, gs-common
# - Vendor blobs: TheMuppets (panther, pantah, gs201)
# - Google Kernel (Linux 5.10 GKI / Kleaf / android-gs-pantah-5.10-android13)
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
    log_error "Ошибка выполнения в строке ${line_no} (код: ${exit_code})"
    exit "${exit_code}"
}
trap 'trap_err ${LINENO}' ERR

# Загрузка переменных окружения, если файл существует
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
if [ -f "${PROJECT_ROOT}/.env_setup" ]; then
    # shellcheck source=/dev/null
    source "${PROJECT_ROOT}/.env_setup"
fi

LINEAGE_DIR="${PROJECT_ROOT}/src/lineage"
KERNEL_DIR="${PROJECT_ROOT}/src/kernel"
MANIFEST_SRC="${PROJECT_ROOT}/local_manifests/pixel7_panther.xml"

# Определение числа потоков процессора для параллельной загрузки
THREADS=$(nproc 2>/dev/null || echo 4)
log_info "Количество потоков для загрузки: ${THREADS}"

# Проверка наличия утилиты repo
if ! command -v repo >/dev/null 2>&1; then
    if [ -x "${HOME}/.local/bin/repo" ]; then
        export PATH="${HOME}/.local/bin:${PATH}"
    else
        log_error "Утилита 'repo' не найдена в PATH! Выполните сначала scripts/01_setup_env.sh"
        exit 1
    fi
fi

# ==============================================================================
# ЧАСТЬ 1: Инициализация и синхронизация LineageOS 20 (AOSP 13)
# ==============================================================================
log_info "=========================================================="
log_info " Шаг 1/2: Синхронизация дерева LineageOS 20 и Device Trees"
log_info "=========================================================="

mkdir -p "${LINEAGE_DIR}"
cd "${LINEAGE_DIR}"

if [ ! -d "${LINEAGE_DIR}/.repo" ]; then
    log_info "Инициализация репозитория LineageOS 20 (ветка: lineage-20.0, глубина: 1)..."
    repo init \
        -u https://github.com/LineageOS/android.git \
        -b lineage-20.0 \
        --git-lfs \
        --depth=1
    log_success "Repo успешно инициализирован."
else
    log_info "Каталог .repo уже существует, продолжаем обновление дерева..."
fi

# Настройка local_manifests для Pixel 7 (panther, pantah, gs201, vendor blobs)
LOCAL_MANIFEST_DIR="${LINEAGE_DIR}/.repo/local_manifests"
mkdir -p "${LOCAL_MANIFEST_DIR}"

if [ -f "${MANIFEST_SRC}" ]; then
    log_info "Копирование локального манифеста ${MANIFEST_SRC} -> ${LOCAL_MANIFEST_DIR}/pixel7_panther.xml"
    cp "${MANIFEST_SRC}" "${LOCAL_MANIFEST_DIR}/pixel7_panther.xml"
else
    log_warn "Файл ${MANIFEST_SRC} не найден! Создаем манифест 'на лету'..."
    cat <<'EOF' > "${LOCAL_MANIFEST_DIR}/pixel7_panther.xml"
<?xml version="1.0" encoding="UTF-8"?>
<manifest>
    <remove-project name="device/google/gs-common" />

    <project name="LineageOS/android_device_google_panther" path="device/google/panther" remote="github" revision="lineage-20.0" />
    <project name="LineageOS/android_device_google_pantah" path="device/google/pantah" remote="github" revision="lineage-20.0" />
    <project name="LineageOS/android_device_google_gs201" path="device/google/gs201" remote="github" revision="lineage-20.0" />
    <project name="LineageOS/android_device_google_gs-common" path="device/google/gs-common" remote="github" revision="lineage-20.0" />
    <project name="LineageOS/android_kernel_google_gs201" path="kernel/google/gs201" remote="github" revision="lineage-20.0" />

    <project name="TheMuppets/proprietary_vendor_google_panther" path="vendor/google/panther" remote="github" revision="lineage-20.0" />
    <project name="TheMuppets/proprietary_vendor_google_pantah" path="vendor/google/pantah" remote="github" revision="lineage-20.0" />
    <project name="TheMuppets/proprietary_vendor_google_gs201" path="vendor/google/gs201" remote="github" revision="lineage-20.0" />
</manifest>
EOF
fi

log_info "Запуск синхронизации исходных кодов LineageOS (repo sync)..."
log_info "Параметры: --force-sync -c -j${THREADS} --no-clone-bundle --no-tags"

# Функция синхронизации с повторными попытками при сбоях сети
sync_with_retry() {
    local max_attempts=3
    local attempt=1
    local success=0

    while [ "${attempt}" -le "${max_attempts}" ]; do
        log_info "Попытка синхронизации ${attempt}/${max_attempts}..."
        if repo sync -c --force-sync --optimized-fetch --no-clone-bundle --no-tags -j"${THREADS}"; then
            success=1
            break
        else
            log_warn "Синхронизация завершилась с ошибкой. Повторная попытка через 10 секунд..."
            sleep 10
            attempt=$((attempt + 1))
        fi
    done

    if [ "${success}" -ne 1 ]; then
        log_error "Не удалось выполнить repo sync после ${max_attempts} попыток!"
        return 1
    fi
}

sync_with_retry
log_success "Исходные коды LineageOS 20 успешно синхронизированы."

# ==============================================================================
# ЧАСТЬ 2: Загрузка исходных кодов ядра Google Pantah (Linux 5.10 GKI / Kleaf)
# ==============================================================================
log_info "=========================================================="
log_info " Шаг 2/2: Загрузка исходных кодов ядра Linux 5.10 для Pixel 7"
log_info "=========================================================="

mkdir -p "${KERNEL_DIR}"
cd "${KERNEL_DIR}"

KERNEL_BRANCH="android-gs-pantah-5.10-android14"

if [ ! -d "${KERNEL_DIR}/.repo" ]; then
    log_info "Инициализация репозитория ядра Google Pantah (${KERNEL_BRANCH})..."
    repo init \
        -u https://android.googlesource.com/kernel/manifest \
        -b "${KERNEL_BRANCH}" \
        --depth=1
fi

log_info "Синхронизация исходного кода ядра и сборочной инфраструктуры Kleaf..."
sync_with_retry
log_success "Исходный код ядра Google Tensor G2 успешно загружен в ${KERNEL_DIR}"

log_info "=========================================================="
log_success "Все исходные коды успешно загружены и готовы к патчингу!"
log_info "=========================================================="
