#!/bin/sh
# ============================================================================
# usb-insert-hook.sh — Интерактивный загрузчик rootfs по USB в initramfs
# ============================================================================
# Запускается внутри initramfs (halium-boot.img) до монтирования корня.
# Выводит на экран Pixel 7 (simplefb / drmfb / fbcon) сообщение:
#   "PLEASE, INSERT UBUNTU-FILE.IMG OVER USB..."
# и принимает образ с ПК:
#   - Режим 1 (Поток в RAM/zram или в /tmpmnt/unencrypted): порт TCP 9000
#   - Режим 2 (Виртуальный диск по кабелю — NBD): порт TCP 10809
# ============================================================================

USB_IP="10.15.19.82"
PC_IP="10.15.19.100"
LISTEN_PORT="9000"
NBD_PORT="10809"

# --- Вывод сообщения на экран телефона (fb0 / tty0) ---
show_screen_banner() {
    MSG_TITLE="$1"
    MSG_SUB="$2"
    MSG_STATUS="$3"

    # Включаем подсветку экрана, если погашена
    for bl in /sys/class/backlight/*; do
        if [ -f "$bl/max_brightness" ]; then
            cat "$bl/max_brightness" > "$bl/brightness" 2>/dev/null || true
        fi
    done

    # Разблокируем framebuffer blanking
    if [ -f /sys/class/graphics/fb0/blank ]; then
        echo 0 > /sys/class/graphics/fb0/blank 2>/dev/null || true
    fi

    # Выводим на экранную консоль (/dev/tty0 и /dev/tty1) и в kmsg
    for tty_dev in /dev/tty0 /dev/tty1 /dev/console; do
        if [ -c "$tty_dev" ]; then
            {
                printf "\033[2J\033[H"
                printf "\n\n\n"
                printf "  ==================================================\n"
                printf "       UBUNTU TOUCH LOADER — GOOGLE PIXEL 7         \n"
                printf "  ==================================================\n\n"
                printf "   >>> %s <<<\n\n" "$MSG_TITLE"
                printf "   %s\n\n" "$MSG_SUB"
                printf "   STATUS: %s\n" "$MSG_STATUS"
                printf "   USB IP: %s (Port %s)\n" "$USB_IP" "$LISTEN_PORT"
                printf "  ==================================================\n"
            } > "$tty_dev" 2>/dev/null || true
        fi
    done

    echo "[UBTOUCH-USB-LOADER] $MSG_TITLE | $MSG_SUB | $MSG_STATUS" > /dev/kmsg 2>/dev/null || true
}

# --- Поднятие USB RNDIS сети ---
setup_usb_network() {
    mkdir -p /config
    mount -t configfs none /config 2>/dev/null || true
    GADGET="/config/usb_gadget/g1"

    if [ ! -d "$GADGET" ]; then
        mkdir -p "$GADGET"
        echo "0x18d1" > "$GADGET/idVendor"
        echo "0xd001" > "$GADGET/idProduct"
        mkdir -p "$GADGET/strings/0x409"
        echo "Google" > "$GADGET/strings/0x409/manufacturer"
        echo "Pixel 7 Ubuntu Loader" > "$GADGET/strings/0x409/product"
        echo "PANTHER-UBTOUCH" > "$GADGET/strings/0x409/serialnumber"

        mkdir -p "$GADGET/functions/rndis.usb0"
        mkdir -p "$GADGET/configs/c.1/strings/0x409"
        echo "RNDIS" > "$GADGET/configs/c.1/strings/0x409/configuration"
        ln -s "$GADGET/functions/rndis.usb0" "$GADGET/configs/c.1/" 2>/dev/null || true

        UDC_DEV=$(ls /sys/class/udc | head -n 1)
        if [ -n "$UDC_DEV" ]; then
            echo "$UDC_DEV" > "$GADGET/UDC" 2>/dev/null || true
        fi
    fi

    # Назначаем IP интерфейсу usb0 или rndis0
    for iface in usb0 rndis0; do
        if [ -d "/sys/class/net/$iface" ]; then
            ip link set "$iface" up 2>/dev/null || ifconfig "$iface" "$USB_IP" netmask 255.255.255.0 up
            ip addr add "$USB_IP/24" dev "$iface" 2>/dev/null || true
        fi
    done
}

# --- Ожидание и приём ubuntu-file.img с ПК ---
wait_for_ubuntu_image() {
    TARGET_IMG="/tmpmnt/ubuntu.img"

    # Если образ уже есть и валиден — сразу используем
    if [ -f "$TARGET_IMG" ] && [ -s "$TARGET_IMG" ]; then
        show_screen_banner "FOUND EXISTING UBUNTU-FILE.IMG" "Mounting local rootfs image..." "OK"
        return 0
    fi

    setup_usb_network

    show_screen_banner \
        "PLEASE, INSERT UBUNTU-FILE.IMG" \
        "Run 'scripts/insert_ubuntu.ps1' on your PC to send or mount the image." \
        "WAITING FOR USB CONNECTION..."

    # Создаём RAM/zram или unencrypted хранилище для приёма файла, если /tmpmnt недоступен
    if ! mountpoint -q /tmpmnt 2>/dev/null; then
        mkdir -p /tmpmnt
        # Выделяем tmpfs / zram под образ
        mount -t tmpfs -o size=3500M tmpfs /tmpmnt
    fi

    # Слушаем порт 9000: когда ПК подключается, принимаем поток в $TARGET_IMG
    while [ ! -s "$TARGET_IMG" ]; do
        # Проверяем, не поднял ли ПК NBD-сервер (виртуальный диск по кабелю без копирования)
        if command -v nbd-client >/dev/null 2>&1 && ping -c 1 -W 1 "$PC_IP" >/dev/null 2>&1; then
            if nbd-client "$PC_IP" "$NBD_PORT" /dev/nbd0 -persist 2>/dev/null; then
                show_screen_banner \
                    "UBUNTU-FILE.IMG CONNECTED (NBD STREAM)" \
                    "Mounting rootfs directly from PC over USB..." \
                    "MOUNTING /dev/nbd0..."
                ln -sf /dev/nbd0 "$TARGET_IMG"
                return 0
            fi
        fi

        # Иначе ждём прямой передачи файла по TCP 9000 через nc (netcat)
        show_screen_banner \
            "PLEASE, INSERT UBUNTU-FILE.IMG" \
            "Send file to $USB_IP:$LISTEN_PORT (Run insert_ubuntu.ps1 on PC)" \
            "LISTENING ON PORT $LISTEN_PORT..."

        nc -l -p "$LISTEN_PORT" > "$TARGET_IMG.part" 2>/dev/null || \
            nc -l "$LISTEN_PORT" > "$TARGET_IMG.part" 2>/dev/null || sleep 2

        if [ -s "$TARGET_IMG.part" ]; then
            SIZE_MB=$(du -m "$TARGET_IMG.part" | cut -f1)
            mv "$TARGET_IMG.part" "$TARGET_IMG"
            show_screen_banner \
                "UBUNTU-FILE.IMG RECEIVED (${SIZE_MB} MB)!" \
                "Verifying and mounting rootfs..." \
                "BOOTING UBUNTU TOUCH..."
            sleep 2
            return 0
        fi
    done
}

wait_for_ubuntu_image
