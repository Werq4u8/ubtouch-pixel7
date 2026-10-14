#!/bin/bash
# ============================================================================
# extract_vendor_blobs.sh — Извлечение vendor blobs с рутованного Pixel 7
# ============================================================================
# Вы первый кто это делает. Нужны РЕАЛЬНЫЕ блобы с вашего устройства,
# потому что Android 16 vendor blobs нигде в открытом доступе нет.
# ============================================================================
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

WORKDIR="$(cd "$(dirname "$0")/.." && pwd)"
BLOB_DIR="$WORKDIR/vendor_blobs"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"

info()    { echo -e "${BLUE}[ИНФО]${NC} $*"; }
success() { echo -e "${GREEN}[  ✅ ]${NC} $*"; }
warn()    { echo -e "${YELLOW}[  ⚠️ ]${NC} $*"; }
error()   { echo -e "${RED}[  ❌ ]${NC} $*"; exit 1; }

echo -e "${CYAN}"
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  🔬 ИЗВЛЕЧЕНИЕ VENDOR BLOBS С PIXEL 7 (Android 16 + Root)  ║"
echo "║                                                              ║"
echo "║  Эти блобы — ключ ко всему. Без них ничего не заработает.   ║"
echo "║  Ваш рут позволяет вытащить ВСЁ что нужно.                  ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

# --- Проверки ---
if ! command -v adb &>/dev/null; then
    error "adb не найден"
fi

if ! adb devices | grep -q "device$"; then
    error "Устройство не подключено"
fi

# Проверка root
ROOT_CHECK=$(adb shell "su -c 'id'" 2>/dev/null || echo "no_root")
if [[ "$ROOT_CHECK" != *"uid=0"* ]]; then
    error "Root не доступен. Проверьте Magisk/KernelSU и разрешите shell root."
fi
success "Root доступ подтверждён"

# Информация об устройстве
ANDROID_VER=$(adb shell getprop ro.build.version.release 2>/dev/null)
DEVICE_NAME=$(adb shell getprop ro.product.device 2>/dev/null)
BUILD_ID=$(adb shell getprop ro.build.display.id 2>/dev/null)
KERNEL_VER=$(adb shell uname -r 2>/dev/null)

info "Устройство: $DEVICE_NAME"
info "Android: $ANDROID_VER"
info "Сборка: $BUILD_ID"
info "Ядро: $KERNEL_VER"

mkdir -p "$BLOB_DIR"

# ═══════════════════════════════════════════════════════
# 1. Дамп полного vendor раздела (RAW)
# ═══════════════════════════════════════════════════════
echo ""
info "📦 [1/8] Дамп vendor раздела (raw image)..."
VENDOR_BLOCK=$(adb shell "su -c 'readlink -f /dev/block/by-name/vendor'" 2>/dev/null | tr -d '\r\n')
if [ -n "$VENDOR_BLOCK" ]; then
    # Используем exec-out вместо shell, чтобы не повредить бинарный поток символами CRLF (\r\n)
    adb exec-out "su -c 'dd if=$VENDOR_BLOCK bs=4M'" > "$BLOB_DIR/vendor.img" 2>/dev/null
    VENDOR_SIZE=$(du -h "$BLOB_DIR/vendor.img" | cut -f1)
    success "vendor.img сохранён ($VENDOR_SIZE)"
else
    warn "Не удалось определить vendor раздел, пробуем через /vendor"
fi

# ═══════════════════════════════════════════════════════
# 2. Копирование файлов из /vendor
# ═══════════════════════════════════════════════════════
info "📦 [2/8] Копирование /vendor/lib64 (HAL библиотеки)..."
mkdir -p "$BLOB_DIR/vendor"
adb exec-out "su -c 'tar cf - /vendor'" 2>/dev/null | tar xf - -C "$BLOB_DIR/" 2>/dev/null && \
    success "/vendor скопирован" || {
    warn "tar не сработал, копируем по частям..."
    for dir in lib lib64 etc firmware bin; do
        info "  Копируем /vendor/$dir..."
        adb shell "su -c 'ls /vendor/$dir'" &>/dev/null && \
            adb pull "/vendor/$dir" "$BLOB_DIR/vendor/$dir" 2>/dev/null || \
            warn "  /vendor/$dir не доступен"
    done
}

# ═══════════════════════════════════════════════════════
# 3. HAL-сервисы (AIDL) — самое важное!
# ═══════════════════════════════════════════════════════
info "📦 [3/8] Извлечение AIDL HAL сервисов..."
mkdir -p "$BLOB_DIR/hal_services"

# Список запущенных HAL-сервисов
adb shell "su -c 'service list'" > "$BLOB_DIR/hal_services/service_list.txt" 2>/dev/null
adb shell "su -c 'dumpsys -l'" > "$BLOB_DIR/hal_services/dumpsys_list.txt" 2>/dev/null

# VINTF (Vendor Interface) — описание всех HAL
adb shell "su -c 'cat /vendor/etc/vintf/manifest.xml'" > "$BLOB_DIR/hal_services/vendor_manifest.xml" 2>/dev/null
adb shell "su -c 'cat /vendor/etc/vintf/compatibility_matrix.xml'" > "$BLOB_DIR/hal_services/vendor_compat_matrix.xml" 2>/dev/null

# Framework manifest
adb shell "su -c 'cat /system/etc/vintf/manifest.xml'" > "$BLOB_DIR/hal_services/system_manifest.xml" 2>/dev/null

# Все AIDL-сервисы
adb shell "su -c 'dumpsys -l'" 2>/dev/null | grep -i "android.hardware" > "$BLOB_DIR/hal_services/aidl_services.txt" 2>/dev/null

success "HAL-сервисы извлечены"

# ═══════════════════════════════════════════════════════
# 4. Firmware — прошивки чипов (Wi-Fi, модем, GPU и т.д.)
# ═══════════════════════════════════════════════════════
info "📦 [4/8] Извлечение firmware..."
mkdir -p "$BLOB_DIR/firmware"

# Wi-Fi BCM4389 firmware
for fw_path in /vendor/firmware /vendor/firmware/bcm4389 /lib/firmware; do
    adb shell "su -c 'ls $fw_path'" &>/dev/null && {
        adb exec-out "su -c 'tar cf - $fw_path'" 2>/dev/null | tar xf - -C "$BLOB_DIR/firmware/" 2>/dev/null
        success "Firmware из $fw_path скопирован"
    }
done

# Modem firmware (Shannon)
for modem_path in /vendor/firmware_mnt /vendor/modem /dev/block/by-name/modem; do
    adb shell "su -c 'ls $modem_path'" &>/dev/null && {
        info "  Найден modem firmware: $modem_path"
        adb exec-out "su -c 'tar cf - $modem_path'" 2>/dev/null | tar xf - -C "$BLOB_DIR/firmware/" 2>/dev/null
    }
done

success "Firmware извлечены"

# ═══════════════════════════════════════════════════════
# 5. Конфигурация ядра (текущая)
# ═══════════════════════════════════════════════════════
info "📦 [5/8] Извлечение конфигурации ядра..."

adb exec-out "su -c 'cat /proc/config.gz'" 2>/dev/null | gunzip > "$BLOB_DIR/current_kernel_config" 2>/dev/null && \
    success "Конфиг ядра извлечён" || {
    adb shell "su -c 'zcat /proc/config.gz'" > "$BLOB_DIR/current_kernel_config" 2>/dev/null && \
        success "Конфиг ядра извлечён (zcat)" || \
        warn "config.gz недоступен"
}


# Текущие модули ядра
adb shell "su -c 'find /vendor/lib/modules -name *.ko'" > "$BLOB_DIR/kernel_modules_list.txt" 2>/dev/null
adb shell "su -c 'lsmod'" > "$BLOB_DIR/loaded_modules.txt" 2>/dev/null

# ═══════════════════════════════════════════════════════
# 6. Разметка разделов
# ═══════════════════════════════════════════════════════
info "📦 [6/8] Извлечение разметки разделов..."

adb shell "su -c 'ls -la /dev/block/by-name/'" > "$BLOB_DIR/partition_layout.txt" 2>/dev/null
adb shell "su -c 'cat /proc/partitions'" >> "$BLOB_DIR/partition_layout.txt" 2>/dev/null
adb shell "su -c 'df -h'" > "$BLOB_DIR/disk_usage.txt" 2>/dev/null
adb shell "su -c 'mount'" > "$BLOB_DIR/mount_points.txt" 2>/dev/null

# Fstab
adb shell "su -c 'cat /vendor/etc/fstab.gs201'" > "$BLOB_DIR/fstab.gs201" 2>/dev/null || \
    adb shell "su -c 'find /vendor/etc -name fstab*' -exec cat {} \;" > "$BLOB_DIR/fstab_all.txt" 2>/dev/null

success "Разметка разделов сохранена"

# ═══════════════════════════════════════════════════════
# 7. Init-скрипты (как Android загружает HAL)
# ═══════════════════════════════════════════════════════
info "📦 [7/8] Извлечение init-скриптов..."
mkdir -p "$BLOB_DIR/init_scripts"

adb shell "su -c 'find /vendor/etc/init -name *.rc'" 2>/dev/null | while read -r rc_file; do
    local_name=$(echo "$rc_file" | tr '/' '_')
    adb shell "su -c 'cat $rc_file'" > "$BLOB_DIR/init_scripts/$local_name" 2>/dev/null
done
success "Init-скрипты извлечены"

# ═══════════════════════════════════════════════════════
# 8. SELinux и prop-файлы
# ═══════════════════════════════════════════════════════
info "📦 [8/8] Извлечение SELinux и системных свойств..."

adb shell "su -c 'getprop'" > "$BLOB_DIR/all_properties.txt" 2>/dev/null
adb shell "su -c 'getenforce'" > "$BLOB_DIR/selinux_status.txt" 2>/dev/null
adb shell "su -c 'cat /vendor/etc/selinux/vendor_property_contexts'" > "$BLOB_DIR/vendor_property_contexts.txt" 2>/dev/null

success "SELinux и свойства сохранены"

# ═══════════════════════════════════════════════════════
# Итоги
# ═══════════════════════════════════════════════════════
TOTAL_SIZE=$(du -sh "$BLOB_DIR" | cut -f1)

echo ""
echo -e "${CYAN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║${NC}  ${GREEN}✅ ИЗВЛЕЧЕНИЕ ЗАВЕРШЕНО!${NC}                                    ${CYAN}║${NC}"
echo -e "${CYAN}╠══════════════════════════════════════════════════════════════╣${NC}"
echo -e "${CYAN}║${NC}  📁 Папка: $BLOB_DIR"
echo -e "${CYAN}║${NC}  💾 Размер: $TOTAL_SIZE"
echo -e "${CYAN}╠══════════════════════════════════════════════════════════════╣${NC}"
echo -e "${CYAN}║${NC}  📋 Что извлечено:                                          ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    • vendor.img (raw образ раздела)                         ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    • /vendor/* (HAL библиотеки, конфиги)                    ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    • AIDL HAL сервисы и VINTF манифесты                     ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    • Firmware (Wi-Fi BCM4389, модем Shannon, GPU)           ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    • Конфигурация ядра (NetHunter)                          ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    • Разметка разделов и fstab                              ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    • Init-скрипты и SELinux                                 ${CYAN}║${NC}"
echo -e "${CYAN}╠══════════════════════════════════════════════════════════════╣${NC}"
echo -e "${CYAN}║${NC}                                                              ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  ${YELLOW}⚠️  НЕ ЗАГРУЖАЙТЕ vendor_blobs/ на GitHub!${NC}                   ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  ${YELLOW}   Это проприетарные файлы Google/Samsung.${NC}                    ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  ${YELLOW}   Добавьте vendor_blobs/ в .gitignore${NC}                        ${CYAN}║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════════════════════════════╝${NC}"

# Добавляем в gitignore
echo "vendor_blobs/" >> "$WORKDIR/.gitignore" 2>/dev/null
