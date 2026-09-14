# ============================================================================
#  gen_player_posture_bites.ps1
#    자세별 **물기** 행 두 장.
#      12행 = 웅크려 물기 (10행 자세에서)
#      13행 = 엎드려 물기 ( 7행 자세에서)
#    384x768 (12행) -> 384x896 (14행)
#
#  ---- ★ 왜 필요한가 ----
#    물기는 무기를 쓰지 않는데, 자세별 그림이 없어서 **자세 행을 그대로**
#    쓰고 있었다. 그 행들은 **반복(loop)** 이라 공격해도 아무 일도 안 일어나
#    보였고, 엎드린 쪽은 칼 그림(11행)을 빌려 쓸 수도 없다 —
#    이빨로 무는데 칼이 나가면 안 된다.
#
#  ---- ★ 이빨만 그린다 ----
#    머리를 앞으로 던지는 방식(gen_player_bite.ps1)은 서 있는 자세라 됐다.
#    웅크린 자세는 머리가 **위**에 있어서 앞으로 밀면 몸에서 떨어진다.
#    그래서 자세는 그대로 두고 **머리 앞쪽에 이빨만** 낸다 —
#    타격 프레임(2·3)에만 나오므로 「지금 물었다」가 그것만으로 읽힌다.
#
#  ---- 프레임 ----
#      0-1 준비  /  2-3 ★타격(이빨)  /  4-5 후딜
#    6프레임 x 4틱 = 24 = kBite 의 총 길이. startup 8 / 4 = 프레임 2 ✔
#
#  ★ 돌린 뒤 **gen_player_arms.ps1 을 다시** 돌릴 것.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_player_posture_bites.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root "assets\textures"
$srcPath = Join-Path $dir "player.png"

$CELL      = 64
$KEEP_ROWS = 12          # 0~11 을 그대로 옮긴다
$FRAMES    = 6

# (자세 행, 만들 행)
$jobs = @(
    @{ from = 10; to = 12; name = "crouch" },
    @{ from =  7; to = 13; name = "prone"  }
)

$SKIN_R = 224; $SKIN_G = 198; $SKIN_B = 168
$colTooth   = [System.Drawing.Color]::FromArgb(255, 246, 246, 250)
$colOutline = [System.Drawing.Color]::FromArgb(255,  18,  18,  26)

# 이빨이 나오는 프레임
$biteFrames = @(2, 3)


$srcBytes  = [System.IO.File]::ReadAllBytes($srcPath)
$srcStream = New-Object System.IO.MemoryStream(,$srcBytes)
$srcImage  = [System.Drawing.Image]::FromStream($srcStream)
$W = $srcImage.Width
$bmSrc = New-Object System.Drawing.Bitmap($W, $srcImage.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g0 = [System.Drawing.Graphics]::FromImage($bmSrc)
$g0.DrawImage($srcImage, 0, 0, $W, $srcImage.Height)
$g0.Dispose(); $srcImage.Dispose(); $srcStream.Dispose()

if ($bmSrc.Height -lt $KEEP_ROWS * $CELL) {
    throw ("player.png 가 너무 작다 ({0}). 앞선 생성 스크립트를 먼저 돌릴 것" -f $bmSrc.Height)
}

$maxRow = 0
foreach ($j in $jobs) { if ($j.to -gt $maxRow) { $maxRow = $j.to } }

$outH = ($maxRow + 1) * $CELL
$bmOut = New-Object System.Drawing.Bitmap($W, $outH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmOut)
$g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
$keep = New-Object System.Drawing.Rectangle(0, 0, $W, ($KEEP_ROWS * $CELL))
$g.DrawImage($bmSrc, $keep, $keep, [System.Drawing.GraphicsUnit]::Pixel)

foreach ($j in $jobs) {
    $srcCell = New-Object System.Drawing.Rectangle(0, ($j.from * $CELL), $CELL, $CELL)
    for ($c = 0; $c -lt $FRAMES; $c++) {
        $dst = New-Object System.Drawing.Rectangle(($c * $CELL), ($j.to * $CELL), $CELL, $CELL)
        $g.DrawImage($bmSrc, $dst, $srcCell, [System.Drawing.GraphicsUnit]::Pixel)
    }
}
$g.Dispose(); $bmSrc.Dispose()


# ---- 머리(피부색)의 앞쪽 끝을 찾아 이빨을 낸다 ----
$rect = New-Object System.Drawing.Rectangle(0, 0, $bmOut.Width, $bmOut.Height)
$data = $bmOut.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $data.Stride
$buf = New-Object byte[] ($stride * $bmOut.Height)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
$bmOut.UnlockBits($data)

function Fill($bmp, $ox, $oy, $x0, $y0, $x1, $y1, $tone)
{
    for ($y = [Math]::Max($y0, 0); $y -le [Math]::Min($y1, $CELL - 1); $y++) {
        for ($x = [Math]::Max($x0, 0); $x -le [Math]::Min($x1, $CELL - 1); $x++) {
            $bmp.SetPixel($ox + $x, $oy + $y, $tone)
        }
    }
}

foreach ($j in $jobs) {
    $oy = $j.to * $CELL

    # 0번 칸에서 머리의 앞쪽 끝(피부색의 가장 오른쪽)과 그 세로 범위를 찾는다
    $headMaxX = -1; $headTop = 999; $headBot = -1
    for ($y = 0; $y -lt $CELL; $y++) {
        $rowOff = ($oy + $y) * $stride
        for ($x = 0; $x -lt $CELL; $x++) {
            $i = $rowOff + $x * 4
            if ($buf[$i + 3] -eq 0) { continue }
            if ($buf[$i + 2] -ne $SKIN_R -or $buf[$i + 1] -ne $SKIN_G -or $buf[$i] -ne $SKIN_B) { continue }
            if ($x -gt $headMaxX) { $headMaxX = $x }
            if ($y -lt $headTop)  { $headTop  = $y }
            if ($y -gt $headBot)  { $headBot  = $y }
        }
    }
    if ($headMaxX -lt 0) { throw ("{0} : 머리(피부색)를 못 찾았다" -f $j.name) }

    # 입은 머리의 아래쪽 1/3 쯤에 둔다
    $mouthY = [int]($headTop + ($headBot - $headTop) * 0.62)
    Write-Output ("{0,-6} : 머리 앞끝 x{1}  y{2}..{3}  ->  입 y{4}" -f `
        $j.name, $headMaxX, $headTop, $headBot, $mouthY)

    foreach ($c in $biteFrames) {
        $ox = $c * $CELL
        # 이빨 — 머리 앞쪽에 작은 흰 조각 둘, 테두리로 감싼다
        Fill $bmOut $ox $oy ($headMaxX - 1) ($mouthY - 2) ($headMaxX + 3) ($mouthY + 2) $colOutline
        Fill $bmOut $ox $oy ($headMaxX)     ($mouthY - 1) ($headMaxX + 2) ($mouthY - 1) $colTooth
        Fill $bmOut $ox $oy ($headMaxX)     ($mouthY + 1) ($headMaxX + 2) ($mouthY + 1) $colTooth
    }
}

$bmOut.Save($srcPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmOut.Dispose()

Write-Output ("OK  player.png -> {0}x{1} ({2}행). 다음: gen_player_arms.ps1" -f $W, $outH, ($maxRow + 1))
