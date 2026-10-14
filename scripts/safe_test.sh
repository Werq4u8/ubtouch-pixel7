#!/bin/bash
# ============================================================================
# safe_test.sh — Безопасный тест Ubuntu Touch БЕЗ стирания данных
# ============================================================================
# Этот скрипт:
# 1. Закидывает ubuntu.img на телефон рядом с данными Android
# 2. Загружает ядро в RAM (fastboot boot — не flash!)
# 3. Ubuntu Touch монтирует rootfs из файла на /data
# 4. Перезагрузка = обратно в Android, данные целы
# ============================================================================
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

WORKDIR="$(cd "$(dirname "$0")/.." && pwd)"

find_image() {
    local name="$1"
    for p in "${WORKDIR}/out/images/${name}" "${WORKDIR}/out/${name}"; do
        if [ -f "$p" ]; then
            echo "$p"
            return 0
        fi
    done
    echo "${WORKDIR}/out/images/${name}"
}

BOOT_IMG=$(find_image "halium-boot.img")
[ ! -f "$BOOT_IMG" ] && BOOT_IMG=$(find_image "boot.img")
ROOTFS_IMG=$(find_image "rootfs.img")
[ ! -f "$ROOTFS_IMG" ] && ROOTFS_IMG=$(find_image "ubuntu.img")
SYSTEM_IMG=$(find_image "system.img")


info()    { echo -e "${BLUE}[ИНФО]${NC} $*"; }
success() { echo -e "${GREEN}[  ✅ ]${NC} $*"; }
warn()    { echo -e "${YELLOW}[  ⚠️ ]${NC} $*"; }
error()   { echo -e "${RED}[  ❌ ]${NC} $*"; exit 1; }

echo -e "${CYAN}"
echo "╔══════════════════════════════════════════════════════════╗"
echo "║  🧪 БЕЗОПАСНЫЙ ТЕСТ Ubuntu Touch на Pixel 7             ║"
echo "║                                                          ║"
echo "║  ✅ Данные Android НЕ будут стёрты                       ║"
echo "║  ✅ Перезагрузка = возврат в Android                     ║"
echo "║  ✅ Ядро грузится в RAM, не пишется на устройство        ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo -e "${NC}"

# ═══════════════════════════════════════════════════════
# Этап 0: Проверка инструментов
# ═══════════════════════════════════════════════════════
for tool in adb fastboot; do
    if ! command -v "$tool" &>/dev/null; then
        error "$tool не найден. Установите Android Platform Tools."
    fi
done

# ═══════════════════════════════════════════════════════
# Этап 1: Проверка образов
# ═══════════════════════════════════════════════════════
info "Проверяю наличие образов..."

if [ ! -f "$BOOT_IMG" ]; then
    error "halium-boot.img не найден: $BOOT_IMG\n  Сначала соберите проект: ./scripts/build_all.sh"
fi
success "halium-boot.img найден ($(du -h "$BOOT_IMG" | cut -f1))"

if [ ! -f "$ROOTFS_IMG" ]; then
    error "rootfs.img не найден: $ROOTFS_IMG\n  Сначала соберите проект: ./scripts/build_all.sh"
fi
ROOTFS_SIZE=$(du -h "$ROOTFS_IMG" | cut -f1)
success "rootfs.img найден ($ROOTFS_SIZE)"

# ═══════════════════════════════════════════════════════
# Этап 2: Проверка устройства
# ═══════════════════════════════════════════════════════
echo ""
info "Проверяю подключение устройства..."

if ! adb devices | grep -q "device$"; then
    error "Устройство не найдено. Подключите Pixel 7 с включённой USB-отладкой."
fi

DEVICE_NAME=$(adb shell getprop ro.product.device 2>/dev/null || echo "unknown")
if [[ "$DEVICE_NAME" != "panther" && "$DEVICE_NAME" != "pantah" ]]; then
    warn "Устройство определено как '$DEVICE_NAME', ожидается 'panther'."
    warn "Это может быть не Pixel 7. Продолжить? (y/N)"
    read -r CONFIRM
    [[ "$CONFIRM" != "y" && "$CONFIRM" != "Y" ]] && exit 0
fi
success "Устройство: $(adb shell getprop ro.product.model) ($DEVICE_NAME)"

# Проверяем свободное место
FREE_SPACE_KB=$(adb shell df /data 2>/dev/null | tail -1 | awk '{print $4}')
FREE_SPACE_MB=$((FREE_SPACE_KB / 1024))
ROOTFS_SIZE_MB=$(du -m "$ROOTFS_IMG" | cut -f1)

info "Свободно на /data: ${FREE_SPACE_MB} МБ, нужно: ${ROOTFS_SIZE_MB} МБ"

if [ "$FREE_SPACE_MB" -lt "$ROOTFS_SIZE_MB" ]; then
    error "Недостаточно места! Нужно ${ROOTFS_SIZE_MB} МБ, свободно ${FREE_SPACE_MB} МБ.\n  Освободите место на телефоне."
fi
success "Места достаточно"

# ═══════════════════════════════════════════════════════
# Этап 3: Загрузка rootfs на телефон
# ═══════════════════════════════════════════════════════
echo ""
info "📤 Загружаю rootfs на телефон (это может занять несколько минут)..."
info "   ubuntu.img будет лежать рядом с вашими данными Android,"
info "   как обычный файл. Ничего не стирается."

# Закидываем rootfs через /data/local/tmp и перемещаем с правами root
adb push "$ROOTFS_IMG" /data/local/tmp/ubuntu.img
adb shell "su -c 'mv /data/local/tmp/ubuntu.img /data/ubuntu.img && chmod 644 /data/ubuntu.img'" 2>/dev/null || \
adb push "$ROOTFS_IMG" /data/ubuntu.img 2>/dev/null || true
success "rootfs загружен на телефон: /data/ubuntu.img"

# Если есть system.img — тоже закидываем
if [ -f "$SYSTEM_IMG" ]; then
    info "📤 Загружаю system.img..."
    adb push "$SYSTEM_IMG" /data/local/tmp/system.img
    adb shell "su -c 'mv /data/local/tmp/system.img /data/system.img && chmod 644 /data/system.img'" 2>/dev/null || true
    success "system.img загружен"
fi


# ═══════════════════════════════════════════════════════
# Этап 4: Загрузка ядра в RAM (БЕЗ записи!)
# ═══════════════════════════════════════════════════════
echo ""
echo -e "${BOLD}${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}${YELLOW}  ВНИМАНИЕ: Сейчас телефон перезагрузится в bootloader${NC}"
echo -e "${BOLD}${YELLOW}  и загрузит ядро Ubuntu Touch ИЗ ОПЕРАТИВНОЙ ПАМЯТИ. ${NC}"
echo -e "${BOLD}${YELLOW}                                                       ${NC}"
echo -e "${BOLD}${YELLOW}  • Ваши данные НЕ будут затронуты                     ${NC}"
echo -e "${BOLD}${YELLOW}  • Если что-то не так — зажмите ПИТАНИЕ на 15 сек     ${NC}"
echo -e "${BOLD}${YELLOW}  • Телефон перезагрузится обратно в Android            ${NC}"
echo -e "${BOLD}${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo ""
echo -e "Продолжить? ${GREEN}(y/N)${NC}: "
read -r CONFIRM
if [[ "$CONFIRM" != "y" && "$CONFIRM" != "Y" ]]; then
    info "Отменено. Файлы на телефоне можно удалить:"
    info "  adb shell rm /data/ubuntu.img"
    exit 0
fi

# Перезагрузка в bootloader
info "🔄 Перезагрузка в bootloader..."
adb reboot bootloader
sleep 10

# Проверяем что мы в fastboot
if ! fastboot devices | grep -q "fastboot"; then
    sleep 5
    if ! fastboot devices | grep -q "fastboot"; then
        error "Устройство не найдено в режиме fastboot.\n  Попробуйте вручную: выключите, зажмите Vol Down + Power"
    fi
fi
success "Устройство в режиме fastboot"

# КЛЮЧЕВОЙ МОМЕНТ: fastboot boot, НЕ fastboot flash!
# Загружаем ядро в RAM — на устройство НИЧЕГО не пишется
info "🚀 Загрузка halium-boot.img в RAM (fastboot boot, НЕ flash!)..."
fastboot boot "$BOOT_IMG"

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  🚀 Ядро загружено! Ожидайте...                         ║${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║                                                          ║${NC}"
echo -e "${GREEN}║  Что должно произойти:                                   ║${NC}"
echo -e "${GREEN}║  1. Ядро грузится (~10-30 сек)                           ║${NC}"
echo -e "${GREEN}║  2. initramfs ищет /data/ubuntu.img                      ║${NC}"
echo -e "${GREEN}║  3. Если нашёл — монтирует и запускает Ubuntu Touch       ║${NC}"
echo -e "${GREEN}║  4. Если нет — поднимает debug shell по USB              ║${NC}"
echo -e "${GREEN}║                                                          ║${NC}"
echo -e "${GREEN}║  Для подключения к debug shell:                          ║${NC}"
echo -e "${GREEN}║    telnet 10.15.19.82 2323                               ║${NC}"
echo -e "${GREEN}║  или:                                                    ║${NC}"
echo -e "${GREEN}║    ssh phablet@10.15.19.82  (пароль: phablet)            ║${NC}"
echo -e "${GREEN}║                                                          ║${NC}"
echo -e "${GREEN}║  Для возврата в Android:                                 ║${NC}"
echo -e "${GREEN}║    Зажмите кнопку ПИТАНИЯ на 15 секунд                   ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════╝${NC}"
echo ""

# Ждём появления USB-сети
info "Ожидаю USB-сетевой интерфейс (до 60 сек)..."
for i in $(seq 1 60); do
    if ping -c1 -W1 10.15.19.82 &>/dev/null 2>&1; then
        success "USB-сеть доступна! Подключайтесь:"
        echo ""
        echo "  telnet 10.15.19.82 2323"
        echo "  или"
        echo "  ssh phablet@10.15.19.82"
        echo ""
        exit 0
    fi
    printf "\r  Ожидание... %d/60 сек" "$i"
    sleep 1
done

echo ""
warn "USB-сеть не появилась за 60 секунд."
warn "Возможные причины:"
echo "  1. Ядро не загрузилось (kernel panic) — проверьте экран"
echo "  2. USB-сеть не настроена в initramfs"
echo "  3. Нужен другой IP — попробуйте: ip addr show"
echo ""
echo "Для возврата в Android: зажмите ПИТАНИЕ на 15 секунд"
