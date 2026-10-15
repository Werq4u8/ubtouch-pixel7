# Ubuntu Touch для Google Pixel 7 (panther)

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Halium](https://img.shields.io/badge/Halium-13.0_preview-orange.svg)](https://halium.org/)
[![Ubuntu Touch](https://img.shields.io/badge/Ubuntu_Touch-Focal_20.04-green.svg)](https://ubports.com/)
[![Android Base](https://img.shields.io/badge/Android_Base-LineageOS_20_(Android_13)-brightgreen.svg)](https://lineageos.org/)
[![Kernel](https://img.shields.io/badge/Kernel-Linux_5.10_GKI_2.0-purple.svg)](https://android.googlesource.com/kernel/common/)
[![Status](https://img.shields.io/badge/Status-Alpha_Work_in_Progress-yellow.svg)]()

> [!WARNING]
> ### 🚧 Проект находится в стадии активной разработки (Work in Progress / Alpha Bring-up)
> **Текущий статус**: Экспериментальный этап отладки первичной загрузки ядра (Bring-up) под архитектуру Google Tensor G2 (GKI 2.0).
> 
> Проект **не готов для повседневного использования** (Daily Driver). Образы предназначены исключительно для разработчиков и тестирования в оперативной памяти через `fastboot boot` (без изменения постоянной памяти телефона). Если вы не уверены в своих действиях — дождитесь стабильного бета-релиза.

Полностью автоматизированный проект портирования операционной системы **Ubuntu Touch (Lomiri)** на смартфон **Google Pixel 7** (кодовое имя: `panther`, семейство: `pantah`, платформа: `gs201` / Google Tensor G2) на базе **Android 13 (LineageOS 20)** и **Halium 13 / GKI 2.0**.


---

## ⚠️ Важное предупреждение (Disclaimer & Safety)

> [!CAUTION]
> **ВЫ ДЕЙСТВУЕТЕ НА СВОЙ СТРАХ И РИСК!**
> Портирование альтернативной операционной системы связано с глубокой модификацией разделов загрузчика и файловой системы.
> Возможные риски:
> - Полная потеря всех пользовательских данных на устройстве (Wipe).
> - Временная или постоянная потеря работоспособности устройства (Soft-brick / Hard-brick).
> - Аннулирование гарантии производителя.
> Ни создатели проекта, ни сообщество UBports не несут ответственности за поврежденные устройства, сгоревшие чипы или потерянные данные. Обязательно сделайте резервную копию перед началом прошивки!

---

## 📱 Спецификации целевого устройства

| Параметр | Значение |
| :--- | :--- |
| **Устройство** | Google Pixel 7 |
| **Кодовое имя (Device)** | `panther` |
| **Семейство (Device family)** | `pantah` |
| **SoC / Чипсет** | Google Tensor G2 (`gs201` / S5E9855 hybrid) |
| **Архитектура CPU** | 2x Cortex-X1 + 2x Cortex-A78 + 4x Cortex-A55 (ARMv8.2-A, 64-bit) |
| **Графический ускоритель (GPU)** | Arm Mali-G710 MP7 (Vulkan / GLES / Wayland через libhybris) |
| **Базовая версия Android** | Android 13 (API 33, LineageOS 20) |
| **Ядро** | Linux Kernel 5.10 (Google Generic Kernel Image / GKI 2.0, сборка Kleaf/Bazel) |
| **Архитектура HAL** | AIDL (Android Interface Definition Language) с гибридными HIDL-обёртками |
| **Модем (RIL/Cellular)** | Samsung Shannon Exynos 5300 (через oFono / Binder AIDL) |
| **Wi-Fi / Bluetooth** | Broadcom BCM4389 (WLAN `wlan0`, BlueZ / android-binder) |
| **Rootfs Ubuntu Touch** | Ubuntu Touch Focal (20.04 LTS) arm64 rootfs (с поддержкой перехода на Noble 24.04) |

---

## 📋 Требования для установки (Prerequisites)

1. **Смартфон Google Pixel 7 (`panther`)**:
   - Официально разблокированный загрузчик (**Bootloader Unlocked**).
   - Установленная актуальная стоковая прошивка Android 13 (любой релиз 2023 года) в обоих слотах `slot-a` и `slot-b`.
   - Заряд батареи не менее 70%.
2. **Оборудование**:
   - Качественный кабель USB Type-C на Type-C или Type-A (рекомендуется оригинальный кабель).
   - Компьютер под управлением Linux (Ubuntu 20.04/22.04 LTS или через WSL2 под Windows).
3. **Программное обеспечение**:
   - Установленные `fastboot` и `adb` из официального пакета [Android SDK Platform-Tools](https://developer.android.com/tools/releases/platform-tools) (версия >= 34.0.0).
   - Учетная запись GitHub (для сборки через GitHub Actions, если вы не компилируете локально).

---

## 🚀 Быстрый старт (Quick Start)

### 📦 Вариант 0: Скачать готовые образы из Releases (Рекомендуется)

Вам **не нужно ничего компилировать самостоятельно**! Готовые скомпилированные образы автоматически публикуются в разделе **[Releases](https://github.com/Werq4u8/ubtouch-pixel7/releases)**:

1. Перейдите на страницу **[GitHub Releases — Latest Preview](https://github.com/Werq4u8/ubtouch-pixel7/releases)**.
2. Скачайте файлы:
   - **`halium-boot.img`** — ядро с интерактивным загрузчиком для безопасного Live-запуска.
   - **`rootfs.img`** — файловая система Ubuntu Touch Focal arm64.
3. Положите их в папку `out/images/` (или рядом со скриптами).

#### 🧪 Безопасный Live-тест в оперативную память (БЕЗ стирания Android):
1. Переведите Pixel 7 в режим загрузчика (Fastboot Mode):
   ```powershell
   adb reboot bootloader
   ```
2. Загрузите ядро напрямую в оперативную память (RAM):
   ```powershell
   fastboot boot halium-boot.img
   ```
3. Когда на экране смартфона появится приглашение `>>> PLEASE, INSERT UBUNTU-FILE.IMG <<<`, запустите передатчик системы на ПК:
   ```powershell
   .\scripts\insert_ubuntu.ps1
   ```
4. Телефон примет образ по USB и запустит Ubuntu Touch!  
   *(Для возврата в Android просто перезагрузите телефон кнопкой питания)*.

---

### Вариант 1: Самостоятельная сборка через GitHub Actions


В репозитории настроен автоматический CI/CD пайплайн, компилирующий ядро GKI 5.10, дерево Halium и собирающий готовый образ rootfs:

1. **Сделайте Fork** данного репозитория в свой аккаунт GitHub.
2. В форкнутом репозитории перейдите во вкладку **Actions** и включите GitHub Workflows (**Enable Workflows**).
3. Запустите пайплайн **Build Ubuntu Touch for Pixel 7** вручную через меню `Run workflow` (или сделайте push в ветку `main`).
4. По завершении сборки перейдите в раздел **Artifacts** и скачайте архив:
   - `pixel7-ubtouch-images.zip` (содержит `boot.img`, `halium-boot.img`, `system.img`, `rootfs.img`).
5. Распакуйте архив в локальную папку:
   ```bash
   unzip pixel7-ubtouch-images.zip -d ./images/
   ```
6. Переведите смартфон в режим Fastboot (`adb reboot bootloader` или зажмите `Питание + Громкость Вниз`) и запустите скрипт прошивки:
   ```bash
   chmod +x ./scripts/07_flash.sh
   ./scripts/07_flash.sh --images-dir ./images/
   ```

---

### Вариант 2: Локальная сборка на своей машине (Ubuntu 22.04 LTS)

Для полной компиляции локально требуется мощный сервер: минимум **32 ГБ RAM**, **8-16 ядер CPU** и **250+ ГБ свободного места на SSD/NVMe**.

1. Клонируйте репозиторий:
   ```bash
   git clone https://github.com/your-username/ubtouch-pixel7.git
   cd ubtouch-pixel7
   chmod +x scripts/*.sh
   ```
2. Запустите комплексный скрипт автоматической сборки:
   ```bash
   ./scripts/build_all.sh
   ```
   Скрипт пошагово выполнит:
   - Подготовку хост-окружения (`01_setup_env.sh`)
   - Загрузку деревьев LineageOS 20 и Kleaf/GKI ядра (`02_fetch_sources.sh`)
   - Патчинг ядра для Ubuntu Touch / AppArmor / Binder (`03_patch_kernel.sh`)
   - Компиляцию ядра 5.10 через Bazel/Kleaf (`04_build_kernel.sh`)
   - Сборку Halium 13 (`05_build_halium.sh`)
   - Подготовку и упаковку Rootfs (`06_prepare_rootfs.sh`)

3. Прошейте полученные образы:
   ```bash
   ./scripts/07_flash.sh
   ```

---

## 📂 Структура проекта

```
ubtouch-pixel7/
├── .github/
│   └── workflows/
│       └── build.yml             # GitHub Actions CI/CD пайплайн сборки
├── scripts/
│   ├── 01_setup_env.sh          # Установка пакетов, компиляторов, Bazel, Repo
│   ├── 02_fetch_sources.sh       # Загрузка LineageOS 20, vendor blobs и gs201
│   ├── 03_patch_kernel.sh        # Конфигурация ядра (AppArmor, cgroups, binder, GKI ABI)
│   ├── 04_build_kernel.sh        # Сборка ядра Linux 5.10 через Kleaf / Bazel
│   ├── 05_build_halium.sh        # Генерация halium.mk и компиляция halium-boot / system
│   ├── 06_prepare_rootfs.sh      # Загрузка и настройка Ubuntu Touch arm64 rootfs (Focal)
│   ├── 07_flash.sh               # Интерактивная безопасная прошивка через fastboot
│   └── build_all.sh              # Мастер-скрипт оркестрации с трекингом и возобновлением
├── patches/                      # Патчи для ядра, LineageOS и libhybris
├── rootfs/
│   └── overlays/                 # Конфигурационные файлы для Pixel 7 в Ubuntu Touch
│       ├── etc/
│       │   ├── ubuntu-touch-session.d/
│       │   ├── pulse/
│       │   └── udev/rules.d/
├── local_manifests/
│   └── pixel7_panther.xml        # Repo манифест для device, vendor и kernel
└── README.md                     # Документация проекта (данный файл)
```

---

## ⚙️ Технические особенности порта Pixel 7

1. **GKI 2.0 (Generic Kernel Image)**:
   - Pixel 7 использует архитектуру ядра GKI 2.0 (ядро 5.10), собираемое с помощью системы **Kleaf / Bazel**.
   - Для работы Ubuntu Touch необходимо отключить строгую проверку KMI ABI (`ABI_DEFINITION=""`), чтобы добавить поддержку cgroup v1/v2, AppArmor, системных вызовов FUSE/SquashFS, пространств имен (namespaces) и монтирования `overlayfs`.

2. **AIDL HAL переход**:
   - В Android 13 многие системные интерфейсы переведены с HIDL на AIDL (например, `android.hardware.power`, `android.hardware.sensors`).
   - Используются специальные адаптеры и мосты в слое `libhybris` для взаимодействия графики Wayland/Mesa через Mali Gralloc и Binder AIDL.

3. **Графика и Wayland**:
   - GPU Arm Mali-G710 использует проприетарные драйверы DDK от Google/Arm.
   - Оболочка Lomiri взаимодействует с экраном через EGL/GLES и Hybris-hwcomposer / mini-compat-gl.

---

## 🔄 Восстановление стоковой прошивки (Stock Restore)

Если вам необходимо вернуть оригинальную прошивку Google Android:
1. Запустите скрипт прошивки с флагом восстановления:
   ```bash
   ./scripts/07_flash.sh --restore
   ```
2. Либо воспользуйтесь официальным веб-прошивальщиком Google:
   [https://flash.android.com/](https://flash.android.com/)

---

## 🔗 Полезные ссылки и документация

- [Официальный портал UBports](https://ubports.com/)
- [Документация UBports по портированию (Halium 9+)](https://docs.ubports.com/en/latest/porting/introduction/intro_new.html)
- [Проект Halium — Core для запуска Linux на Android-устройствах](https://halium.org/)
- [Kleaf: Построение ядер Android через Bazel](https://source.android.com/docs/setup/build/building-kernels-kleaf)
- [LineageOS Source Code Repository](https://github.com/LineageOS)
- [Исходные коды ядра Android Google Pixel](https://android.googlesource.com/kernel/devices/google/pantah/)

---

## 📄 Лицензия

Данный проект распространяется под лицензией **Apache 2.0**.
Подробности смотрите в файле [LICENSE](file:///C:/Users/rootina/.gemini/antigravity/scratch/ubtouch-pixel7/LICENSE) или на сайте [Apache Licenses](https://www.apache.org/licenses/LICENSE-2.0).
Компоненты ядра Linux распространяются под лицензией **GNU GPL v2**.
Компоненты Ubuntu Touch распространяются под соответствующими свободными лицензиями проекта UBports (GPL, LGPL, Apache).
