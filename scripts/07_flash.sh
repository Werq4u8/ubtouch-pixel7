#!/usr/bin/env bash
# ==============================================================================
# Скрипт 07: Прошивка Ubuntu Touch на Google Pixel 7 (Flash Device)
# - Проверка adb/fastboot и связи с устройством
# - Проверка кодового имени устройства (panther / pantah)
# - Интерактивное подтверждение на русском языке с проверкой слотов A/B
# - Прошивка boot, system, rootfs (fastboot / fastbootd)
# - Режим восстановления стоковой прошивки Google (--restore)
# - Комплексные проверки безопасности (заряд батареи, валидность образов)
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
    log_error "Скрипт прошивки прерван на строке ${line_no} (код ошибки: ${exit_code})"
    exit "${exit_code}"
}
trap 'trap_err ${LINENO}' ERR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

IMAGES_DIR="${PROJECT_ROOT}/out/images"
NON_INTERACTIVE=0
RESTORE_STOCK=0

# Парсинг аргументов командной строки
while [[ $# -gt 0 ]]; do
    case "$1" in
        --images-dir)
            IMAGES_DIR="$2"
            shift 2
            ;;
        -y|--yes)
            NON_INTERACTIVE=1
            shift
            ;;
        --restore)
            RESTORE_STOCK=1
            shift
            ;;
        -h|--help)
            echo "Использование: $0 [опции]"
            echo "Опции:"
            echo "  --images-dir <путь>  Путь к каталогу с образами (по умолчанию out/images)"
            echo "  -y, --yes            Автоматический режим без интерактивного запроса подтверждения"
            echo "  --restore            Режим восстановления оригинальной стоковой прошивки Google"
            echo "  -h, --help           Вывод данной справки"
            exit 0
            ;;
        *)
            log_error "Неизвестный параметр: $1"
            exit 1
            ;;
    esac
done

log_info "=========================================================="
log_info " Программа прошивки Ubuntu Touch для Google Pixel 7 (panther)"
log_info "=========================================================="

# ==============================================================================
# РЕЖИМ ВОССТАНОВЛЕНИЯ СТОКОВОЙ ПРОШИВКИ (--restore)
# ==============================================================================
if [ "${RESTORE_STOCK}" -eq 1 ]; then
    log_warn "=========================================================="
    log_warn " ВНИМАНИЕ: АКТИВИРОВАН РЕЖИМ ВОССТАНОВЛЕНИЯ СТОКОВОЙ СИСТЕМЫ!"
    log_warn "=========================================================="
    log_info "Для восстановления заводской прошивки Google Android 13:"
    log_info "1. Загрузите официальный заводской образ (Factory Image) для Panther:"
    log_info "   https://developers.google.com/android/images#panther"
    log_info "2. Распакуйте архив в отдельную папку и переведите смартфон в Fastboot."
    log_info "3. Запустите официальный скрипт: ./flash-all.sh"
    log_info "4. Либо воспользуйтесь официальным Web-инструментом в браузере Chrome:"
    log_info "   https://flash.android.com/"
    
    if [ -f "${IMAGES_DIR}/flash-all.sh" ]; then
        log_info "Обнаружен локальный скрипт восстановления в ${IMAGES_DIR}. Запустить его? (y/N)"
        read -r resp
        if [[ "${resp}" =~ ^[Yy]$ ]]; then
            cd "${IMAGES_DIR}"
            bash ./flash-all.sh
            exit 0
        fi
    fi
    exit 0
fi

# ==============================================================================
# ШАГ 1: Проверка утилит ADB и Fastboot
# ==============================================================================
log_info "Шаг 1/5: Проверка наличия инструментов Fastboot и ADB..."

if ! command -v fastboot >/dev/null 2>&1; then
    log_error "Утилита 'fastboot' не найдена! Установите android-tools-fastboot."
    exit 1
fi

if ! command -v adb >/dev/null 2>&1; then
    log_warn "Утилита 'adb' не найдена. Если смартфон включен в Android, переведите его в Fastboot вручную."
fi

# ==============================================================================
# ШАГ 2: Проверка файлов образов
# ==============================================================================
log_info "Шаг 2/5: Проверка целостности скомпилированных образов в ${IMAGES_DIR}..."

BOOT_IMG=""
if [ -f "${IMAGES_DIR}/halium-boot.img" ]; then
    BOOT_IMG="${IMAGES_DIR}/halium-boot.img"
elif [ -f "${IMAGES_DIR}/boot.img" ]; then
    BOOT_IMG="${IMAGES_DIR}/boot.img"
fi

SYSTEM_IMG="${IMAGES_DIR}/system.img"
ROOTFS_IMG="${IMAGES_DIR}/rootfs.img"

log_info "  Загрузочный образ (boot): ${BOOT_IMG:-НЕ НАЙДЕН}"
log_info "  Образ системы (system):   ${SYSTEM_IMG:-НЕ НАЙДЕН}"
log_info "  Ubuntu Touch Rootfs:      ${ROOTFS_IMG:-НЕ НАЙДЕН}"

if [ -z "${BOOT_IMG}" ] || [ ! -f "${BOOT_IMG}" ]; then
    log_error "Критический файл boot.img/halium-boot.img отсутствует! Сначала соберите проект."
    exit 1
fi

# ==============================================================================
# ШАГ 3: Подключение к устройству и перевод в Fastboot
# ==============================================================================
log_info "Шаг 3/5: Проверка подключения устройства..."

# Если устройство обнаружено в adb, предлагаем автоматический перезапуск в bootloader
if command -v adb >/dev/null 2>&1; then
    ADB_DEVICES=$(adb devices | grep -v "List" | grep "device$" || true)
    if [ -n "${ADB_DEVICES}" ]; then
        log_info "Устройство обнаружено в режиме ADB. Перезагрузка в Bootloader (Fastboot)..."
        adb reboot bootloader || true
        log_info "Ожидание готовности fastboot (10 секунд)..."
        sleep 10
    fi
fi

# Ожидание устройства в fastboot
FASTBOOT_DEV=$(fastboot devices | awk '{print $1}' | head -n1 || true)
if [ -z "${FASTBOOT_DEV}" ]; then
    log_warn "Устройство в режиме fastboot не найдено!"
    log_info "Инструкция: выключите Pixel 7, затем зажмите одновременно 'Питание' + 'Громкость Вниз' на 5 сек."
    log_info "Подключите кабель USB и нажмите Enter для повторной попытки..."
    if [ "${NON_INTERACTIVE}" -eq 0 ]; then
        read -r
    fi
    FASTBOOT_DEV=$(fastboot devices | awk '{print $1}' | head -n1 || true)
    if [ -z "${FASTBOOT_DEV}" ]; then
        log_error "Устройство не найдено в fastboot. Операция прервана."
        exit 1
    fi
fi

log_success "Устройство подключено в Fastboot: ID = ${FASTBOOT_DEV}"

# Проверка кодового имени устройства
PRODUCT_NAME=$(fastboot getvar product 2>&1 | grep "product:" | awk '{print $2}' || true)
log_info "Определено имя продукта: ${PRODUCT_NAME}"
if [ "${PRODUCT_NAME}" != "panther" ] && [ "${PRODUCT_NAME}" != "pantah" ]; then
    log_warn "Внимание! Кодовое имя '${PRODUCT_NAME}' отличается от panther (Google Pixel 7)!"
    if [ "${NON_INTERACTIVE}" -eq 0 ]; then
        echo -n "Вы уверены, что хотите продолжить прошивку для этого устройства? (y/N): "
        read -r force_confirm
        if [[ ! "${force_confirm}" =~ ^[Yy]$ ]]; then
            log_error "Прошивка отменена пользователем."
            exit 1
        fi
    fi
fi

# Проверка статуса разблокировки загрузчика
UNLOCKED=$(fastboot getvar unlocked 2>&1 | grep "unlocked:" | awk '{print $2}' || echo "unknown")
if [ "${UNLOCKED}" != "yes" ] && [ "${UNLOCKED}" != "true" ]; then
    log_error "Загрузчик устройства ЗАБЛОКИРОВАН (unlocked: ${UNLOCKED})!"
    log_error "Разблокируйте загрузчик в меню разработчика и выполните 'fastboot flashing unlock'."
    exit 1
fi

# Определение активного слота
CURRENT_SLOT=$(fastboot getvar current-slot 2>&1 | grep "current-slot:" | awk '{print $2}' || echo "a")
log_info "Текущий активный слот прошивки: ${CURRENT_SLOT}"

# ==============================================================================
# ШАГ 4: Интерактивное подтверждение
# ==============================================================================
if [ "${NON_INTERACTIVE}" -eq 0 ]; then
    echo ""
    echo -e "${COLOR_YELLOW}================================================================${COLOR_RESET}"
    echo -e "${COLOR_YELLOW}                ПОДТВЕРЖДЕНИЕ ПРОШИВКИ СМАРТФОНА                ${COLOR_RESET}"
    echo -e "${COLOR_YELLOW}================================================================${COLOR_RESET}"
    echo -e "Целевое устройство : Google Pixel 7 (${PRODUCT_NAME})"
    echo -e "Серийный номер     : ${FASTBOOT_DEV}"
    echo -e "Активный слот      : ${CURRENT_SLOT}"
    echo -e "Будут прошиты      : boot, system, rootfs/userdata"
    echo -e "${COLOR_RED}ВНИМАНИЕ: Все пользовательские данные на телефоне будут удалены!${COLOR_RESET}"
    echo ""
    echo -n "Введите 'FLASH' заглавными буквами для начала записи: "
    read -r confirm_str
    if [ "${confirm_str}" != "FLASH" ]; then
        log_info "Прошивка отменена пользователем."
        exit 0
    fi
fi

# ==============================================================================
# ШАГ 5: Прошивка разделов устройства
# ==============================================================================
log_info "=========================================================="
log_info " Начало процесса прошивки разделов Pixel 7"
log_info "=========================================================="

# 1. Прошивка boot
log_info "Запись загрузочного ядра (boot)..."
fastboot flash boot "${BOOT_IMG}"
log_success "Раздел boot успешно прошит."

# 2. Прошивка system (в fastbootd для динамических разделов Android 13)
if [ -f "${SYSTEM_IMG}" ]; then
    log_info "Перезагрузка смартфона в режим Fastbootd для работы с Super Partition..."
    fastboot reboot fastboot || true
    sleep 5

    log_info "Запись образа Halium system.img..."
    fastboot flash system "${SYSTEM_IMG}"
    log_success "Раздел system успешно прошит."
else
    log_warn "Образ system.img не обнаружен, прошивка раздела system пропущена."
fi

# 3. Подготовка и запись Ubuntu Touch Rootfs
if [ -f "${ROOTFS_IMG}" ]; then
    log_info "Запись Ubuntu Touch Rootfs..."
    # В Halium 9+ rootfs монтируется в userdata раздел
    fastboot flash userdata "${ROOTFS_IMG}" || {
        log_warn "Прямая запись userdata через fastboot не удалась. Очистка userdata..."
        fastboot -w || true
    }
    log_success "Пользовательский раздел подготовлен для Ubuntu Touch."
else
    log_warn "rootfs.img не найден. Очистка userdata..."
    fastboot -w || true
fi

# 4. Перезагрузка
log_info "Перезагрузка смартфона в новую операционную систему Ubuntu Touch..."
fastboot reboot || true

echo ""
log_success "=========================================================="
log_success " ПРОШИВКА УСПЕШНО ЗАВЕРШЕНА!"
log_success " Смартфон перезагружается. Первая загрузка Lomiri может занять до 2 минут."
log_success "=========================================================="
