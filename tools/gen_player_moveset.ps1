# ============================================================================
#  gen_player_moveset.ps1
#    player.png 에 무브셋 3행을 추가한다.
#
#      row 0  idle   (4)   ← 기존. 그대로 복사
#      row 1  run    (6)   ← 기존
#      row 2  attack (6)   ← 기존 = LIGHT (몸통)
#      row 3  roll   (6)   ← 기존
#      row 4  crouch (6)   ← 새로. 웅크려 낮게 벤다      (다리)
#      row 5  thrust (6)   ← 새로. 머리 높이로 찌른다    (머리)
#      row 6  runatk (6)   ← 새로. 앞으로 크게 뻗는다    (몸통, 멀리)
#
#    384x256  ->  384x448
#
#  ---- 왜 필요한가 ----
#    무브셋 4종이 전부 같은 행(row 2)을 쓰고 있어서 화면에서 구분되지 않았다.
#    무엇이 나갔는지 보이지 않으면 「Ctrl 을 누르면 다리를 벤다」를 플레이어가
#    배울 수 없다. 판정은 이미 다른데 그림이 같으면 시스템이 없는 것과 같다.
#
#  ---- 만드는 방법 : 몸은 복사, 칼만 그린다 ----
#    idle 프레임 0 의 몸을 픽셀 단위로 복사하고 그 위에 칼날을 얹는다.
#    새로 그리지 않는 이유는 실루엣이 1픽셀이라도 달라지면 행이 바뀌는 순간
#    화면에서 "톡" 튀어 보이기 때문이다. enemy.png 의 공격 행과 같은 방식이다.
#
#  ---- ★ 칼날 위치를 코드로 계산하지 않는다 ----
#    칼이 몸에 붙어야 하는데, 몸의 오른쪽 끝은 y 에 따라 다르다
#    (몸통은 x40 까지, 다리는 x37 까지). 숫자를 적어 두면 웅크린 자세에서
#    칼이 공중에 뜬다.
#    그래서 **그 줄의 가장 오른쪽 불투명 픽셀을 찾아** 거기서부터 그린다.
#
#    그리고 테두리가 몸 안쪽에 떨어지면 「팔」이 아니라 「몸에 난 금」으로 보인다
#    (enemy.png 에서 실제로 밟은 함정). 테두리를 몸의 테두리 칸에 **정확히 겹친다.**
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_player_moveset.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$path = Join-Path $root "assets\textures\player.png"

$CELL = 64
$COLS = 6
$KEEP_ROWS = 4        # 원본에서 옮겨 올 행 수 (멱등성 — 다시 돌려도 같은 결과)
$OUT_ROWS  = 7

# ---- 원본 팔레트 ----
$OUTLINE = [System.Drawing.Color]::FromArgb(255,  18,  18,  26)
$BLADE   = [System.Drawing.Color]::FromArgb(255, 206, 210, 220)

# ---- 원본을 메모리로 (FromFile 은 파일을 잠근다) ----
$bytes  = [System.IO.File]::ReadAllBytes($path)
$ms     = New-Object System.IO.MemoryStream(,$bytes)
$srcImg = [System.Drawing.Image]::FromStream($ms)
$src = New-Object System.Drawing.Bitmap($srcImg.Width, $srcImg.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($src)
$g.DrawImage($srcImg, 0, 0, $srcImg.Width, $srcImg.Height)
$g.Dispose(); $srcImg.Dispose(); $ms.Dispose()

$out = New-Object System.Drawing.Bitmap(($CELL*$COLS), ($CELL*$OUT_ROWS), [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

# 기존 행을 픽셀 단위로 옮긴다 (DrawImage 는 보간이 낄 수 있다)
for ($y = 0; $y -lt ($CELL*$KEEP_ROWS); $y++) {
    for ($x = 0; $x -lt $src.Width; $x++) { $out.SetPixel($x, $y, $src.GetPixel($x, $y)) }
}

$FOOT     = 62    # 발끝 y
$HEAD_TOP = 13    # 서 있을 때 머리 꼭대기

# ----------------------------------------------------------------------------
#  몸 복사. squash < 1 이면 발끝을 고정한 채 세로로 눌러 웅크린 자세를 만든다.
#
#  ★ 목적지 줄을 돌면서 원본을 되짚는다. 반대로 하면 눌리는 과정에서
#    목적지 줄이 비어 가로줄 구멍이 생긴다.
# ----------------------------------------------------------------------------
function Copy-Body($dstCol, $dstRow, $lean, $squash) {
    $dx = $dstCol * $CELL
    $dy = $dstRow * $CELL
    for ($y = 0; $y -le $FOOT; $y++) {
        $srcY = [int][Math]::Round($FOOT - ($FOOT - $y) / $squash)
        if ($srcY -lt 0 -or $srcY -gt $FOOT) { continue }
        for ($x = 0; $x -lt $CELL; $x++) {
            $c = $src.GetPixel($x, $srcY)          # row 0 (idle) frame 0
            if ($c.A -eq 0) { continue }
            $tx = $x + $lean
            if ($tx -lt 0 -or $tx -ge $CELL) { continue }
            $out.SetPixel($dx + $tx, $dy + $y, $c)
        }
    }
}

function Fill($col, $row, $x0, $y0, $x1, $y1, $color) {
    $ox = $col * $CELL; $oy = $row * $CELL
    for ($y = [Math]::Max($y0,0); $y -le [Math]::Min($y1,$CELL-1); $y++) {
        for ($x = [Math]::Max($x0,0); $x -le [Math]::Min($x1,$CELL-1); $x++) {
            $out.SetPixel($ox + $x, $oy + $y, $color)
        }
    }
}

# 그 줄에서 몸의 가장 오른쪽 불투명 픽셀. 없으면 -1.
function Body-Right($col, $row, $y) {
    $ox = $col * $CELL; $oy = $row * $CELL
    for ($x = $CELL - 1; $x -ge 0; $x--) {
        if ($out.GetPixel($ox + $x, $oy + $y).A -gt 0) { return $x }
    }
    return -1
}

# 가로로 뻗는 칼날. 몸의 오른쪽 테두리 칸에서 시작해 reachX 까지.
function Blade-H($col, $row, $centerY, $reachX) {
    $edge = Body-Right $col $row $centerY
    if ($edge -lt 0) { $edge = 40 }
    if ($reachX -le $edge + 2) { return }
    # ★ 테두리를 몸의 테두리 칸(edge)에 정확히 겹친다 -> 몸 안에 금이 안 생긴다
    Fill $col $row  $edge        ($centerY-3)  ($reachX+1) ($centerY+3) $OUTLINE
    Fill $col $row  ($edge+1)    ($centerY-2)  $reachX     ($centerY+2) $BLADE
}

# 머리 위로 세운 칼날. 빈 공간이라 몸과 겹칠 걱정이 없다.
function Blade-V($col, $row, $headTop, $length) {
    $y1 = $headTop - 3
    $y0 = $y1 - $length
    Fill $col $row 29 ($y0-1) 35 ($y1+1) $OUTLINE
    Fill $col $row 30 $y0     34 $y1     $BLADE
}

# ----------------------------------------------------------------------------
#  한 행 만들기
#    f0,f1 : 칼을 머리 위로 세운다 (예고)
#    f2    : ★ 타격. 가장 멀리 뻗는다.  startup / ticksPerFrame == 2 에 맞춘 자리다
#    f3    : 뻗은 채 (지속~후딜)
#    f4,f5 : 회수
# ----------------------------------------------------------------------------
function Make-Row($row, $lean, $squash, $bladeY, $reach) {
    $headTop = [int][Math]::Round($FOOT - ($FOOT - $HEAD_TOP) * $squash)

    for ($f = 0; $f -lt 6; $f++) { Copy-Body $f $row $lean $squash }

    Blade-V 0 $row $headTop 9
    Blade-V 1 $row $headTop 16
    Blade-H 2 $row $bladeY  $reach
    Blade-H 3 $row $bladeY  ($reach - 7)
    Blade-H 4 $row $bladeY  ($lean + 52)
    Blade-V 5 $row $headTop 7
}

#            row  lean squash bladeY  reach
Make-Row      4     2   0.78     51      62     # CROUCH : 낮게. heightFromFoot 10 -> 셀 y 52
Make-Row      5     3   1.00     14      63     # THRUST : 머리 높이. heightFromFoot 48 -> 셀 y 14
Make-Row      6     7   1.00     34      63     # RUN    : 몸통 높이지만 크게 앞으로

$src.Dispose()
$out.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$out.Dispose()
Write-Output "OK  $path  ->  384 x 448 (7 rows)"
