# ============================================================================
#  gen_player_prone_attack.ps1
#    11행 = **엎드린 공격** (6프레임)
#    384x704 (11행) -> 384x768 (12행)
#
#  ---- ★ 왜 필요한가 ----
#    엎드린 채 공격하면 **기어가기 루프**가 재생되고 있었다.
#    전용 그림이 없어서 「자세만 유지」하도록 예외를 두었기 때문인데,
#    그 결과 공격했는데 **아무 일도 안 일어나 보였다.**
#
#    웅크리기 때와 같은 결론이다 — 예외로 때우지 말고 그림을 그린다.
#    그림을 그리면 `Prone() ? 기어가기 : 공격그림` 이라는 **예외 자체가 사라진다.**
#    공격 데이터가 자기 그림을 들고 다니면 되기 때문이다.
#
#  ---- 프레임 구성 ----
#      0-1  준비  : 칼을 당긴다
#      2-3  ★타격 : 앞으로 찌른다
#      4-5  후딜  : 돌아온다
#
#    좌클릭(kDaggerProne)은 6프레임 x 5틱 = 30,
#    우클릭(kBiteProne)은 6프레임 x 4틱 = 24 로 **같은 행을 다른 속도로** 쓴다.
#
#  ---- ★ 몸은 기어가기 행에서 그대로 가져온다 ----
#    다시 그리면 미묘하게 달라져 다른 캐릭터가 된다.
#    7행 0번 칸(기어가기 첫 프레임)을 복사하고 칼만 얹는다.
#
#  ★ 이 스크립트를 돌린 뒤에는 **gen_player_arms.ps1 을 다시 돌려야 한다.**
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_player_prone_attack.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root "assets\textures"
$srcPath = Join-Path $dir "player.png"

$CELL       = 64
$FOOT_Y     = 62
$KEEP_ROWS  = 11          # 0~10 을 그대로 옮긴다
$CRAWL_ROW  = 7           # 몸을 가져올 행
$PRONE_ROW  = 11          # 새로 만드는 행
$FRAMES     = 6

$colOutline = [System.Drawing.Color]::FromArgb(255,  18,  18,  26)
$colSteel   = [System.Drawing.Color]::FromArgb(255, 206, 210, 220)

# 칼이 앞으로 나가는 길이. startup 10 / 5틱 = 프레임 2 가 타격이다.
$bladeLen = @( 4,  2, 20, 22, 14,  7)

# 칼 높이 — 엎드린 몸의 앞쪽. 판정(heightFromFoot 8)과 같은 높이다.
$BLADE_H = 8


# ---- 원본 읽기 ----
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

# ---- 출력 시트 ----
$outH = ($PRONE_ROW + 1) * $CELL
$bmOut = New-Object System.Drawing.Bitmap($W, $outH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmOut)
$g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
$keep = New-Object System.Drawing.Rectangle(0, 0, $W, ($KEEP_ROWS * $CELL))
$g.DrawImage($bmSrc, $keep, $keep, [System.Drawing.GraphicsUnit]::Pixel)

# 11행 : 기어가기 첫 프레임을 여섯 번 복사
$srcCell = New-Object System.Drawing.Rectangle(0, ($CRAWL_ROW * $CELL), $CELL, $CELL)
for ($c = 0; $c -lt $FRAMES; $c++) {
    $dst = New-Object System.Drawing.Rectangle(($c * $CELL), ($PRONE_ROW * $CELL), $CELL, $CELL)
    $g.DrawImage($bmSrc, $dst, $srcCell, [System.Drawing.GraphicsUnit]::Pixel)
}
$g.Dispose()

# ---- 몸의 앞쪽 끝을 찾는다 (칼을 붙일 자리) ----
#   ★ 좌표를 적지 않고 **그림에서 읽는다.** 기어가기 그림을 고치면 칼이 따라온다.
$rect = New-Object System.Drawing.Rectangle(0, 0, $bmOut.Width, $bmOut.Height)
$data = $bmOut.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $data.Stride
$buf = New-Object byte[] ($stride * $bmOut.Height)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
$bmOut.UnlockBits($data)

$bladeY = $FOOT_Y - $BLADE_H
$frontX = -1
for ($x = $CELL - 1; $x -ge 0; $x--) {
    $i = (($PRONE_ROW * $CELL) + $bladeY) * $stride + $x * 4
    if ($buf[$i + 3] -ne 0) { $frontX = $x; break }
}
if ($frontX -lt 0) {
    # 그 높이에 몸이 없으면 몸 전체의 오른쪽 끝을 쓴다
    for ($y = 0; $y -lt $CELL; $y++) {
        $rowOff = (($PRONE_ROW * $CELL) + $y) * $stride
        for ($x = 0; $x -lt $CELL; $x++) {
            if ($buf[$rowOff + $x * 4 + 3] -ne 0 -and $x -gt $frontX) { $frontX = $x }
        }
    }
}
Write-Output ("엎드린 몸의 앞끝 x{0}  (칼 높이 = 발끝 기준 {1})" -f $frontX, $BLADE_H)


function Fill($bmp, $ox, $oy, $x0, $y0, $x1, $y1, $tone)
{
    for ($y = [Math]::Max($y0, 0); $y -le [Math]::Min($y1, $CELL - 1); $y++) {
        for ($x = [Math]::Max($x0, 0); $x -le [Math]::Min($x1, $CELL - 1); $x++) {
            $bmp.SetPixel($ox + $x, $oy + $y, $tone)
        }
    }
}

for ($c = 0; $c -lt $FRAMES; $c++) {
    $ox = $c * $CELL; $oy = $PRONE_ROW * $CELL
    $len = $bladeLen[$c]
    if ($len -le 0) { continue }

    $x0 = $frontX + 1
    Fill $bmOut $ox $oy $x0 ($bladeY - 2) ($x0 + $len)     ($bladeY + 2) $colOutline
    Fill $bmOut $ox $oy $x0 ($bladeY - 1) ($x0 + $len - 1) ($bladeY + 1) $colSteel
}

$bmSrc.Dispose()
$bmOut.Save($srcPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmOut.Dispose()

Write-Output ("OK  player.png -> {0}x{1} ({2}행). 다음: gen_player_arms.ps1 을 다시 돌릴 것" -f `
    $W, $outH, ($PRONE_ROW + 1))
