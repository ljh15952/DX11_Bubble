# ============================================================================
#  gen_player_guard.ps1
#    player.png 에 **방어 자세 4행**을 추가한다.  384x1344 -> 384x1600
#
#      21 작은 방패 · 서서    <- idle  (row 0 f0)
#      22 작은 방패 · 앉아서  <- crouch(row 10 f0)
#      23 큰 방패   · 서서
#      24 큰 방패   · 앉아서
#
#  ---- ★ 왜 방패마다 행인가 ----
#    방패의 성격은 **덮는 띠의 높이**다(design.md §3.11). 그림이 같으면
#    「이 방패가 어디까지 막는지」를 화면에서 알 수가 없고, 그러면 작은 방패와
#    큰 방패를 고르는 일이 **숫자 읽기**가 되어 버린다.
#
#    행 번호는 `weapons.json` 의 `guardClip` 에 있으므로, 방패를 추가할 때
#    C++ 은 한 줄도 안 고친다 — 무기의 공격 그림과 완전히 같은 구조다.
#
#  ---- ★★ 그림이 판정과 **같은 높이**여야 한다 ----
#    방패 사각형을 그리는 y 범위가 곧 `guardTop` / `guardBottom` 이다.
#    눈에 보이는 판이 판정보다 크면 「분명히 막았는데 맞았다」가 되고,
#    작으면 「안 막았는데 막혔다」가 된다. **둘 다 신뢰를 깨뜨린다.**
#    그래서 이 파일의 표와 weapons.json 의 값은 **같은 숫자**여야 한다.
#
#  ---- 앉은 자세의 방패는 **비율로 내려간다** ----
#    코드가 몸 높이 비율(28/44)로 상자를 내리므로, 그림도 같은 비율로 내린다.
#    ★ 두 곳에서 따로 계산하는 대신 **같은 식**을 쓴다 — 한쪽만 바꾸면 어긋난다.
#
#  ---- ★★ 띠를 정한 근거 (적의 공격) ----
#    잡몹 휘두르기 28~37 / 물기 4~20.
#      buckler 24~36  서서 휘두르기를 막고 **물기는 못 막는다**
#                     앉으면 x0.636 -> 15~23  **물기를 막고 휘두르기가 빈다**
#      kite    22~52  서서 휘두르기 + 상단까지. 앉으면 14~33 으로 둘 다 걸친다
#    ★ 「앉아서 하단을 막는다」가 **숫자로 성립해야** 규칙이 된다 —
#      처음엔 18~36 이라 물기와 2픽셀 겹쳐서 서서도 막혔다.
#
#  ★ player.png 를 건드리므로 뒤에 **gen_player_arms.ps1 을 다시** 돌린다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_player_guard.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$path = Join-Path $root "assets\textures\player.png"

$CELL      = 64
$COLS      = 6
$KEEP_ROWS = 21     # 0..20 은 그대로 (멱등성 — 다시 돌려도 같은 결과)
$OUT_ROWS  = 25
$FRAMES    = 2      # 아주 느린 2프레임. 살짝 흔들려 「들고 있다」가 읽힌다

$STAND_H  = 44.0    # BodyComponent 의 kBodyStandHeight
$CROUCH_H = 28.0    #                kBodyCrouchHeight

# ---- 팔레트 ----
$OUTLINE = [System.Drawing.Color]::FromArgb(255,  18,  18,  26)
$BOARD   = [System.Drawing.Color]::FromArgb(255,  96,  74,  52)   # 나무판
$RIM     = [System.Drawing.Color]::FromArgb(255, 176, 182, 196)   # 쇠테
$BOSS    = [System.Drawing.Color]::FromArgb(255, 214, 218, 228)   # 가운데 돌기

# (행, 원본행, 앉았는가, guardTop, guardBottom)
#   ★ 이 표가 weapons.json 과 **같은 숫자**여야 한다.
$jobs = @(
    @{ row = 21; src =  0; crouch = $false; top = 36; bottom = 24 },  # buckler 서서
    @{ row = 22; src = 10; crouch = $true;  top = 36; bottom = 24 },  # buckler 앉아서
    @{ row = 23; src =  0; crouch = $false; top = 52; bottom = 22 },  # kite    서서
    @{ row = 24; src = 10; crouch = $true;  top = 52; bottom = 22 }   # kite    앉아서
)

# ---- 원본을 메모리로 (FromFile 은 파일을 잠근다) ----
$bytes  = [System.IO.File]::ReadAllBytes($path)
$ms     = New-Object System.IO.MemoryStream(,$bytes)
$srcImg = [System.Drawing.Image]::FromStream($ms)
$src = New-Object System.Drawing.Bitmap($srcImg.Width, $srcImg.Height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($src)
$g.DrawImage($srcImg, 0, 0, $srcImg.Width, $srcImg.Height)
$g.Dispose(); $srcImg.Dispose(); $ms.Dispose()

if ($src.Height -lt ($KEEP_ROWS * $CELL)) {
    throw ("player.png 가 너무 작다 ({0}) — 앞선 생성기를 먼저 돌릴 것" -f $src.Height)
}

$out = New-Object System.Drawing.Bitmap(($CELL * $COLS), ($CELL * $OUT_ROWS),
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

for ($y = 0; $y -lt ($CELL * $KEEP_ROWS); $y++) {
    for ($x = 0; $x -lt $src.Width; $x++) { $out.SetPixel($x, $y, $src.GetPixel($x, $y)) }
}

$FOOT = 62    # 발끝 y

function Fill($ox, $oy, $x0, $y0, $x1, $y1, $color) {
    for ($y = [Math]::Max($y0, 0); $y -le [Math]::Min($y1, $CELL - 1); $y++) {
        for ($x = [Math]::Max($x0, 0); $x -le [Math]::Min($x1, $CELL - 1); $x++) {
            $out.SetPixel($ox + $x, $oy + $y, $color)
        }
    }
}

# 그 줄에서 몸의 가장 오른쪽 불투명 픽셀. 없으면 -1.
function Body-Right($ox, $oy, $y) {
    for ($x = $CELL - 1; $x -ge 0; $x--) {
        if ($out.GetPixel($ox + $x, $oy + $y).A -gt 0) { return $x }
    }
    return -1
}

foreach ($j in $jobs) {
    # 판정과 **같은 식**으로 내린다
    $scale = if ($j.crouch) { $CROUCH_H / $STAND_H } else { 1.0 }
    $top    = [int][Math]::Round($FOOT - $j.top    * $scale)
    $bottom = [int][Math]::Round($FOOT - $j.bottom * $scale)

    for ($f = 0; $f -lt $FRAMES; $f++) {
        $dx = $f * $CELL
        $dy = $j.row * $CELL
        $sy = $j.src * $CELL

        # ---- ① 몸을 옮긴다 (원본 0번 칸) ----
        for ($y = 0; $y -lt $CELL; $y++) {
            for ($x = 0; $x -lt $CELL; $x++) {
                $out.SetPixel($dx + $x, $dy + $y, $src.GetPixel($x, $sy + $y))
            }
        }

        # ---- ② 방패를 몸 앞에 세운다 ----
        #   ★ 몸의 오른쪽 끝을 **재서** 거기에 붙인다. 숫자를 적어 두면
        #     앉은 자세에서 방패가 공중에 뜬다(gen_player_moveset 에서 배운 것).
        $mid  = [int](($top + $bottom) / 2)
        $edge = Body-Right $dx $dy $mid
        if ($edge -lt 0) { $edge = 40 }

        # 프레임마다 1픽셀 흔든다 — 멈춰 있으면 정지 화면처럼 보인다
        $x0 = $edge - 1 + $f
        $x1 = $x0 + 5

        Fill $dx $dy ($x0 - 1) ($top - 1) ($x1 + 1) ($bottom + 1) $OUTLINE
        Fill $dx $dy  $x0       $top       $x1       $bottom      $BOARD
        # 위아래 쇠테
        Fill $dx $dy  $x0       $top       $x1       $top         $RIM
        Fill $dx $dy  $x0       $bottom    $x1       $bottom      $RIM
        # 가운데 돌기 — 이것 하나로 「판」이 「방패」로 읽힌다
        Fill $dx $dy ($x0 + 1) ($mid - 1) ($x1 - 1) ($mid + 1)    $BOSS
    }
}

$src.Dispose()
$out.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$out.Dispose()

Write-Output ("OK  player.png  {0}x{1}  (방어 자세 {2}행 x {3}프레임)" -f `
    ($CELL * $COLS), ($CELL * $OUT_ROWS), $jobs.Count, $FRAMES)
Write-Output "다음: gen_player_arms.ps1 을 다시 돌릴 것"
