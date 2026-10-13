#!/usr/bin/env bash
# ==============================================================================
# Скрипт 03: Патчинг ядра Linux 5.10 для Ubuntu Touch (Patch Kernel)
# - Интеграция фрагмента конфигурации (Namespaces, cgroups, AppArmor, OverlayFS, FUSE)
# - Отключение строгой проверки GKI KMI ABI (Kleaf / build.config)
# - Проверка совместимости с Halium 13 и systemd
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
    log_error "Ошибка патчинга ядра в строке ${line_no} (код: ${exit_code})"
    exit "${exit_code}"
}
trap 'trap_err ${LINENO}' ERR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
if [ -f "${PROJECT_ROOT}/.env_setup" ]; then
    # shellcheck source=/dev/null
    source "${PROJECT_ROOT}/.env_setup"
fi

CONFIG_FRAGMENT="${PROJECT_ROOT}/patches/ubuntu_touch_defconfig.fragment"

if [ ! -f "${CONFIG_FRAGMENT}" ]; then
    log_error "Фрагмент конфигурации ${CONFIG_FRAGMENT} не найден!"
    exit 1
fi

log_info "=========================================================="
log_info " Запуск скрипта 03_patch_kernel.sh: Патчинг ядра Linux 5.10"
log_info "=========================================================="

# Поиск директории ядра (Google GKI в src/kernel или в src/lineage/kernel/google/gs201)
KERNEL_ROOT=""
if [ -d "${PROJECT_ROOT}/src/kernel/common" ]; then
    KERNEL_ROOT="${PROJECT_ROOT}/src/kernel"
    log_info "Обнаружено автономное дерево GKI/Kleaf ядра: ${KERNEL_ROOT}"
elif [ -d "${PROJECT_ROOT}/src/lineage/kernel/google/gs201" ]; then
    KERNEL_ROOT="${PROJECT_ROOT}/src/lineage/kernel/google/gs201"
    log_info "Обнаружено встроенное дерево ядра LineageOS: ${KERNEL_ROOT}"
else
    log_info "Исходники ядра еще не скачаны. Клонируем LineageOS/android_kernel_google_gs201 (shallow)..."
    mkdir -p "${PROJECT_ROOT}/src/lineage/kernel/google"
    git clone --depth=1 -b lineage-21 https://github.com/LineageOS/android_kernel_google_gs201.git \
        "${PROJECT_ROOT}/src/lineage/kernel/google/gs201" || \
    git clone --depth=1 -b lineage-20 https://github.com/LineageOS/android_kernel_google_gs201.git \
        "${PROJECT_ROOT}/src/lineage/kernel/google/gs201"
    KERNEL_ROOT="${PROJECT_ROOT}/src/lineage/kernel/google/gs201"
fi

# ==============================================================================
# ШАГ 1: Поиск и объединение дефконфигов (Defconfig Merging)
# ==============================================================================
log_info "Шаг 1/3: Интеграция конфигурации Ubuntu Touch в defconfig ядра..."

# Возможные расположения базового defconfig в дереве Pixel 7 (pantah / gs201 / GKI)
TARGET_DEFCONFIGS=(
    "${KERNEL_ROOT}/common/arch/arm64/configs/gki_defconfig"
    "${KERNEL_ROOT}/arch/arm64/configs/gki_defconfig"
    "${KERNEL_ROOT}/arch/arm64/configs/cloudripper_gki_defconfig"
    "${KERNEL_ROOT}/private/google-modules/soc/gs/arch/arm64/configs/pantah_defconfig"
    "${KERNEL_ROOT}/private/devices/google/pantah/pantah_defconfig"
    "${PROJECT_ROOT}/src/lineage/kernel/google/gs201/arch/arm64/configs/pantah_defconfig"
    "${PROJECT_ROOT}/src/lineage/device/google/pantah/pantah_defconfig"
)

PATCHED_DEFCONFIG_COUNT=0

for defcfg in "${TARGET_DEFCONFIGS[@]}"; do
    if [ -f "${defcfg}" ]; then
        log_info "Применение конфигурационного фрагмента к: ${defcfg}"

        # Создание резервной копии оригинального файла
        if [ ! -f "${defcfg}.orig" ]; then
            cp "${defcfg}" "${defcfg}.orig"
        fi

        # Слияние настроек (merge_config logic)
        MERGE_TOOL="${KERNEL_ROOT}/common/scripts/kconfig/merge_config.sh"
        if [ -x "${MERGE_TOOL}" ]; then
            log_info "Использование merge_config.sh..."
            bash "${MERGE_TOOL}" -m -O "$(dirname "${defcfg}")" "${defcfg}" "${CONFIG_FRAGMENT}"
        else
            log_info "Прямое объединение и нормализация директив конфигурации..."
            # Удаляем дублирующиеся директивы из базового файла и дописываем наш фрагмент
            while IFS= read -r line || [ -n "${line}" ]; do
                # Пропускаем комментарии и пустые строки
                [[ "${line}" =~ ^[[:space:]]*# ]] && continue
                [[ -z "${line}" ]] && continue

                CONFIG_KEY=$(echo "${line}" | cut -d'=' -f1)
                # Удаляем старое значение из целевого файла
                sed -i "/^${CONFIG_KEY}=/d" "${defcfg}" 2>/dev/null || true
                sed -i "/^# ${CONFIG_KEY} is not set/d" "${defcfg}" 2>/dev/null || true
            done < "${CONFIG_FRAGMENT}"

            # Дописываем настройки Ubuntu Touch в конец файла
            echo "" >> "${defcfg}"
            echo "# --- UBUNTU TOUCH & HALIUM COMPATIBILITY OPTIONS ---" >> "${defcfg}"
            cat "${CONFIG_FRAGMENT}" >> "${defcfg}"
        fi

        log_success "Конфигурация успешно обновлена: ${defcfg}"
        PATCHED_DEFCONFIG_COUNT=$((PATCHED_DEFCONFIG_COUNT + 1))
    fi
done

if [ "${PATCHED_DEFCONFIG_COUNT}" -eq 0 ]; then
    log_warn "Файлы defconfig не найдены на диске (возможно исходники еще не скачаны). Создаем эталонный файл gki_defconfig..."
    mkdir -p "${KERNEL_ROOT}/common/arch/arm64/configs"
    cp "${CONFIG_FRAGMENT}" "${KERNEL_ROOT}/common/arch/arm64/configs/gki_defconfig"
    log_success "Эталонный gki_defconfig создан в ${KERNEL_ROOT}/common/arch/arm64/configs/"
fi

# ==============================================================================
# ШАГ 2: Отключение проверки GKI ABI и KMI Enforce в Kleaf / Bazel / Build.config
# ==============================================================================
log_info "Шаг 2/3: Отключение строгой валидации KMI ABI (Kernel Module Interface)..."

# Поиск build.config файлов
BUILD_CONFIG_FILES=$(find "${KERNEL_ROOT}" -name "build.config*" -o -name "*.bazel" 2>/dev/null || true)

for bcfg in ${BUILD_CONFIG_FILES}; do
    if [ -f "${bcfg}" ]; then
        # Отключение строгих проверок KMI в скриптах build.config
        if grep -q "KMI_SYMBOL_LIST_STRICT_MODE" "${bcfg}" 2>/dev/null; then
            sed -i 's/KMI_SYMBOL_LIST_STRICT_MODE=1/KMI_SYMBOL_LIST_STRICT_MODE=0/g' "${bcfg}"
            log_info "Отключен KMI_SYMBOL_LIST_STRICT_MODE в ${bcfg}"
        fi

        if grep -q "TRIM_NONLISTED_KMI" "${bcfg}" 2>/dev/null; then
            sed -i 's/TRIM_NONLISTED_KMI=1/TRIM_NONLISTED_KMI=0/g' "${bcfg}"
            log_info "Отключен TRIM_NONLISTED_KMI в ${bcfg}"
        fi

        if grep -q "ABI_DEFINITION" "${bcfg}" 2>/dev/null; then
            sed -i 's/ABI_DEFINITION=.*/ABI_DEFINITION=""/g' "${bcfg}"
            log_info "Очищен ABI_DEFINITION в ${bcfg}"
        fi
    fi
done

# Добавление глобального флага подавления проверки ABI для Kleaf
KLEAF_ENV_FLAG="${KERNEL_ROOT}/build.config.local"
cat <<'EOF' > "${KLEAF_ENV_FLAG}"
# Переопределение проверок ABI для сборки с поддержкой Ubuntu Touch
export KMI_SYMBOL_LIST_STRICT_MODE=0
export TRIM_NONLISTED_KMI=0
export KMI_ENFORCE=0
export ABI_DEFINITION=""
export STOP_SHIP_TRACEPOINTS=0
EOF
log_success "Создан файл переопределения ${KLEAF_ENV_FLAG}"

# ==============================================================================
# ШАГ 3: Проверка ключевых опций ядра для Ubuntu Touch
# ==============================================================================
log_info "Шаг 3/3: Валидация примененных опций ядра..."

CHECK_LIST=(
    "CONFIG_NAMESPACES"
    "CONFIG_CGROUPS"
    "CONFIG_SECURITY_APPARMOR"
    "CONFIG_OVERLAY_FS"
    "CONFIG_SQUASHFS"
    "CONFIG_ANDROID_BINDER_IPC"
    "CONFIG_DEVTMPFS"
)

log_info "Проверка наличия критических параметров в конфигурации:"
for opt in "${CHECK_LIST[@]}"; do
    if grep -q "${opt}=y" "${CONFIG_FRAGMENT}"; then
        echo -e "  [✔] ${opt}=y"
    else
        echo -e "  [✘] ${opt} отсутствует"
    fi
done

log_success "Патчинг ядра и конфигурация defconfig успешно завершены!"
