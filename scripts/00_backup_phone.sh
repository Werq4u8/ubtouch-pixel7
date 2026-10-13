#!/bin/bash
# ============================================================================
# 00_backup_phone.sh — Полный бэкап данных с Pixel 7 перед прошивкой
# ============================================================================
# Запускайте ДО любых действий с прошивкой!
# Сохраняет: фото, видео, документы, контакты, приложения, SMS
# ============================================================================
set -euo pipefail

# --- Цвета ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

BACKUP_DIR="${1:-$HOME/pixel7-backup/$(date +%Y%m%d_%H%M%S)}"

info()    { echo -e "${BLUE}[ИНФО]${NC} $*"; }
success() { echo -e "${GREEN}[  ✅ ]${NC} $*"; }
warn()    { echo -e "${YELLOW}[  ⚠️ ]${NC} $*"; }
error()   { echo -e "${RED}[  ❌ ]${NC} $*"; }

echo -e "${CYAN}"
echo "╔══════════════════════════════════════════════════════╗"
echo "║    📱 БЭКАП GOOGLE PIXEL 7 ПЕРЕД ПРОШИВКОЙ          ║"
echo "╚══════════════════════════════════════════════════════╝"
echo -e "${NC}"

# --- Проверка adb ---
if ! command -v adb &>/dev/null; then
    error "adb не найден. Установите: sudo apt install adb"
    exit 1
fi

# --- Проверка подключения ---
info "Проверяю подключение устройства..."
if ! adb devices | grep -q "device$"; then
    error "Устройство не найдено."
    echo "  1. Подключите Pixel 7 по USB"
    echo "  2. Включите USB-отладку (Настройки → Для разработчиков)"
    echo "  3. Разрешите подключение на экране телефона"
    exit 1
fi

DEVICE_MODEL=$(adb shell getprop ro.product.model 2>/dev/null || echo "unknown")
DEVICE_SERIAL=$(adb get-serialno 2>/dev/null || echo "unknown")
success "Устройство подключено: $DEVICE_MODEL (SN: $DEVICE_SERIAL)"

# --- Создание папки бэкапа ---
mkdir -p "$BACKUP_DIR"
info "Папка бэкапа: $BACKUP_DIR"

# --- 1. Фото и видео (DCIM) ---
echo ""
info "📸 [1/7] Копирование фото и видео..."
mkdir -p "$BACKUP_DIR/DCIM"
adb pull /sdcard/DCIM/ "$BACKUP_DIR/DCIM/" 2>/dev/null && \
    success "Фото/видео скопированы" || warn "Нет файлов в DCIM или ошибка"

# --- 2. Скриншоты и картинки ---
info "🖼️  [2/7] Копирование картинок и скриншотов..."
mkdir -p "$BACKUP_DIR/Pictures"
adb pull /sdcard/Pictures/ "$BACKUP_DIR/Pictures/" 2>/dev/null && \
    success "Картинки скопированы" || warn "Нет файлов в Pictures"

# --- 3. Загрузки ---
info "📥 [3/7] Копирование загрузок..."
mkdir -p "$BACKUP_DIR/Download"
adb pull /sdcard/Download/ "$BACKUP_DIR/Download/" 2>/dev/null && \
    success "Загрузки скопированы" || warn "Нет файлов в Download"

# --- 4. Документы ---
info "📄 [4/7] Копирование документов..."
mkdir -p "$BACKUP_DIR/Documents"
adb pull /sdcard/Documents/ "$BACKUP_DIR/Documents/" 2>/dev/null && \
    success "Документы скопированы" || warn "Нет файлов в Documents"

# --- 5. Музыка ---
info "🎵 [5/7] Копирование музыки..."
mkdir -p "$BACKUP_DIR/Music"
adb pull /sdcard/Music/ "$BACKUP_DIR/Music/" 2>/dev/null && \
    success "Музыка скопирована" || warn "Нет файлов в Music"

# --- 6. Полный бэкап через adb backup ---
info "💾 [6/7] Создание полного бэкапа приложений (adb backup)..."
warn "На экране телефона нажмите 'Создать резервную копию'"
warn "(Можете задать пароль шифрования или оставить пустым)"
adb backup -apk -shared -all -f "$BACKUP_DIR/full_backup.ab" 2>/dev/null && \
    success "Полный бэкап создан: full_backup.ab" || \
    warn "adb backup не поддерживается на этой версии Android. Пропускаю."

# --- 7. Список установленных приложений ---
info "📋 [7/7] Сохранение списка приложений..."
adb shell pm list packages -3 2>/dev/null | sed 's/package://' | sort > "$BACKUP_DIR/installed_apps.txt" && \
    success "Список приложений сохранён: installed_apps.txt" || warn "Не удалось получить список"

# --- 8. Сохраняем информацию об устройстве ---
info "📝 Сохранение информации об устройстве..."
{
    echo "=== Информация об устройстве ==="
    echo "Дата бэкапа: $(date)"
    echo "Модель: $DEVICE_MODEL"
    echo "Серийный номер: $DEVICE_SERIAL"
    echo "Android версия: $(adb shell getprop ro.build.version.release 2>/dev/null)"
    echo "Сборка: $(adb shell getprop ro.build.display.id 2>/dev/null)"
    echo "Bootloader: $(adb shell getprop ro.bootloader 2>/dev/null)"
    echo "Кодовое имя: $(adb shell getprop ro.product.device 2>/dev/null)"
    echo ""
    echo "=== Разделы ==="
    adb shell ls -la /dev/block/by-name/ 2>/dev/null || echo "(недоступно)"
} > "$BACKUP_DIR/device_info.txt"
success "Информация сохранена"

# --- Итог ---
BACKUP_SIZE=$(du -sh "$BACKUP_DIR" 2>/dev/null | cut -f1)

echo ""
echo -e "${CYAN}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║${NC}  ${GREEN}✅ БЭКАП ЗАВЕРШЁН!${NC}                                  ${CYAN}║${NC}"
echo -e "${CYAN}╠══════════════════════════════════════════════════════╣${NC}"
echo -e "${CYAN}║${NC}  📁 Папка: $BACKUP_DIR"
echo -e "${CYAN}║${NC}  💾 Размер: $BACKUP_SIZE"
echo -e "${CYAN}╠══════════════════════════════════════════════════════╣${NC}"
echo -e "${CYAN}║${NC}  ${YELLOW}⚠️  ПРОВЕРЬТЕ что все важные файлы скопировались!${NC}    ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}                                                      ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  Также убедитесь что:                                ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  • Контакты синхронизированы с Google                ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  • WhatsApp/Telegram бэкап в облаке                  ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  • 2FA коды перенесены / записаны                    ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  • Стоковая прошивка скачана на ПК                   ${CYAN}║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════════════════════╝${NC}"
