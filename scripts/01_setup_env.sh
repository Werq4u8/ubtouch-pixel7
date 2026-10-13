#!/usr/bin/env bash
# ==============================================================================
# Скрипт 01: Настройка сборочного окружения (Host Environment Setup)
# Целевая ОС хоста: Ubuntu 22.04 LTS (x86_64)
# Целевое устройство: Google Pixel 7 (panther, SoC gs201)
# База: Android 13 (LineageOS 20) / Halium 13 / Ubuntu Touch (Focal/Noble)
# ==============================================================================

set -euo pipefail

# Цветовая палитра для терминала
readonly COLOR_RESET="\033[0m"
readonly COLOR_RED="\033[1;31m"
readonly COLOR_GREEN="\033[1;32m"
readonly COLOR_YELLOW="\033[1;33m"
readonly COLOR_BLUE="\033[1;34m"
readonly COLOR_CYAN="\033[1;36m"

# Функции форматированного вывода сообщений
log_info() {
    echo -e "${COLOR_BLUE}[ИНФО]${COLOR_RESET} $*"
}

log_success() {
    echo -e "${COLOR_GREEN}[УСПЕХ]${COLOR_RESET} $*"
}

log_warn() {
    echo -e "${COLOR_YELLOW}[ВНИМАНИЕ]${COLOR_RESET} $*"
}

log_error() {
    echo -e "${COLOR_RED}[ОШИБКА]${COLOR_RESET} $*" >&2
}

# Обработчик критических ошибок
trap_err() {
    local exit_code=$?
    local line_no=$1
    log_error "Скрипт прерван на строке ${line_no} с кодом ошибки ${exit_code}!"
    exit "${exit_code}"
}
trap 'trap_err ${LINENO}' ERR

log_info "=========================================================="
log_info " Запуск скрипта 01_setup_env.sh: Подготовка окружения"
log_info "=========================================================="

# Определение рабочей директории проекта
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

log_info "Корень проекта определен как: ${PROJECT_ROOT}"

# Экспорт неинтерактивного режима для apt
export DEBIAN_FRONTEND=noninteractive
export NEEDRESTART_MODE=a

# Проверка привилегий root для apt-get
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo -E"
        log_info "Скрипт запущен от обычного пользователя. Будет использован sudo."
    else
        log_error "Для установки пакетов требуются права root или установленная утилита sudo."
        exit 1
    fi
fi

# 1. Обновление репозиториев apt
log_info "Шаг 1/6: Обновление индексов пакетов apt..."
${SUDO} apt-get update -y

# 2. Установка необходимых базовых пакетов сборки AOSP, Halium и ядра Linux
log_info "Шаг 2/6: Установка системных зависимостей, компиляторов и утилит..."

DEPENDENCY_PACKAGES=(
    # Базовые инструменты компиляции
    build-essential
    clang
    llvm
    lld
    binutils
    gcc-multilib
    g++-multilib
    gcc-aarch64-linux-gnu
    g++-aarch64-linux-gnu
    crossbuild-essential-arm64
    libc6-dev-i386

    # Утилиты генерации кода и сборки
    bison
    flex
    gperf
    ninja-build
    cmake
    make
    automake
    autoconf
    libtool
    pkg-config
    bc
    rsync
    ccache
    schedtool

    # Работа с архивами, образами и файловыми системами
    zip
    unzip
    tar
    gzip
    bzip2
    xz-utils
    lz4
    squashfs-tools
    e2fsprogs
    f2fs-tools
    dosfstools
    mtools
    android-sdk-libsparse-utils
    device-tree-compiler
    u-boot-tools

    # Библиотеки и заголовки для AOSP / Host tools
    libncurses5
    libncurses5-dev
    libssl-dev
    zlib1g-dev
    libxml2
    libxml2-utils
    xsltproc
    libgl1-mesa-dev
    libgl1-mesa-glx
    libffi-dev
    libelf-dev
    libcap-dev

    # Скриптовые языки и окружение
    python3
    python3-dev
    python3-pip
    python-is-python3
    ruby
    perl

    # Сетевые утилиты и VCS
    curl
    wget
    git
    git-lfs
    ca-certificates
    openssh-client
    gnupg
    fastboot
    adb

    # Java Development Kit (Android 13 использует OpenJDK 11 / 17)
    openjdk-11-jdk
)

# Установка пакетов пакетами с игнорированием уже установленных
${SUDO} apt-get install -y --no-install-recommends "${DEPENDENCY_PACKAGES[@]}"

log_success "Системные пакеты успешно установлены."

# 3. Установка Google Repo
log_info "Шаг 3/6: Проверка и установка утилиты Google 'repo'..."
BIN_DIR="${HOME}/.local/bin"
mkdir -p "${BIN_DIR}"

if ! command -v repo >/dev/null 2>&1; then
    log_info "Загрузка официального бинарника repo..."
    curl -fsSL https://storage.googleapis.com/git-repo-downloads/repo > "${BIN_DIR}/repo"
    chmod a+x "${BIN_DIR}/repo"
    log_success "Утилита repo установлена в ${BIN_DIR}/repo"
else
    log_info "Утилита repo уже установлена: $(command -v repo)"
fi

# Обеспечение наличия $HOME/.local/bin в текущем PATH
export PATH="${BIN_DIR}:${PATH}"

# 4. Установка Bazelisk / Bazel (необходим для сборки GKI 2.0 / Kleaf в Android 13)
log_info "Шаг 4/6: Проверка и установка Bazel / Bazelisk..."
if ! command -v bazel >/dev/null 2>&1 && ! command -v bazelisk >/dev/null 2>&1; then
    log_info "Загрузка Bazelisk (официального лаунчера версий Bazel)..."
    curl -fsSL https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64 -o "${BIN_DIR}/bazel"
    chmod +x "${BIN_DIR}/bazel"
    log_success "Bazelisk успешно установлен как ${BIN_DIR}/bazel"
else
    log_info "Bazel/Bazelisk обнаружен в системе."
fi

# 5. Настройка рабочей структуры каталогов
log_info "Шаг 5/6: Создание рабочей файловой структуры проекта..."
WORK_DIRS=(
    "${PROJECT_ROOT}/src/lineage"
    "${PROJECT_ROOT}/src/kernel"
    "${PROJECT_ROOT}/src/halium"
    "${PROJECT_ROOT}/rootfs/overlays"
    "${PROJECT_ROOT}/rootfs/download"
    "${PROJECT_ROOT}/out/target/panther"
    "${PROJECT_ROOT}/out/kernel"
    "${PROJECT_ROOT}/out/images"
    "${PROJECT_ROOT}/logs"
    "${PROJECT_ROOT}/patches"
    "${PROJECT_ROOT}/local_manifests"
    "${HOME}/.ccache"
)

for dir in "${WORK_DIRS[@]}"; do
    if [ ! -d "${dir}" ]; then
        mkdir -p "${dir}"
        log_info "Создана директория: ${dir}"
    fi
done

# 6. Конфигурация Git и переменных окружения
log_info "Шаг 6/6: Проверка конфигурации Git и переменных окружения..."

GIT_USER_NAME="$(git config --global user.name || echo "")"
GIT_USER_EMAIL="$(git config --global user.email || echo "")"

if [ -z "${GIT_USER_NAME}" ]; then
    log_warn "Имя пользователя Git не задано. Устанавливаем значение по умолчанию: 'Ubuntu Touch Porter'"
    git config --global user.name "Ubuntu Touch Porter"
fi

if [ -z "${GIT_USER_EMAIL}" ]; then
    log_warn "Email пользователя Git не задан. Устанавливаем значение по умолчанию: 'porter@ubtouch-pixel7.local'"
    git config --global user.email "porter@ubtouch-pixel7.local"
fi

# Оптимизация Git для работы с большими репозиториями AOSP
git config --global core.compression 0
git config --global http.postBuffer 1048576000
git config --global color.ui auto

# Конфигурация ccache
export USE_CCACHE=1
export CCACHE_EXEC=/usr/bin/ccache
export CCACHE_DIR="${HOME}/.ccache"
ccache -M 50G >/dev/null 2>&1 || true

# Сохранение конфигурационного файла переменных для использования в последующих шагах
ENV_SETUP_FILE="${PROJECT_ROOT}/.env_setup"
cat <<EOF > "${ENV_SETUP_FILE}"
# Автоматически сгенерированный файл окружения для сборки Ubuntu Touch Pixel 7
export PROJECT_ROOT="${PROJECT_ROOT}"
export PATH="${BIN_DIR}:${PATH}"
export USE_CCACHE=1
export CCACHE_EXEC=/usr/bin/ccache
export CCACHE_DIR="${HOME}/.ccache"
export ANDROID_JACK_VM_ARGS="-Dfile.encoding=UTF-8 -XX:+TieredCompilation -Xmx8G"
export DEVICE=panther
export VENDOR=google
export SOC=gs201
export ANDROID_VERSION=13
export HALIUM_VERSION=13
export ARCH=arm64
export OUT_DIR="${PROJECT_ROOT}/out"
export LOG_DIR="${PROJECT_ROOT}/logs"
export IMAGES_DIR="${PROJECT_ROOT}/out/images"
EOF

log_success "Переменные окружения экспортированы в ${ENV_SETUP_FILE}"
log_success "Окружение полностью настроено и готово к сборке!"
