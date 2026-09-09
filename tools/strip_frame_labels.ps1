# ============================================================================
#  strip_frame_labels.ps1
#    스프라이트시트의 각 셀 좌상단에 구워진 **프레임 번호**를 지운다.
#
#  ---- 왜 필요한가 ----
#    초기 에셋 생성 스크립트가 디버그용으로 각 셀에 프레임 번호를 그려 넣었고,
#    그것이 그대로 저장소에 들어왔다. 스프라이트 원점이 발밑(32, 64)이므로
#    이 숫자는 **게임 화면에서 캐릭터 왼쪽 위에 작은 색 점으로 실제로 렌더된다.**
#    ×2 확대까지 되므로 눈에 띈다.
#
#    생성 스크립트가 남아 있지 않아 시트를 다시 만들 수 없다.
#    그래서 완성된 PNG 에서 해당 영역만 지우는 방식을 쓴다.
#
#  ---- 안전 장치 ----
#    지우기 전에 **셀마다 검사한다.** 지울 영역(x 0..15, y 0..8) 밖의
#    같은 높이대(y 0..8, x >= 16)에 불투명 픽셀이 하나라도 있으면
#    그림 본체가 그 높이까지 올라온다는 뜻이므로 **중단한다.**
#    도트 그림을 스크립트로 지우는 작업에서 검사 없이 진행하면
#    한 번의 실행으로 에셋이 망가지고 되돌릴 수 없다.
#
#    멱등하다 — 이미 지워진 파일에 다시 돌려도 아무 일도 일어나지 않는다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/strip_frame_labels.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root "assets\textures"

$CELL = 64

# 지울 영역 (셀 기준 상대 좌표). 번호는 x 3..8 / y 3..8 에 그려져 있다.
$CLEAR_X1 = 15
$CLEAR_Y1 = 8

# 대상은 **게임이 실제로 쓰는 64x64 시트만**이다.
#   font_8x14.png  — 셀이 8x14 인 폰트. 애초에 번호가 없다
#   sheet.png      — 초기 진단용이고 현재 미사용. 셀 규격이 달라
#                    안전 검사가 거부한다(그리고 렌더되지 않으므로 고칠 이유도 없다)
$sheets = @("enemy.png", "player.png")

foreach ($name in $sheets) {
    $path = Join-Path $dir $name
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Output "skip  $name (없음)"
        continue
    }

    # FromFile 은 파일을 잠근다. 같은 경로에 저장해야 하므로 메모리로 읽는다.
    $bytes  = [System.IO.File]::ReadAllBytes($path)
    $ms     = New-Object System.IO.MemoryStream(,$bytes)
    $srcImg = [System.Drawing.Image]::FromStream($ms)

    $bmp = New-Object System.Drawing.Bitmap($srcImg.Width, $srcImg.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.DrawImage($srcImg, 0, 0, $srcImg.Width, $srcImg.Height)
    $g.Dispose()
    $srcImg.Dispose()
    $ms.Dispose()

    $cols = [int]($bmp.Width  / $CELL)
    $rows = [int]($bmp.Height / $CELL)

    # ---- ① 검사 : 지울 높이대에 그림 본체가 있으면 중단 ----
    for ($r = 0; $r -lt $rows; $r++) {
        for ($c = 0; $c -lt $cols; $c++) {
            for ($y = 0; $y -le $CLEAR_Y1; $y++) {
                for ($x = $CLEAR_X1 + 1; $x -lt $CELL; $x++) {
                    if ($bmp.GetPixel($c*$CELL + $x, $r*$CELL + $y).A -gt 0) {
                        $bmp.Dispose()
                        throw "$name : r$r c$c 의 y$y x$x 에 그림이 있다 — 지우면 손상된다. 중단."
                    }
                }
            }
        }
    }

    # ---- ② 지우기 ----
    $transparent = [System.Drawing.Color]::FromArgb(0, 0, 0, 0)
    $removed = 0
    for ($r = 0; $r -lt $rows; $r++) {
        for ($c = 0; $c -lt $cols; $c++) {
            for ($y = 0; $y -le $CLEAR_Y1; $y++) {
                for ($x = 0; $x -le $CLEAR_X1; $x++) {
                    $px = $c*$CELL + $x
                    $py = $r*$CELL + $y
                    if ($bmp.GetPixel($px, $py).A -gt 0) {
                        $bmp.SetPixel($px, $py, $transparent)
                        $removed++
                    }
                }
            }
        }
    }

    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()

    if ($removed -eq 0) {
        Write-Output "OK    $name  (이미 깨끗함)"
    } else {
        Write-Output "OK    $name  ($removed 픽셀 제거)"
    }
}
