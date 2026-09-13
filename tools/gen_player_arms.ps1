# ============================================================================
#  gen_player_arms.ps1
#    팔 레이어 시트 4장을 만든다 — player.png 와 칸이 1:1 로 대응한다.
#
#      player_arm_front.png     앞팔 (facing 쪽 팔)
#      player_arm_back.png      뒷팔
#      player_stump_front.png   앞팔이 잘린 자리
#      player_stump_back.png    뒷팔이 잘린 자리
#
#  ---- ★ 왜 「지우기」가 아니라 「덧그리기」인가 ----
#    player.png 의 몸통은 통짜 사각형이라 **팔이 애초에 안 그려져 있다.**
#    그래서 잘린 팔을 지울 수가 없다 — 팔을 레이어로 얹고, 잘리면 그 레이어를
#    안 그리는 대신 상처 레이어를 그린다.
#
#    design.md §8.1 이 예고한 하이브리드 그대로다:
#      몸통·다리  통짜 애니메이션 (자세가 크게 바뀌므로)
#      팔         겹쳐 그리는 레이어
#
#    ★ 머리 레이어는 만들지 않는다. 머리 파괴 = 즉사라
#      「머리 없는 몸」을 화면에 보여줄 일이 없다(design.md §3.2.2).
#
#  ---- ★ 팔 위치를 숫자로 적지 않는다 ----
#    프레임마다 몸이 흔들리고 기울어진다. 물기(행8)는 몸통이 x24 에서 x32 까지
#    앞으로 나간다. 좌표를 박아 두면 그 프레임에서 팔이 몸에서 떨어진다.
#    그래서 칸마다 **몸통 색(78,92,124) 의 경계를 읽어** 거기에 붙인다 —
#    무기를 그릴 때 「그 줄의 가장 오른쪽 픽셀」을 찾은 것과 같은 방법이다.
#
#  ---- ★ 팔은 「빈 곳에만」 그린다 ----
#    칼날이나 (기어가는 자세의) 머리가 팔 자리를 지나간다.
#    그 위에 덧칠하면 칼이 팔에 먹힌다. 원본이 투명한 픽셀에만 그리면
#    팔이 알아서 그것들 **뒤로** 들어간다 — 예외 처리가 규칙 한 줄로 사라진다.
#    상처는 반대다. 상처는 몸의 일부이므로 덮어쓴다.
#
#  ---- 함정 ----
#    ★ PowerShell 변수명은 대소문자를 구분하지 않는다.
#      $FRONT(색) 과 $front(비트맵) 이 **같은 변수**여서 색이 비트맵으로
#      덮여썼다. 이름을 $colArmNear / $bmArmFront 처럼 접두어로 나눈다.
#    ★ GetPixel 을 22만 번 부르면 끝나지 않는다. LockBits 로 한 번에 읽는다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_player_arms.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root "assets\textures"
$srcPath = Join-Path $dir "player.png"

$CELL = 64

# 구르기 행은 몸이 공처럼 말려 팔이 몸 안에 들어간다 — 팔을 그리지 않는다.
$SKIP_ROW = 3

# ---- 색 ----
#   팔레트에 이미 있는 색만 쓴다. 새로 들어오는 것은 상처의 붉은색 하나뿐이다.
$colOutline = [System.Drawing.Color]::FromArgb(255,  18,  18,  26)
$colArmNear = [System.Drawing.Color]::FromArgb(255,  78,  92, 124)   # 앞팔 = 몸통 본색
$colArmFar  = [System.Drawing.Color]::FromArgb(255,  52,  58,  80)   # 뒷팔 = 어두운 색
$colWound   = [System.Drawing.Color]::FromArgb(255, 120,  28,  32)

# 몸통을 찾는 기준색
$TORSO_R = 78; $TORSO_G = 92; $TORSO_B = 124

# 몸통이 들어 있는 범위. 칸 전체를 돌면 22만 픽셀이라 좁혀 둔다.
# ★ 가정을 그냥 믿지 않고, 경계에 닿으면 경고한다.
$SCAN_X0 = 8; $SCAN_X1 = 58; $SCAN_Y0 = 18; $SCAN_Y1 = 62


# ---- 원본을 32bpp 로 정규화해 읽는다 ----
$srcBytes  = [System.IO.File]::ReadAllBytes($srcPath)
$srcStream = New-Object System.IO.MemoryStream(,$srcBytes)
$srcImage  = [System.Drawing.Image]::FromStream($srcStream)
$W = $srcImage.Width; $H = $srcImage.Height
$bmSource = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$gfx = [System.Drawing.Graphics]::FromImage($bmSource)
$gfx.DrawImage($srcImage, 0, 0, $W, $H)
$gfx.Dispose(); $srcImage.Dispose(); $srcStream.Dispose()

$lockRect = New-Object System.Drawing.Rectangle(0, 0, $W, $H)
$lockData = $bmSource.LockBits($lockRect,
    [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $lockData.Stride
$buf = New-Object byte[] ($stride * $H)
[System.Runtime.InteropServices.Marshal]::Copy($lockData.Scan0, $buf, 0, $buf.Length)
$bmSource.UnlockBits($lockData); $bmSource.Dispose()

$cols = [int]($W / $CELL)
$rows = [int]($H / $CELL)

$bmArmFront   = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$bmArmBack    = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$bmStumpFront = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$bmStumpBack  = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)


# ----------------------------------------------------------------------------
#  Paint — 칸 안의 사각형을 칠한다
#
#    ★ 「어디에 칠해도 되는가」를 mode 로 나눈다. 팔과 상처의 요구가 다르다.
#
#      "empty" : 원본이 **투명한** 곳에만.   팔이 쓴다.
#                칼날·머리가 지나가는 자리를 알아서 비켜 → 팔이 그것들 뒤로 간다.
#
#      "body"  : 투명 + **몸의 색**에만.     상처가 쓴다.
#                상처는 몸의 일부라 몸 위에 덮어써야 하지만,
#                피부(머리)와 칼날 위에 칠하면 얼굴에 피가 묻고 칼이 붉어진다 —
#                기어가는 자세에서 실제로 그렇게 됐다.
# ----------------------------------------------------------------------------
function Paint($target, $ox, $oy, $x0, $y0, $x1, $y1, $tone, $mode)
{
    for ($y = [Math]::Max($y0, 0); $y -le [Math]::Min($y1, $CELL - 1); $y++)
    {
        for ($x = [Math]::Max($x0, 0); $x -le [Math]::Min($x1, $CELL - 1); $x++)
        {
            $i = ($oy + $y) * $script:stride + ($ox + $x) * 4
            $opaque = ($script:buf[$i + 3] -ne 0)

            if ($opaque)
            {
                if ($mode -eq "empty") { continue }

                # "body" — 몸통색 · 어두운색 · 테두리색만 덮어쓴다.
                $pr = $script:buf[$i + 2]; $pg = $script:buf[$i + 1]; $pb = $script:buf[$i]
                $onBody = (($pr -eq  78 -and $pg -eq  92 -and $pb -eq 124) -or
                           ($pr -eq  52 -and $pg -eq  58 -and $pb -eq  80) -or
                           ($pr -eq  18 -and $pg -eq  18 -and $pb -eq  26))
                if (-not $onBody) { continue }
            }

            $target.SetPixel($ox + $x, $oy + $y, $tone)
        }
    }
}


$madeArms   = 0
$madeStumps = 0
$warnings   = 0

for ($r = 0; $r -lt $rows; $r++)
{
    for ($c = 0; $c -lt $cols; $c++)
    {
        $ox = $c * $CELL; $oy = $r * $CELL

        # ---- 이 칸의 몸통 경계를 읽는다 ----
        $minX = 999; $maxX = -1; $minY = 999; $maxY = -1
        for ($y = $SCAN_Y0; $y -le $SCAN_Y1; $y++)
        {
            $rowOff = ($oy + $y) * $stride
            for ($x = $SCAN_X0; $x -le $SCAN_X1; $x++)
            {
                $i = $rowOff + ($ox + $x) * 4
                if ($buf[$i + 3] -eq 0) { continue }
                if ($buf[$i + 2] -ne $TORSO_R) { continue }
                if ($buf[$i + 1] -ne $TORSO_G) { continue }
                if ($buf[$i]     -ne $TORSO_B) { continue }
                if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
            }
        }

        if ($maxX -lt 0) { continue }   # 쓰지 않는 빈 칸

        # ★ 스캔 범위 가정이 깨졌는지 확인한다. 조용히 잘린 팔보다 시끄러운 경고가 낫다.
        if ($minX -le $SCAN_X0 -or $maxX -ge $SCAN_X1 -or
            $minY -le $SCAN_Y0 -or $maxY -ge $SCAN_Y1)
        {
            Write-Warning ("r{0} c{1} : 몸통이 스캔 범위에 닿았다 (x{2}..{3} y{4}..{5}) - SCAN_* 를 넓힐 것" -f `
                $r, $c, $minX, $maxX, $minY, $maxY)
            $warnings++
        }

        # 어깨에서 시작해 몸통 아래까지. 기어가는 자세는 몸통이 납작해 자동으로 짧아진다.
        $armTop = $minY + 2
        $armBot = [Math]::Min($armTop + 15, $maxY - 1)

        # ---- 상처 ----
        #   ★ 테두리 블록을 먼저 깔고 그 안에 붉은색을 넣는다.
        #     전에는 테두리를 「기둥 + 바닥」 두 줄로만 그렸는데, 몸 실루엣이
        #     그 줄보다 좁은 프레임에서 **테두리 픽셀이 허공에 떠 버렸다.**
        #     빈틈 없는 블록으로 만들면 그 종류의 어긋남이 불가능해진다.
        #
        #   ★ 구르기 행에도 그린다. 팔은 없어도 상처는 보여야 한다.
        Paint $bmStumpFront $ox $oy ($maxX - 2) ($armTop - 2) ($maxX + 3) ($armTop + 4) $colOutline "body"
        Paint $bmStumpFront $ox $oy ($maxX - 1) ($armTop - 1) ($maxX + 2) ($armTop + 3) $colWound   "body"

        Paint $bmStumpBack  $ox $oy ($minX - 3) ($armTop - 2) ($minX + 2) ($armTop + 4) $colOutline "body"
        Paint $bmStumpBack  $ox $oy ($minX - 2) ($armTop - 1) ($minX + 1) ($armTop + 3) $colWound   "body"
        $madeStumps++

        if ($r -eq $SKIP_ROW)    { continue }
        if ($armBot -le $armTop) { continue }

        # ---- 팔 ----
        #   테두리를 먼저 깔고 본색을 덮는다. 둘 다 「빈 곳에만」이라
        #   본색은 방금 깐 테두리를 덮지만 원본 몸/칼날은 건드리지 않는다.
        Paint $bmArmFront $ox $oy ($maxX + 1) ($armTop - 1) ($maxX + 6) ($armBot + 1) $colOutline "empty"
        Paint $bmArmFront $ox $oy ($maxX + 2) $armTop       ($maxX + 5) $armBot       $colArmNear "empty"

        Paint $bmArmBack  $ox $oy ($minX - 6) ($armTop - 1) ($minX - 1) ($armBot + 1) $colOutline "empty"
        Paint $bmArmBack  $ox $oy ($minX - 5) $armTop       ($minX - 2) $armBot       $colArmFar  "empty"
        $madeArms++
    }
}

$png = [System.Drawing.Imaging.ImageFormat]::Png
$bmArmFront.Save(  (Join-Path $dir "player_arm_front.png"),   $png)
$bmArmBack.Save(   (Join-Path $dir "player_arm_back.png"),    $png)
$bmStumpFront.Save((Join-Path $dir "player_stump_front.png"), $png)
$bmStumpBack.Save( (Join-Path $dir "player_stump_back.png"),  $png)
$bmArmFront.Dispose();   $bmArmBack.Dispose()
$bmStumpFront.Dispose(); $bmStumpBack.Dispose()

Write-Output ("OK  arm {0} cells / stump {1} cells / warnings {2}" -f $madeArms, $madeStumps, $warnings)
