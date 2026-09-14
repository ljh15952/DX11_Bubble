# ============================================================================
#  gen_icons.ps1
#    장비 슬롯 아이콘.  assets/textures/icons.png  (64x16, 16x16 4칸)
#
#      0  빈손      — 테두리만
#      1  단검      — 칼날 + 손잡이
#      2  잘림      — 붉은 X
#      3  이빨      — 무기가 없어도 쓸 수 있는 것(물기)
#
#  ---- ★ 왜 글자가 아니라 그림인가 ----
#    폰트가 ASCII 전용이라 이미 몸 상태를 **그림**으로 표시하고 있다(§3.2.3).
#    손에 든 것도 같은 층의 정보이므로 같은 방식이어야 한다 —
#    한쪽만 글자면 눈이 두 번 읽어야 한다.
#
#    그리고 「오른손이 잘려 무기를 떨궜다」는 **한 칸이 X 로 바뀌는 것**으로
#    한눈에 읽힌다. 문장으로 쓰면 읽어야 알 수 있다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_icons.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root "assets\textures"
$out  = Join-Path $dir "icons.png"

$CELL   = 16
$CELLS  = 4

$colFrame = [System.Drawing.Color]::FromArgb(255,  92,  96, 112)
$colSteel = [System.Drawing.Color]::FromArgb(255, 206, 210, 220)
$colGrip  = [System.Drawing.Color]::FromArgb(255,  96,  72,  48)
$colWound = [System.Drawing.Color]::FromArgb(255, 168,  46,  46)
$colTooth = [System.Drawing.Color]::FromArgb(255, 240, 240, 246)

$bm = New-Object System.Drawing.Bitmap(($CELL * $CELLS), $CELL,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

function Px($ox, $x, $y, $tone) {
    if ($x -lt 0 -or $x -ge $CELL -or $y -lt 0 -or $y -ge $CELL) { return }
    $script:bm.SetPixel($ox + $x, $y, $tone)
}

function Frame($ox) {
    # 네 변. 모든 칸이 같은 테두리를 가지면 「슬롯」으로 읽힌다.
    for ($i = 0; $i -lt $CELL; $i++) {
        Px $ox $i 0            $script:colFrame
        Px $ox $i ($CELL - 1)  $script:colFrame
        Px $ox 0           $i  $script:colFrame
        Px $ox ($CELL - 1) $i  $script:colFrame
    }
}

for ($c = 0; $c -lt $CELLS; $c++) { Frame ($c * $CELL) }

# ---- 1 : 단검 ----
#   왼쪽 아래에서 오른쪽 위로. 대각선이라 16픽셀 안에서도 「칼」로 읽힌다.
$ox = $CELL
for ($i = 0; $i -lt 8; $i++) {
    Px $ox (4 + $i) (11 - $i) $colSteel
    Px $ox (5 + $i) (11 - $i) $colSteel
}
for ($i = 0; $i -lt 3; $i++) { Px $ox (3 + $i) (12 - $i) $colGrip }
Px $ox 6 11 $colGrip
Px $ox 3 10 $colGrip

# ---- 2 : 잘림 (X) ----
#   ★ 대각선을 1픽셀로 그으면 **점선으로 보인다.** 도트에서 대각선은
#     옆 픽셀을 하나 더 찍어야 이어진 선이 된다.
$ox = $CELL * 2
for ($i = 0; $i -lt 8; $i++) {
    Px $ox (4 + $i)  (4 + $i) $colWound
    Px $ox (5 + $i)  (4 + $i) $colWound
    Px $ox (11 - $i) (4 + $i) $colWound
    Px $ox (10 - $i) (4 + $i) $colWound
}

# ---- 3 : 이빨 ----
#   위아래로 마주 보는 삼각 두 개. 「문다」가 형태만으로 읽힌다.
#   ★ 가운데를 **비워야** 입으로 읽힌다. 붙으면 그냥 쐐기 하나가 된다.
$ox = $CELL * 3
for ($i = 0; $i -lt 4; $i++) {
    for ($j = 0; $j -le $i; $j++) {
        Px $ox (4 + $i * 2)     (3 + $j)   $script:colTooth
        Px $ox (4 + $i * 2 + 1) (3 + $j)   $script:colTooth
        Px $ox (4 + $i * 2)     (12 - $j)  $script:colTooth
        Px $ox (4 + $i * 2 + 1) (12 - $j)  $script:colTooth
    }
}

$bm.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bm.Dispose()

Write-Output ("OK  icons.png  {0}x{1}  ({2}칸)" -f ($CELL * $CELLS), $CELL, $CELLS)
