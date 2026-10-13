# AIDL Bridges for Ubuntu Touch on Google Pixel 7 (panther)

Набор сервисов-мостов (AIDL Bridges) для обеспечения аппаратной совместимости между базой **Android 13 (API 33)** и операционной системой **Ubuntu Touch (Focal/Noble)** на смартфоне **Google Pixel 7** (`panther`, платформа `pantah`, чипсет Google Tensor G2 / GS201).

---

## Архитектурный контекст (Architecture Overview)

Исторически Halium и библиотека `libhybris` в Ubuntu Touch проектировались для работы с аппаратным слоем Android через **HIDL** (`android.hardware.*@X.Y::I...`). Начиная с Android 12 и в полной мере в Android 13, компания Google перевела ключевые аппаратные HAL-интерфейсы на **AIDL** (Android Interface Definition Language).

Из-за отсутствия прямой поддержки AIDL в классическом `libhybris` на стороне glibc, в данном проекте реализована гибридная архитектура **Bridge Daemons**:

```
+-------------------------------------------------------------------------+
|                  Ubuntu Touch Host (glibc, rootfs)                      |
|                                                                         |
|  [Ofono / RIL Plugin]   [PulseAudio / PipeWire]   [Camera Service]  ... |
+-------------------------------------------------------------------------+
                                    ▲
                                    │ Unix Domain Sockets (/dev/socket/ubtouch-*)
                                    │ (Zero-copy memfd / shm for media buffers)
                                    ▼
+-------------------------------------------------------------------------+
|             Android / Bionic Environment (LXC Container)                |
|                                                                         |
|  [ubtouch-radio-bridge]  [ubtouch-audio-bridge]  [ubtouch-camera-bridge] |
|                                                                         |
|          Binder NDK (libbinder_ndk / AServiceManager)                   |
|                                                                         |
|             AIDL HAL Services (Vendor / System partitions)              |
|   - android.hardware.radio.* (Exynos 5300 Shannon Modem)                |
|   - android.hardware.audio.core (Tensor Audio DSP)                      |
|   - android.hardware.camera.provider (ISOCELL GN1 + IMX381 + Tensor ISP)|
|   - android.hardware.gnss (Tensor GNSS Subsystem)                       |
+-------------------------------------------------------------------------+
```

Каждый мост представляет собой нативный C++ демон, скомпилированный под Bionic, работающий внутри контейнера Android и транслирующий вызовы между стандартным Unix Domain сокетом (со стороны Ubuntu Touch) и AIDL Binder сервисами (со стороны Android).

---

## Каталог компонентов

### 1. Common Protocol (`aidl-bridges/common/bridge_protocol.h`)
- Заголовочный файл бинарного протокола `UBTP` (`0x55425450`, версия 1).
- Упакованные структуры заголовков (`MessageHeader`), коды команд и индикаций.
- Утилиты сериализации/десериализации буферов (`BufferWriter`, `BufferReader`).
- Сокетные хелперы для передачи данных и дескрипторов файлов (`sendFd` / `recvFd` через `SCM_RIGHTS`).

### 2. Radio Bridge (`aidl-bridges/radio-bridge/`)
- **Исполняемый файл**: `/vendor/bin/ubtouch-radio-bridge`
- **Сокет**: `/dev/socket/ubtouch-radio`
- **Интерфейсы AIDL**:
  - `android.hardware.radio.voice.IRadioVoice`
  - `android.hardware.radio.network.IRadioNetwork`
  - `android.hardware.radio.sim.IRadioSim`
  - `android.hardware.radio.data.IRadioData`
  - `android.hardware.radio.modem.IRadioModem`
  - `android.hardware.radio.messaging.IRadioMessaging`
- **Возможности**:
  - Управление звонками: `DIAL`, `HANGUP`, `ANSWER`.
  - Мониторинг сети и сигнала: `GET_SIGNAL_STRENGTH`, `GET_OPERATOR`, `SET_NETWORK_TYPE`.
  - Передача данных: `SETUP_DATA_CALL`, `DEACTIVATE_DATA_CALL`.
  - Обмен SMS (PDU) и USSD-запросы.
  - Автоматическое восстановление соединения при перезагрузке модема Shannon 5300 через `AIBinder_DeathRecipient`.

### 3. Camera Bridge (`aidl-bridges/camera-bridge/`)
- **Исполняемый файл**: `/vendor/bin/ubtouch-camera-bridge`
- **Сокет**: `/dev/socket/ubtouch-camera`
- **Интерфейсы AIDL**:
  - `android.hardware.camera.provider.ICameraProvider`
  - `android.hardware.camera.device.ICameraDevice`
  - `android.hardware.camera.device.ICameraDeviceSession`
- **Возможности**:
  - Потоковое вещание превью-кадров (NV21 / YUV420) с нулевым копированием через разделяемую память `memfd`.
  - Передача файлового дескриптора буфера клиенту по Unix сокету.
  - Поддержка переключения основной камеры (Samsung GN1) и фронтальной камеры (Sony IMX381).
  - Съемка фотокадров и автофокусировка.

### 4. Audio Bridge (`aidl-bridges/audio-bridge/`)
- **Исполняемый файл**: `/vendor/bin/ubtouch-audio-bridge`
- **Сокет**: `/dev/socket/ubtouch-audio`
- **Интерфейсы AIDL**:
  - `android.hardware.audio.core.IModule`
  - `android.hardware.audio.core.IStreamOut`
  - `android.hardware.audio.core.IStreamIn`
- **Возможности**:
  - Низколатентный кольцевой буфер в `memfd` с атомарными указателями чтения/записи для воспроизведения и захвата звука.
  - Поддержка частоты дискретизации 48 кГц, стерео PCM 16-бит.
  - Регулировка громкости (`SET_VOLUME`), отключение звука (`SET_MUTE`).
  - Переключение аудио-маршрутов (`SET_ROUTING`: динамик, разговорный динамик, гарнитура, BT SCO).

### 5. GNSS Bridge (`aidl-bridges/gnss-bridge/`)
- **Исполняемый файл**: `/vendor/bin/ubtouch-gnss-bridge`
- **Сокет**: `/dev/socket/ubtouch-gnss`
- **Интерфейсы AIDL**:
  - `android.hardware.gnss.IGnss`
  - `android.hardware.gnss.IGnssCallback`
- **Возможности**:
  - Запуск и остановка позиционирования (`START`, `STOP`).
  - Прямая трансляция "сырых" NMEA-сообщений (`$GPGGA`, `$GPRMC`, `$GPGSV`) в сокет для штатной службы геолокации Ubuntu Touch и `gpsd`.
  - Трансляция точных координат и статуса спутников (`IND_LOCATION`, `IND_SV_STATUS`).

---

## Сборка в дереве Android 13 (LineageOS 20)

1. Разместите папку `aidl-bridges` в дереве сборки:
   ```bash
   cp -r aidl-bridges $ANDROID_BUILD_TOP/hardware/ubtouch/
   ```

2. Настройте окружение сборки:
   ```bash
   source build/envsetup.sh
   lunch lineage_panther-userdebug
   ```

3. Запустите компиляцию демонов:
   ```bash
   m ubtouch-radio-bridge ubtouch-camera-bridge ubtouch-audio-bridge ubtouch-gnss-bridge
   ```

Скомпилированные бинарники и init-скрипты будут установлены в `/vendor/bin/` и `/vendor/etc/init/`.
