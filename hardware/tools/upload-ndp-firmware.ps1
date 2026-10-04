<#
.SYNOPSIS
    Uploads the NDP120 firmware packages to the Nicla Voice's external flash.

.DESCRIPTION
    Requires tools/ndp_provision to be running on the board (VS Code task
    "Nicla: Flash NDP Provisioner"). Checks the flash is mounted (formatting it only if it
    isn't, or with -Format), sends each .synpkg over YMODEM, then lists the flash with sizes
    and sha256 checksums. Afterwards, flash this project's firmware again
    ("Nicla: Upload (slow)").

    The YMODEM sender is built in, matching the receiver in tools/ndp_provision/ymodem.cpp
    exactly, instead of Arduino's syntiant-uploader (which fails against this board with
    "Didn't get a nak when expected").

    Close every serial monitor on the port first, or opening it fails with "Access is denied".

.PARAMETER Port
    The Nicla Voice's serial port, e.g. COM8.

.PARAMETER Format
    Format the flash even if it already mounts. Without it, the flash is only formatted
    when it can't be mounted.

.PARAMETER FirmwareDir
    Folder holding the .synpkg files, from Arduino's nicla_voice_uploader_and_firmwares.zip.
#>
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [string]$FirmwareDir = "$env:USERPROFILE\Downloads\nicla_voice_uploader_and_firmwares\ndp120",
    [switch]$Format
)

$ErrorActionPreference = 'Stop'

# Same order and names as ndp_config in src/configs.h.
$packages = @(
    'mcu_fw_120_v91.synpkg',
    'dsp_firmware_v91.synpkg',
    'alexa_334_NDP120_B0_v11_v91.synpkg'
)
foreach ($p in $packages) {
    if (-not (Test-Path (Join-Path $FirmwareDir $p))) { throw "$p not found in $FirmwareDir" }
}

# YMODEM control bytes (see ymodem.h).
$SOH = [byte]0x01; $STX = [byte]0x02; $EOT = [byte]0x04; $ACK = [byte]0x06
$NAK = [byte]0x15; $CA = [byte]0x18; $POLL = [byte]0x43  # 'C'

#region Serial helpers

# Reads the provisioner's text output until $until matches or $timeoutSeconds passes.
function Read-TextUntil([string]$until, [int]$timeoutSeconds) {
    $output = ''
    $deadline = (Get-Date).AddSeconds($timeoutSeconds)
    while ((Get-Date) -lt $deadline -and $output -notmatch $until) {
        Start-Sleep -Milliseconds 100
        $output += $serial.ReadExisting()
    }
    return $output
}

# Sends a one-letter command to the provisioner and returns its text reply.
function Invoke-ProvisionerCommand([string]$command, [string]$until, [int]$timeoutSeconds) {
    $serial.DiscardInBuffer()
    $serial.Write($command)
    return Read-TextUntil $until $timeoutSeconds
}

# Waits for one of the $wanted bytes, skipping any in $ignore. Returns the byte, or throws on
# timeout or a cancel (CA) from the receiver.
function Wait-ForByte([byte[]]$wanted, [byte[]]$ignore, [int]$timeoutMs, [string]$what) {
    $deadline = (Get-Date).AddMilliseconds($timeoutMs)
    while ((Get-Date) -lt $deadline) {
        if ($serial.BytesToRead -eq 0) { Start-Sleep -Milliseconds 5; continue }
        $b = [byte]$serial.ReadByte()
        if ($wanted -contains $b) { return $b }
        if ($b -eq $CA) { throw "Receiver cancelled the transfer while waiting for $what." }
        if ($ignore -notcontains $b) { Write-Verbose ("ignored 0x{0:X2} while waiting for {1}" -f $b, $what) }
    }
    throw "Timed out waiting for $what."
}

#endregion

#region YMODEM sender

# CRC-16/XMODEM (poly 0x1021, init 0), as checked by ymodem.cpp's crc16(). Compiled, since a
# PowerShell loop over ~410 KB of firmware would take minutes.
if (-not ('NdpYmodem.Crc' -as [type])) {
    Add-Type -TypeDefinition @'
namespace NdpYmodem {
    public static class Crc {
        public static ushort Crc16(byte[] buffer, int offset, int count) {
            int crc = 0;
            for (int i = offset; i < offset + count; i++) {
                crc ^= buffer[i] << 8;
                for (int bit = 0; bit < 8; bit++) {
                    crc = (crc & 0x8000) != 0 ? ((crc << 1) ^ 0x1021) : (crc << 1);
                    crc &= 0xFFFF;
                }
            }
            return (ushort)crc;
        }
    }
}
'@
}

function Get-Crc16([byte[]]$buffer, [int]$offset, [int]$count) {
    return [NdpYmodem.Crc]::Crc16($buffer, $offset, $count)
}

# Builds one packet: header, seq, ~seq, payload padded to $size, CRC16 big-endian.
function New-Packet([byte]$header, [int]$seq, [int]$size, [byte[]]$payload, [byte]$pad) {
    $packet = New-Object byte[] ($size + 5)
    $packet[0] = $header
    $packet[1] = [byte]($seq -band 0xFF)
    $packet[2] = [byte](255 - ($seq -band 0xFF))
    for ($i = 0; $i -lt $size; $i++) { $packet[3 + $i] = if ($i -lt $payload.Length) { $payload[$i] } else { $pad } }
    $crc = Get-Crc16 $packet 3 $size
    $packet[$size + 3] = [byte](($crc -shr 8) -band 0xFF)
    $packet[$size + 4] = [byte]($crc -band 0xFF)
    return ,$packet
}

# Sends a packet until the receiver ACKs it. Stale 'C' polls are skipped.
function Send-Packet([byte[]]$packet, [string]$what) {
    for ($attempt = 1; $attempt -le 10; $attempt++) {
        $serial.Write($packet, 0, $packet.Length)
        $reply = Wait-ForByte @($ACK, $NAK) @($POLL) 5000 "ACK for $what"
        if ($reply -eq $ACK) { return }
    }
    throw "Receiver kept rejecting $what."
}

# Sends one file: 'Y' command, filename block, 1K data blocks, EOT handshake, end-of-session block.
function Send-YmodemFile([string]$path) {
    $name = [System.IO.Path]::GetFileName($path)
    $data = [System.IO.File]::ReadAllBytes($path)

    # Start the provisioner's YMODEM receiver; it echoes 'Y', then polls with 'C'.
    $serial.DiscardInBuffer()
    $serial.Write('Y')
    Wait-ForByte @([byte][char]'Y') @() 5000 "the provisioner's 'Y' reply" | Out-Null
    Wait-ForByte @($POLL) @() 5000 "the receiver's first 'C' poll" | Out-Null

    # Block 0: "name\0size " - the receiver reads the size up to the space.
    $header = [System.Text.Encoding]::ASCII.GetBytes("$name`0$($data.Length) ")
    Send-Packet (New-Packet $SOH 0 128 $header 0) "the filename block"
    Wait-ForByte @($POLL) @() 5000 "the 'C' after the filename block" | Out-Null

    $blocks = [math]::Ceiling($data.Length / 1024)
    for ($b = 0; $b -lt $blocks; $b++) {
        $count = [math]::Min(1024, $data.Length - $b * 1024)
        $chunk = New-Object byte[] $count
        [Array]::Copy($data, $b * 1024, $chunk, 0, $count)
        Send-Packet (New-Packet $STX ($b + 1) 1024 $chunk 0x1A) "block $($b + 1)/$blocks"
        Write-Progress -Activity "Sending $name" -Status "$($b + 1)/$blocks blocks" -PercentComplete (100 * ($b + 1) / $blocks)
    }
    Write-Progress -Activity "Sending $name" -Completed

    # End of file: EOT -> NAK, EOT -> ACK (+ 'C'), then an empty block 0 ends the session.
    $serial.Write([byte[]]@($EOT), 0, 1)
    Wait-ForByte @($NAK) @($POLL) 5000 "NAK after the first EOT" | Out-Null
    $serial.Write([byte[]]@($EOT), 0, 1)
    Wait-ForByte @($ACK) @($POLL) 5000 "ACK after the second EOT" | Out-Null
    Send-Packet (New-Packet $SOH 0 128 @() 0) "the end-of-session block"

    # The receiver times out (1 s), closes the temp file, and renames it to $name.
    Start-Sleep -Milliseconds 1500
    Write-Host "  $name sent ($($data.Length) bytes)."
}

#endregion

$serial = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$serial.DtrEnable = $true
$serial.RtsEnable = $true
$serial.ReadTimeout = 1000
$serial.WriteTimeout = 5000
$serial.Open()
try {
    Start-Sleep -Milliseconds 300

    Write-Host "Checking the provisioner is running on $Port ..."
    $status = Invoke-ProvisionerCommand 'I' 'FLASH .*\r?\n' 5
    if ($status -notmatch 'FLASH ') {
        throw "No reply from the provisioner on $Port. Run the 'Nicla: Flash NDP Provisioner' task first, close every serial monitor, and replug the board if needed."
    }
    Write-Host $status.Trim()

    # The provisioner doesn't mount at boot, so a damaged filesystem can't hang it there.
    $mount = ''
    if (-not $Format) {
        Write-Host "Mounting the flash ..."
        $mount = Invoke-ProvisionerCommand 'M' 'MOUNT (OK|FAILED.*)\r?\n' 30
        Write-Host $mount.Trim()
        if ($mount -notmatch 'MOUNT (OK|FAILED)') {
            throw "The board stopped responding while mounting the flash. Replug it, then rerun this task with -Format (edit the task, or run: .\tools\upload-ndp-firmware.ps1 -Port $Port -Format)."
        }
    }

    # Only format when the flash won't mount (or when asked): an existing LittleFS just needs the
    # files written, and a format is slow enough that the board has been seen to reset partway.
    if ($Format -or $mount -notmatch 'MOUNT OK') {
        Write-Host "Formatting external flash (can take a while) ..."
        $format = Invoke-ProvisionerCommand 'F' 'FORMAT (OK|FAILED.*)|PROVISIONER READY' 300
        Write-Host $format.Trim()
        if ($format -match 'PROVISIONER READY') {
            throw "The board reset during the format. Check the LED: green means the flash mounted anyway, so rerun this task (it will skip formatting)."
        }
        if ($format -notmatch 'FORMAT OK') {
            throw "Formatting the external flash failed or timed out. The LED is red if the flash couldn't be mounted."
        }
    } else {
        Write-Host "Flash already mounted, skipping format (pass -Format to force one)."
    }

    foreach ($p in $packages) {
        Write-Host "Sending $p ..."
        Send-YmodemFile (Join-Path $FirmwareDir $p)
    }

    Write-Host "Files now on the board's flash (name, then size and sha256):"
    $listing = Invoke-ProvisionerCommand 'S' '(?m)^END' 60
    Write-Host $listing.Trim()
    foreach ($p in $packages) {
        $expected = (Get-FileHash (Join-Path $FirmwareDir $p) -Algorithm SHA256).Hash
        if ($listing -notmatch [regex]::Escape($expected)) { throw "$p is missing from the flash or its checksum doesn't match." }
    }
    Write-Host "All NDP firmware packages uploaded and verified. Now flash the main firmware again (task 'Nicla: Upload (slow)')."
} finally {
    $serial.Close()
}
