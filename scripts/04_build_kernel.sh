#!/usr/bin/env bash
# ==============================================================================
# Скрипт 04: Компиляция ядра Linux 5.10 для Pixel 7 (Build Kernel)
# - Попытка сборки через систему Kleaf / Bazel (рекомендуемый Google метод)
# - Резервный откат (fallback) на build/build.sh или традиционный Clang/Make
# - Сборка ядра, dtb, dtbo и модулей (.ko)
# - Генерация boot.img (Header v4) и упаковка модулей в архив
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
    log_error "Ошибка сборки ядра в строке ${line_no} (код: ${exit_code})"
    exit "${exit_code}"
}
trap 'trap_err ${LINENO}' ERR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
if [ -f "${PROJECT_ROOT}/.env_setup" ]; then
    # shellcheck source=/dev/null
    source "${PROJECT_ROOT}/.env_setup"
fi

KERNEL_SRC_DIR="${PROJECT_ROOT}/src/kernel"
LINEAGE_KERNEL_DIR="${PROJECT_ROOT}/src/lineage/kernel/google/gs201"
OUTPUT_DIR="${PROJECT_ROOT}/out/kernel"
IMAGES_DIR="${PROJECT_ROOT}/out/images"
LOG_DIR="${PROJECT_ROOT}/logs"

mkdir -p "${OUTPUT_DIR}" "${IMAGES_DIR}" "${LOG_DIR}"

log_info "=========================================================="
log_info " Запуск скрипта 04_build_kernel.sh: Сборка ядра Linux 5.10"
log_info "=========================================================="

# Определение числа ядер процессора
BUILD_JOBS=$(nproc 2>/dev/null || echo 4)
log_info "Используется параллельных потоков компиляции: ${BUILD_JOBS}"

# Экспорт переменных сборки KMI
export KMI_SYMBOL_LIST_STRICT_MODE=0
export TRIM_NONLISTED_KMI=0
export KMI_ENFORCE=0
export ABI_DEFINITION=""

BUILD_SUCCESS=0

# ==============================================================================
# МЕТОД 1: Сборка через Google Kleaf / Bazel
# ==============================================================================
if [ -d "${KERNEL_SRC_DIR}" ] && [ -f "${KERNEL_SRC_DIR}/tools/bazel" ]; then
    log_info "Обнаружен Kleaf/Bazel в ${KERNEL_SRC_DIR}. Попытка сборки через Bazel..."
    cd "${KERNEL_SRC_DIR}"

    # Kleaf таргет для Pixel 7 (pantah / gs201)
    KLEAF_TARGET="//private/devices/google/pantah:pantah_dist"

    set +e
    tools/bazel run --config=fast \
        --kmi_symbol_list_strict_mode=0 \
        "${KLEAF_TARGET}" \
        -- --dist_dir="${OUTPUT_DIR}" 2>&1 | tee "${LOG_DIR}/kernel_kleaf_build.log"
    BAZEL_STATUS=$?
    set -e

    if [ "${BAZEL_STATUS}" -eq 0 ] && [ -f "${OUTPUT_DIR}/Image.lz4" -o -f "${OUTPUT_DIR}/Image" ]; then
        log_success "Ядро успешно скомпилировано с помощью Kleaf/Bazel!"
        BUILD_SUCCESS=1
    else
        log_warn "Сборка через Kleaf/Bazel завершилась с ошибкой (код: ${BAZEL_STATUS}). Переход к запасному методу..."
    fi
fi

# ==============================================================================
# МЕТОД 2: Сборка через стандартный build/build.sh скрипт AOSP
# ==============================================================================
if [ "${BUILD_SUCCESS}" -eq 0 ] && [ -f "${KERNEL_SRC_DIR}/build/build.sh" ]; then
    log_info "Попытка сборки через ${KERNEL_SRC_DIR}/build/build.sh..."
    cd "${KERNEL_SRC_DIR}"

    export DIST_DIR="${OUTPUT_DIR}"
    export BUILD_CONFIG="${KERNEL_SRC_DIR}/private/devices/google/pantah/build.config.pantah"
    [ ! -f "${BUILD_CONFIG}" ] && BUILD_CONFIG="build.config.gki.aarch64"

    set +e
    env KMI_SYMBOL_LIST_STRICT_MODE=0 \
        TRIM_NONLISTED_KMI=0 \
        ABI_DEFINITION="" \
        bash build/build.sh 2>&1 | tee "${LOG_DIR}/kernel_build_sh.log"
    BUILD_SH_STATUS=$?
    set -e

    if [ "${BUILD_SH_STATUS}" -eq 0 ]; then
        log_success "Ядро успешно скомпилировано через build/build.sh!"
        BUILD_SUCCESS=1
    else
        log_warn "Сборка через build/build.sh завершилась с кодом ${BUILD_SH_STATUS}."
    fi
fi

# ==============================================================================
# МЕТОД 3: Традиционная компиляция ядра Make / Clang (Fallback)
# ==============================================================================
if [ "${BUILD_SUCCESS}" -eq 0 ]; then
    log_info "Попытка прямой компиляции ядра с помощью Clang/LLVM и Make..."

    SOURCE_TREE=""
    if [ -d "${KERNEL_SRC_DIR}/common" ]; then
        SOURCE_TREE="${KERNEL_SRC_DIR}/common"
    elif [ -d "${LINEAGE_KERNEL_DIR}" ]; then
        SOURCE_TREE="${LINEAGE_KERNEL_DIR}"
    else
        log_error "Исходный код ядра не обнаружен ни в ${KERNEL_SRC_DIR}, ни в ${LINEAGE_KERNEL_DIR}!"
        exit 1
    fi

    cd "${SOURCE_TREE}"

    # Настройка тулчейна Clang
    export ARCH=arm64
    export SUBARCH=arm64
    export CROSS_COMPILE=aarch64-linux-gnu-
    export CROSS_COMPILE_COMPAT=arm-linux-gnueabi-

    if command -v clang >/dev/null 2>&1; then
        export CC=clang
        export LD=ld.lld
        export AR=llvm-ar
        export NM=llvm-nm
        export OBJCOPY=llvm-objcopy
        export OBJDUMP=llvm-objdump
        export READELF=llvm-readelf
        export STRIP=llvm-strip
        export LLVM=1
        export LLVM_IAS=1
    fi

    DEFCONFIG_NAME="gki_defconfig"
    if [ -f "arch/arm64/configs/cloudripper_gki_defconfig" ]; then
        DEFCONFIG_NAME="cloudripper_gki_defconfig"
    fi
    log_info "Генерация конфигурации: make ${DEFCONFIG_NAME}..."
    make O="${OUTPUT_DIR}" "${DEFCONFIG_NAME}"

    # Убедимся, что опции не приводят к ошибкам Werror на новых версиях Clang
    scripts/config --file "${OUTPUT_DIR}/.config" -d CONFIG_WERROR -d CONFIG_LTO_CLANG_THIN -d CONFIG_LTO_CLANG_FULL || true
    make O="${OUTPUT_DIR}" olddefconfig

    log_info "Компиляция Image.lz4, Image и модулей (потоков: ${BUILD_JOBS})..."
    make -j"${BUILD_JOBS}" O="${OUTPUT_DIR}" Image.lz4 Image modules 2>&1 | tee "${LOG_DIR}/kernel_make.log"

    log_info "Установка модулей ядра..."
    make O="${OUTPUT_DIR}" INSTALL_MOD_PATH="${OUTPUT_DIR}/modules_install" modules_install || true

    BUILD_SUCCESS=1
fi

# ==============================================================================
# УПАКОВКА И ГЕНЕРАЦИЯ АРТИФАКТОВ СБОРКИ
# ==============================================================================
log_info "=========================================================="
log_info " Упаковка ядра, модулей и создание boot.img для Pixel 7"
log_info "=========================================================="

KERNEL_BIN=""
for candidate in \
    "${OUTPUT_DIR}/arch/arm64/boot/Image.lz4" \
    "${OUTPUT_DIR}/arch/arm64/boot/Image" \
    "${OUTPUT_DIR}/Image.lz4" \
    "${OUTPUT_DIR}/Image" \
    "${KERNEL_SRC_DIR}/out/dist/Image.lz4" \
    "${KERNEL_SRC_DIR}/out/dist/Image"; do
    if [ -f "${candidate}" ]; then
        KERNEL_BIN="${candidate}"
        break
    fi
done

if [ -z "${KERNEL_BIN}" ] || [ ! -f "${KERNEL_BIN}" ]; then
    log_error "Бинарный файл ядра (Image / Image.lz4) не найден в директории вывода!"
    exit 1
fi

log_success "Обнаружен бинарный файл ядра: ${KERNEL_BIN}"
cp "${KERNEL_BIN}" "${IMAGES_DIR}/kernel-panther"

# Поиск и архивация модулей ядра (.ko)
MODULES_ARCHIVE="${IMAGES_DIR}/pixel7-kernel-modules.tar.gz"
log_info "Поиск скомпилированных модулей ядра (*.ko)..."
MODULE_COUNT=$(find "${OUTPUT_DIR}" -name "*.ko" | wc -l)
log_info "Найдено модулей ядра: ${MODULE_COUNT}"

MODULES_STAGING="${OUTPUT_DIR}/staging_modules"
rm -rf "${MODULES_STAGING}"
mkdir -p "${MODULES_STAGING}/lib/modules/5.10-gki-pantah"

find "${OUTPUT_DIR}" -name "*.ko" -exec cp {} "${MODULES_STAGING}/lib/modules/5.10-gki-pantah/" \; 2>/dev/null || true

tar -czf "${MODULES_ARCHIVE}" -C "${MODULES_STAGING}" .
log_success "Архив модулей создан: ${MODULES_ARCHIVE}"

# Сборка интерактивного initramfs с поддержкой запроса "PLEASE, INSERT UBUNTU-FILE.IMG"
log_info "Формирование интерактивного initramfs (Halium + USB Loader)..."
RAMDISK_WORK="${OUTPUT_DIR}/ramdisk_root"
rm -rf "${RAMDISK_WORK}"
mkdir -p "${RAMDISK_WORK}"/{bin,sbin,etc,proc,sys,dev,config,tmpmnt,halium,data/ubuntu,system/bin}

if [ -f "${PROJECT_ROOT}/device/panther/usb-insert-hook.sh" ]; then
    cp "${PROJECT_ROOT}/device/panther/usb-insert-hook.sh" "${RAMDISK_WORK}/system/bin/usb-insert-hook.sh"
    chmod +x "${RAMDISK_WORK}/system/bin/usb-insert-hook.sh"
fi

cat << 'INIT_EOF' > "${RAMDISK_WORK}/init"
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
if [ -x /system/bin/usb-insert-hook.sh ]; then
    /system/bin/usb-insert-hook.sh
fi
exec /bin/sh
INIT_EOF
chmod +x "${RAMDISK_WORK}/init"

CUSTOM_RAMDISK="${OUTPUT_DIR}/halium_ramdisk.cpio.gz"
(cd "${RAMDISK_WORK}" && find . -print0 | cpio --null -ov --format=newc 2>/dev/null | gzip -9 > "${CUSTOM_RAMDISK}")

# Сборка boot.img (Header Version 4 для Google Pixel 7 / GKI)
log_info "Формирование загрузочного образа boot.img (v4 GKI)..."

MKBOOTIMG_TOOL=""
if command -v mkbootimg >/dev/null 2>&1; then
    MKBOOTIMG_TOOL="mkbootimg"
elif [ -f "${PROJECT_ROOT}/src/lineage/out/host/linux-x86/bin/mkbootimg" ]; then
    MKBOOTIMG_TOOL="${PROJECT_ROOT}/src/lineage/out/host/linux-x86/bin/mkbootimg"
elif [ -f "${PROJECT_ROOT}/src/lineage/system/tools/mkbootimg/mkbootimg.py" ]; then
    MKBOOTIMG_TOOL="python3 ${PROJECT_ROOT}/src/lineage/system/tools/mkbootimg/mkbootimg.py"
fi

BOOT_IMG_OUT="${IMAGES_DIR}/boot.img"

if [ -n "${MKBOOTIMG_TOOL}" ]; then
    log_info "Выполнение сборки boot.img через ${MKBOOTIMG_TOOL}..."
    ${MKBOOTIMG_TOOL} \
        --kernel "${KERNEL_BIN}" \
        --ramdisk "${CUSTOM_RAMDISK}" \
        --header_version 4 \
        --os_version 13.0.0 \
        --os_patch_level 2023-08 \
        --cmdline "console=tty0 console=ttySAC0,115200 androidboot.selinux=permissive" \
        --output "${BOOT_IMG_OUT}" || \
    ${MKBOOTIMG_TOOL} \
        --kernel "${KERNEL_BIN}" \
        --ramdisk "${CUSTOM_RAMDISK}" \
        --header_version 2 \
        --pagesize 4096 \
        --output "${BOOT_IMG_OUT}"
    cp "${BOOT_IMG_OUT}" "${IMAGES_DIR}/halium-boot.img"
    log_success "Образ ядра успешно сформирован: ${BOOT_IMG_OUT} (и halium-boot.img)"
else
    log_warn "Утилита mkbootimg не найдена в системе. Будет упакован образ ядра Image.lz4."
    cp "${KERNEL_BIN}" "${IMAGES_DIR}/boot.img"
    cp "${KERNEL_BIN}" "${IMAGES_DIR}/halium-boot.img"
fi

# Создание архива всех артифактов ядра
KERNEL_BUNDLE="${IMAGES_DIR}/pixel7-kernel-bundle.tar.gz"
tar -czf "${KERNEL_BUNDLE}" -C "${IMAGES_DIR}" boot.img halium-boot.img pixel7-kernel-modules.tar.gz
log_success "Все артифакты ядра успешно упакованы в: ${KERNEL_BUNDLE}"
