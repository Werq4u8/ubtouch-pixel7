# ============================================================================
# insert_ubuntu.ps1 — Передача ubuntu-file.img на Pixel 7 по запросу ядра
# ============================================================================
# Запускается на Windows ПК, когда на экране телефона появляется:
#   "PLEASE, INSERT UBUNTU-FILE.IMG"
# ============================================================================
param(
    [string]$ImagePath = "$PSScriptRoot\..\out\rootfs.img",
    [string]$PhoneIp = "10.15.19.82",
    [int]$PhonePort = 9000
)

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║  💿 ПЕРЕДАЧА UBUNTU-FILE.IMG В ЯДРО PIXEL 7 ПО USB          ║" -ForegroundColor Cyan
Write-Host "╚══════════════════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $ImagePath)) {
    # Проверим также имя ubuntu-file.img или ubuntu.img в текущей папке
    foreach ($alt in @("ubuntu-file.img", "ubuntu.img", "rootfs.img")) {
        if (Test-Path $alt) {
            $ImagePath = (Resolve-Path $alt).Path
            break
        }
    }
}

if (-not (Test-Path $ImagePath)) {
    Write-Host "[❌] Файл образа не найден: $ImagePath" -ForegroundColor Red
    Write-Host "    Укажите путь: .\scripts\insert_ubuntu.ps1 -ImagePath C:\path\to\ubuntu-file.img" -ForegroundColor Yellow
    exit 1
}

$FileInfo = Get-Item $ImagePath
$TotalMB = [math]::Round($FileInfo.Length / 1MB, 1)
Write-Host "[📦] Образ готов к вставке: $($FileInfo.FullName) ($TotalMB МБ)" -ForegroundColor Green
Write-Host "[⏳] Ожидаю запрос 'PLEASE, INSERT UBUNTU-FILE.IMG' от Pixel 7 ($PhoneIp`:$PhonePort)..." -ForegroundColor Yellow

# Ждём пинга от телефона по USB RNDIS
while ($true) {
    if (Test-Connection -ComputerName $PhoneIp -Count 1 -Quiet -ErrorAction SilentlyContinue) {
        Write-Host "[🔌] Телефон обнаружен по USB ($PhoneIp)! Подключаюсь к порту $PhonePort..." -ForegroundColor Green
        break
    }
    Start-Sleep -Seconds 1
}

# Отправляем файл по TCP с отображением прогресс-бара
$Client = New-Object System.Net.Sockets.TcpClient
$Connected = $false
for ($i = 0; $i -lt 30; $i++) {
    try {
        $Client.Connect($PhoneIp, $PhonePort)
        $Connected = $true
        break
    } catch {
        Start-Sleep -Seconds 1
    }
}

if (-not $Connected) {
    Write-Host "[❌] Не удалось подключиться к $PhoneIp`:$PhonePort" -ForegroundColor Red
    exit 1
}

Write-Host "[🚀] Начинаю передачу ubuntu-file.img в ядро..." -ForegroundColor Cyan
$Stream = $Client.GetStream()
$FileStream = [System.IO.File]::OpenRead($FileInfo.FullName)
$Buffer = New-Object byte[] (1024 * 1024) # 1 MB buffer
$SentBytes = 0L

try {
    while (($Read = $FileStream.Read($Buffer, 0, $Buffer.Length)) -gt 0) {
        $Stream.Write($Buffer, 0, $Read)
        $SentBytes += $Read
        $Pct = [int](($SentBytes * 100) / $FileInfo.Length)
        $SentMB = [math]::Round($SentBytes / 1MB, 1)
        Write-Progress -Activity "Вставка ubuntu-file.img в Pixel 7" -Status "$SentMB МБ / $TotalMB МБ ($Pct%)" -PercentComplete $Pct
    }
    $Stream.Flush()
} finally {
    $FileStream.Close()
    $Stream.Close()
    $Client.Close()
}

Write-Progress -Activity "Вставка ubuntu-file.img в Pixel 7" -Completed
Write-Host ""
Write-Host "[✅] Образ успешно передан! Ядро Pixel 7 монтирует систему и запускает Ubuntu Touch." -ForegroundColor Green
