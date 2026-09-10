# ============================================================================
#  gen_player_crawl.ps1
#    player.png 에 기어가기 행을 추가한다.
#
#      row 0..6  기존 (idle / run / attack / roll / crouch / thrust / runatk)
#      row 7     crawl (4 프레임)   ← 새로. 다리가 부서졌을 때
#
#    384x448  ->  384x512
#
#  ---- ★ 만드는 방법 : 적의 기어가기를 플레이어 색으로 다시 칠한다 ----
#    enemy.png 의 row 1 에 이미 4프레임짜리 기어가기가 있다.
#    실루엣은 사람 모양이라 그대로 쓸 수 있고, 색만 바꾸면 플레이어가 된다.
#
#    새로 그리지 않는 이유:
#      · 자세가 맞는 그림이 이미 있는데 다시 그리면 두 그림이 미묘하게 달라진다
#      · 4프레임의 움직임(몸을 끌며 나아가는 리듬)을 공짜로 얻는다
#      · 판정 상자(kBoxCrawl)가 이미 그 실루엣 기준으로 적혀 있다
#
#    ※ 임시 그림이다. 플레이어 전용 기어가기를 그리면 이 스크립트는 버린다.
#      구조를 먼저 확인하고 그림은 나중 — 이 프로젝트가 네 번째 쓰는 순서다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_player_crawl.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$player = Join-Path $root "assets\textures\player.png"
$enemy  = Join-Path $root "assets\textures\enemy.png"

$CELL = 64
$COLS = 6
$KEEP_ROWS = 7        # player.png 에서 그대로 옮겨 올 행 수 (멱등)
$OUT_ROWS  = 8
$CRAWL_SRC_ROW = 1    # enemy.png 의 기어가기 행

# ---- 색 대응표 : 적 -> 플레이어 ----
#   두 시트 모두 6색 팔레트라 1:1 로 옮겨진다.
$map = @{
    "16,12,14"    = @(18, 18, 26)     # 테두리
    "112,58,58"   = @(78, 92,124)     # 몸통 본색
    "74,40,42"    = @(52, 58, 80)     # 어두운 부분(다리)
    "196,176,150" = @(224,198,168)    # 피부
    "220,70,60"   = @(40, 40, 55)     # 눈
    "175,175,175" = @(206,210,220)    # 밝은 점(이빨) -> 칼 색
}

function LoadCopy($path) {
    # FromFile 은 파일을 잠근다. 같은 경로에 저장해야 하므로 메모리로 읽는다.
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $ms    = New-Object System.IO.MemoryStream(,$bytes)
    $img   = [System.Drawing.Image]::FromStream($ms)
    $bmp   = New-Object System.Drawing.Bitmap($img.Width, $img.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.DrawImage($img, 0, 0, $img.Width, $img.Height)
    $g.Dispose(); $img.Dispose(); $ms.Dispose()
    return $bmp
}

$src = LoadCopy $player
$ene = LoadCopy $enemy

$out = New-Object System.Drawing.Bitmap(($CELL*$COLS), ($CELL*$OUT_ROWS), [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

# 기존 행을 픽셀 단위로 옮긴다 (DrawImage 는 보간이 낄 수 있다)
for ($y = 0; $y -lt ($CELL*$KEEP_ROWS); $y++) {
    for ($x = 0; $x -lt $src.Width; $x++) { $out.SetPixel($x, $y, $src.GetPixel($x, $y)) }
}

# ---- 기어가기 행을 옮기며 다시 칠한다 ----
$dstY = $CELL * $KEEP_ROWS
$unmapped = 0
for ($f = 0; $f -lt 4; $f++) {
    for ($y = 0; $y -lt $CELL; $y++) {
        for ($x = 0; $x -lt $CELL; $x++) {
            $c = $ene.GetPixel($f*$CELL + $x, $CRAWL_SRC_ROW*$CELL + $y)
            if ($c.A -eq 0) { continue }
            $key = "$($c.R),$($c.G),$($c.B)"
            if ($map.ContainsKey($key)) {
                $m = $map[$key]
                $n = [System.Drawing.Color]::FromArgb(255, $m[0], $m[1], $m[2])
            } else {
                # ★ 대응표에 없는 색이 있으면 그대로 두고 개수를 센다.
                #   조용히 넘어가면 「왜 한 픽셀만 색이 다르지」를 나중에 찾게 된다.
                $n = $c
                $unmapped++
            }
            $out.SetPixel($f*$CELL + $x, $dstY + $y, $n)
        }
    }
}

$src.Dispose(); $ene.Dispose()
$out.Save($player, [System.Drawing.Imaging.ImageFormat]::Png)
$out.Dispose()

Write-Output "OK  $player  ->  384 x 512 (8 rows)"
if ($unmapped -gt 0) { Write-Output "  주의: 대응표에 없는 색 $unmapped 픽셀 (그대로 뒀다)" }
