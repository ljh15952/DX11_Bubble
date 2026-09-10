# ============================================================================
#  gen_player_bite.ps1
#    player.png 에 물기 행을 추가한다.
#
#      row 0..7  기존
#      row 8     bite (6 프레임)   ← 새로. 팔이 없어도 쓸 수 있는 최후의 수단
#
#    384x512  ->  384x576
#
#  ---- 만드는 방법 : 몸 전체를 앞으로 던진다 ----
#    무기가 없는 공격이므로 칼을 그릴 것이 없다. 대신 **몸을 내민다.**
#
#    ★ 머리만 따로 움직이지 않는 이유는 enemy.png 의 물기와 같다 —
#      머리와 몸이 붙어 있어서 머리를 옮기면 원래 자리를 지워야 한다.
#      몸 전체를 미는 편이 「달려들어 문다」로 읽히고 그림을 새로 그릴 필요가 없다.
#
#    ★ frames × ticksPerFrame == TotalTicks 를 맞춘다: 6 × 4 = 24.
#      그리고 startup(8) / 4 = 2 이므로 **프레임 2 가 무는 순간**이다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_player_bite.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$path = Join-Path $root "assets\textures\player.png"

$CELL = 64
$COLS = 6
$KEEP_ROWS = 8        # 멱등 — 이미 9행인 파일에 다시 돌려도 같은 결과
$OUT_ROWS  = 9
$IDLE_ROW  = 0
$IDLE_FRAME = 0

$LIGHT = [System.Drawing.Color]::FromArgb(255, 206, 210, 220)   # 이빨

$bytes  = [System.IO.File]::ReadAllBytes($path)
$ms     = New-Object System.IO.MemoryStream(,$bytes)
$srcImg = [System.Drawing.Image]::FromStream($ms)
$src = New-Object System.Drawing.Bitmap($srcImg.Width, $srcImg.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($src)
$g.DrawImage($srcImg, 0, 0, $srcImg.Width, $srcImg.Height)
$g.Dispose(); $srcImg.Dispose(); $ms.Dispose()

$out = New-Object System.Drawing.Bitmap(($CELL*$COLS), ($CELL*$OUT_ROWS), [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
for ($y = 0; $y -lt ($CELL*$KEEP_ROWS); $y++) {
    for ($x = 0; $x -lt $src.Width; $x++) { $out.SetPixel($x, $y, $src.GetPixel($x, $y)) }
}

# f0..f5 의 앞으로 내미는 양.
#   f0,f1 = 움츠린다(예고)   f2 = ★ 문다   f3 = 문 채   f4,f5 = 회수
$lean = @(-1, -3, 7, 6, 2, 0)

$dstRow = $KEEP_ROWS
for ($f = 0; $f -lt 6; $f++) {
    for ($y = 0; $y -lt $CELL; $y++) {
        for ($x = 0; $x -lt $CELL; $x++) {
            $c = $src.GetPixel($IDLE_FRAME*$CELL + $x, $IDLE_ROW*$CELL + $y)
            if ($c.A -eq 0) { continue }
            $tx = $x + $lean[$f]
            if ($tx -lt 0 -or $tx -ge $CELL) { continue }
            $out.SetPixel($f*$CELL + $tx, $dstRow*$CELL + $y, $c)
        }
    }
}

# 이빨 — 무는 두 프레임에만. 머리 앞쪽에 작게.
#   ★ 머리는 원이라 y 마다 테두리 x 가 달라진다. 넉넉히 안쪽에 둔다
#     (enemy.png 에서 테두리를 지워 머리에 구멍이 뚫린 적이 있다).
function Teeth($col, $shift) {
    $ox = $col * $CELL
    $oy = $dstRow * $CELL
    for ($y = 20; $y -le 23; $y++) {
        for ($x = 34; $x -le 37; $x++) {
            $out.SetPixel($ox + $x + $shift, $oy + $y, $LIGHT)
        }
    }
}
Teeth 2 7
Teeth 3 6

$src.Dispose()
$out.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$out.Dispose()
Write-Output "OK  $path  ->  384 x 576 (9 rows)"
