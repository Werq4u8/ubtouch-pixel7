#!/usr/bin/env bash
# ==============================================================================
# Мастер-скрипт оркестрации сборки Ubuntu Touch для Google Pixel 7
# (build_all.sh)
#
# Последовательное выполнение этапов:
# 1. 01_setup_env.sh      - Настройка хост-окружения и установка зависимостей
# 2. 02_fetch_sources.sh  - Синхронизация LineageOS 20 и ядра Linux 5.10
# 3. 03_patch_kernel.sh   - Интеграция конфигурации Ubuntu Touch и обход KMI ABI
# 4. 04_build_kernel.sh   - Компиляция ядра Linux 5.10 (Kleaf / Bazel / Clang)
# 5. 05_build_halium.sh   - Сборка Halium 13 boot и systemimage
# 6. 06_prepare_rootfs.sh - Подготовка и упаковка SquashFS Rootfs (Lomiri)
#
# Возможности:
# - Сохранение прогресса в файл состояния (.build_state)
# - Возобновление сборки с последнего успешного шага
# - Расчет общего времени компиляции и приблизительного ETA
# - Цветное информирование на русском языке
# ==============================================================================

set -euo pipefail

# Цветовая палитра
readonly COLOR_RESET="\033[0m"
readonly COLOR_RED="\033[1;31m"
readonly COLOR_GREEN="\033[1;32m"
readonly COLOR_YELLOW="\033[1;33m"
readonly COLOR_BLUE="\033[1;34m"
readonly COLOR_MAGENTA="\033[1;35m"
readonly COLOR_CYAN="\033[1;36m"
readonly COLOR_BOLD="\033[1m"

log_info() { echo -e "${COLOR_BLUE}[ИНФО]${COLOR_RESET} $*"; }
log_success() { echo -e "${COLOR_GREEN}[УСПЕХ]${COLOR_RESET} $*"; }
log_warn() { echo -e "${COLOR_YELLOW}[ВНИМАНИЕ]${COLOR_RESET} $*"; }
log_error() { echo -e "${COLOR_RED}[ОШИБКА]${COLOR_RESET} $*" >&2; }
log_step() { echo -e "\n${COLOR_CYAN}${COLOR_BOLD}>>> $* <<<${COLOR_RESET}\n"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
STATE_FILE="${PROJECT_ROOT}/.build_state"

# Трассировка критических ошибок
trap_err() {
    local exit_code=$?
    local line_no=$1
    log_error "Мастер-скрипт сборки остановлен на строке ${line_no} (код ошибки: ${exit_code})"
    log_warn "Вы можете устранить причину сбоя и повторно запустить ./scripts/build_all.sh для продолжения."
    exit "${exit_code}"
}
trap 'trap_err ${LINENO}' ERR

# Список шагов и их описаний
STEPS=(
    "01_setup_env.sh:Настройка сборочного окружения и зависимостей:180"
    "02_fetch_sources.sh:Загрузка исходных кодов LineageOS 20 и ядра Linux 5.10:1200"
    "03_patch_kernel.sh:Патчинг ядра (AppArmor, cgroups, binder, GKI ABI):60"
    "04_build_kernel.sh:Компиляция ядра и создание boot.img:900"
    "05_build_halium.sh:Сборка компонентов Halium 13 и systemimage:2400"
    "06_prepare_rootfs.sh:Подготовка и упаковка Ubuntu Touch Rootfs:300"
)

TOTAL_STEPS=${#STEPS[@]}

# Парсинг аргументов
START_FROM_STEP=1
if [ "${1:-}" == "--reset" ]; then
    rm -f "${STATE_FILE}"
    log_info "Файл состояния сборки сброшен. Сборка начнется с шага 1."
elif [ "${1:-}" == "--step" ] && [ -n "${2:-}" ]; then
    START_FROM_STEP="$2"
    log_info "Принудительный запуск с шага ${START_FROM_STEP}."
elif [ "${1:-}" == "--help" ] || [ "${1:-}" == "-h" ]; then
    echo "Использование: $0 [опции]"
    echo "Опции:"
    echo "  --reset          Сбросить файл состояния и начать полную сборку с нуля"
    echo "  --step <номер>   Начать сборку с указанного шага (1-${TOTAL_STEPS})"
    echo "  -h, --help       Показать эту справку"
    exit 0
fi

# Чтение предыдущего сохраненного состояния
if [ -f "${STATE_FILE}" ] && [ "${START_FROM_STEP}" -eq 1 ]; then
    SAVED_STEP=$(cat "${STATE_FILE}" 2>/dev/null || echo 0)
    if [[ "${SAVED_STEP}" =~ ^[0-9]+$ ]] && [ "${SAVED_STEP}" -lt "${TOTAL_STEPS}" ]; then
        START_FROM_STEP=$((SAVED_STEP + 1))
        log_info "Обнаружен предыдущий прогресс сборки. Возобновление с шага ${START_FROM_STEP} из ${TOTAL_STEPS}."
    fi
fi

# Подсчет общего расчетного времени
calc_total_est_seconds() {
    local total=0
    for item in "${STEPS[@]}"; do
        local est
        est=$(echo "${item}" | cut -d':' -f3)
        total=$((total + est))
    done
    echo "${total}"
}

format_time() {
    local total_seconds=$1
    local hours=$((total_seconds / 3600))
    local minutes=$(((total_seconds % 3600) / 60))
    local seconds=$((total_seconds % 60))
    printf "%02d:%02d:%02d" "${hours}" "${minutes}" "${seconds}"
}

BUILD_START_TIMESTAMP=$(date +%s)
TOTAL_ESTIMATED_TIME=$(calc_total_est_seconds)

echo ""
echo -e "${COLOR_GREEN}======================================================================${COLOR_RESET}"
echo -e "${COLOR_GREEN}      UBUNTU TOUCH ДЛЯ GOOGLE PIXEL 7 (PANTHER) — МАСТЕР-СБОРКА       ${COLOR_RESET}"
echo -e "${COLOR_GREEN}======================================================================${COLOR_RESET}"
log_info "Всего этапов сборки  : ${TOTAL_STEPS}"
log_info "Старт с этапа        : ${START_FROM_STEP}"
log_info "Расчетное время      : ~$(format_time "${TOTAL_ESTIMATED_TIME}")"
log_info "Каталог проекта      : ${PROJECT_ROOT}"
echo ""

# Основной цикл выполнения шагов
for idx in "${!STEPS[@]}"; do
    STEP_NUM=$((idx + 1))
    
    if [ "${STEP_NUM}" -lt "${START_FROM_STEP}" ]; then
        log_info "Пропуск уже выполненного шага ${STEP_NUM}/${TOTAL_STEPS}: $(echo "${STEPS[${idx}]}" | cut -d':' -f2)"
        continue
    fi

    SCRIPT_NAME=$(echo "${STEPS[${idx}]}" | cut -d':' -f1)
    STEP_DESC=$(echo "${STEPS[${idx}]}" | cut -d':' -f2)
    STEP_EST=$(echo "${STEPS[${idx}]}" | cut -d':' -f3)
    SCRIPT_PATH="${SCRIPT_DIR}/${SCRIPT_NAME}"

    PERCENT=$(( (STEP_NUM - 1) * 100 / TOTAL_STEPS ))

    # Расчет приблизительного оставшегося времени (ETA)
    REMAINING_EST=0
    for ((rem = idx; rem < TOTAL_STEPS; rem++)); do
        r_est=$(echo "${STEPS[${rem}]}" | cut -d':' -f3)
        REMAINING_EST=$((REMAINING_EST + r_est))
    done

    log_step "[${STEP_NUM}/${TOTAL_STEPS}] (${PERCENT}%) ${STEP_DESC}"
    log_info "Скрипт этапа: ${SCRIPT_NAME} | Примерный ETA: ~$(format_time "${REMAINING_EST}")"

    if [ ! -f "${SCRIPT_PATH}" ]; then
        log_error "Скрипт ${SCRIPT_PATH} не найден!"
        exit 1
    fi

    chmod +x "${SCRIPT_PATH}"

    # Запуск скрипта этапа с измерением времени
    STEP_START_TIME=$(date +%s)
    
    bash "${SCRIPT_PATH}"

    STEP_END_TIME=$(date +%s)
    STEP_DURATION=$((STEP_END_TIME - STEP_START_TIME))

    log_success "Этап ${STEP_NUM}/${TOTAL_STEPS} (${SCRIPT_NAME}) успешно завершен за $(format_time "${STEP_DURATION}")."

    # Сохранение прогресса
    echo "${STEP_NUM}" > "${STATE_FILE}"
done

BUILD_END_TIMESTAMP=$(date +%s)
TOTAL_BUILD_DURATION=$((BUILD_END_TIMESTAMP - BUILD_START_TIMESTAMP))

# Очистка файла состояния по успешному завершению всех этапов
rm -f "${STATE_FILE}"

echo ""
echo -e "${COLOR_GREEN}======================================================================${COLOR_RESET}"
echo -e "${COLOR_GREEN}                 ПОЛНАЯ СБОРКА УСПЕШНО ЗАВЕРШЕНА!                    ${COLOR_RESET}"
echo -e "${COLOR_GREEN}======================================================================${COLOR_RESET}"
log_info "Общее время сборки      : $(format_time "${TOTAL_BUILD_DURATION}")"
log_info "Директория с образами   : ${PROJECT_ROOT}/out/images"
log_info "Готовые файлы:"
ls -lh "${PROJECT_ROOT}/out/images" 2>/dev/null || true
echo ""
log_info "Для прошивки на ваш Google Pixel 7 выполните:"
echo -e "  ${COLOR_CYAN}./scripts/07_flash.sh${COLOR_RESET}"
echo ""
