# ============================================================================
#  gen_player_greatsword.ps1
#    player.png 에 **대검용 공격 6행**을 추가한다.  384x960 -> 384x1344
#
#      15 light   <- 2   (몸통)
#      16 crouch  <- 4   (다리)
#      17 thrust  <- 5   (머리)
#      18 dash    <- 6   (몸통, 멀리)
#      19 jump    <- 9   (내려찍기)
#      20 prone   <- 11  (엎드려)
#
#  ---- ★ 왜 「무기마다 행」인가 ----
#    클립의 행 번호는 **이미 weapons.json 에 있다**(`clip.row`).
#    그래서 무기가 자기 행을 가리키기만 하면 **C++ 을 한 줄도 안 고치고**
#    무기마다 다른 그림이 나온다 — 데이터로 빼 둔 것이 여기서 값을 한다.
#
#  ---- ★★ 만드는 법 : 단검 행을 복사하고 **칼만 덮어 그린다** ----
#    자세를 새로 그리면 같은 캐릭터로 안 보이고, 프레임마다의 기울기·눌림을
#    다시 계산해야 한다. 몸은 그대로 두고 **무기만 바꾸는 것**이 맞다 —
#    실제로 무기를 바꾼다는 게 그런 뜻이다.
#
#    칼날은 색이 하나로 정해져 있다(모든 생성기가 같은 $colSteel 을 쓴다).
#    그 색의 픽셀을 찾아 **그 자리에서 더 두껍게** 다시 그린다.
#
#    ★★ 처음엔 **경계 상자 하나**로 덮으려 했다. 가로로 누운 칼은 맞았지만,
#      내려찍기처럼 **비스듬한** 칼은 상자의 위아래 귀퉁이가 안 덮여
#      「큰 칼 밑에 작은 칼이 하나 더」가 보였다.
#      **비스듬한 것을 사각형 하나로 다루면 반드시 귀퉁이가 남는다.**
#
#      그래서 **열(세로줄)마다** 그 줄의 칼날 범위를 재서 위아래로 넓힌다.
#      기울기가 그대로 보존되고, 덮이는 것도 정확하다.
#
#  ---- ★ 대검은 더 크기만 한 것이 아니라 **더 어둡다** ----
#    크기만 키우면 「큰 단검」으로 읽힌다. 도트에서 무게는 **명도**다.
#    icons.png 의 대검 아이콘과 같은 색을 쓴다 — 바닥의 그것, 손 슬롯의 그것,
#    휘두르는 그것이 **같은 물건**으로 읽혀야 한다.
#
#  ★ player.png 를 건드리므로 뒤에 **gen_player_arms.ps1 을 다시** 돌린다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_player_greatsword.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$path = Join-Path $root "assets\textures\player.png"

$CELL      = 64
$COLS      = 6
$KEEP_ROWS = 15     # 0..14 는 그대로 옮긴다 (멱등성 — 다시 돌려도 같은 결과)
$OUT_ROWS  = 21

# 단검 행 -> 대검 행
$jobs = @(
    @{ src =  2; dst = 15; what = "light"  },
    @{ src =  4; dst = 16; what = "crouch" },
    @{ src =  5; dst = 17; what = "thrust" },
    @{ src =  6; dst = 18; what = "dash"   },
    @{ src =  9; dst = 19; what = "jump"   },
    @{ src = 11; dst = 20; what = "prone"  }
)

# ---- 팔레트 ----
$STEEL   = [System.Drawing.Color]::FromArgb(255, 206, 210, 220)   # 찾을 색(단검 날)
$OUTLINE = [System.Drawing.Color]::FromArgb(255,  18,  18,  26)
$HEAVY   = [System.Drawing.Color]::FromArgb(255, 158, 166, 186)   # 대검 날 (한 단 어둡다)
$EDGE    = [System.Drawing.Color]::FromArgb(255, 222, 226, 236)   # 날의 밝은 모서리

# 얼마나 더 큰가.
#   ★ 두께가 길이보다 먼저다. 길게만 하면 「긴 단검」이고,
#     두꺼워야 무겁게 보인다 — 판정(width/height)도 그렇게 키운다.
$EXTEND = 6    # 칼 끝에서 더 뻗는 열 수
$GROW   = 3    # 열마다 위아래로 넓히는 픽셀 (단검 두께 5 -> 대검 11)

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

# 기존 행을 픽셀 단위로 옮긴다 (DrawImage 는 보간이 낄 수 있다)
for ($y = 0; $y -lt ($CELL * $KEEP_ROWS); $y++) {
    for ($x = 0; $x -lt $src.Width; $x++) { $out.SetPixel($x, $y, $src.GetPixel($x, $y)) }
}

function Same($c, $t) {
    return ($c.A -gt 0 -and $c.R -eq $t.R -and $c.G -eq $t.G -and $c.B -eq $t.B)
}

function Fill($ox, $oy, $x0, $y0, $x1, $y1, $color) {
    for ($y = [Math]::Max($y0, 0); $y -le [Math]::Min($y1, $CELL - 1); $y++) {
        for ($x = [Math]::Max($x0, 0); $x -le [Math]::Min($x1, $CELL - 1); $x++) {
            $out.SetPixel($ox + $x, $oy + $y, $color)
        }
    }
}

$made = 0
$skipped = 0

foreach ($j in $jobs) {
    for ($c = 0; $c -lt $COLS; $c++) {
        $sx = $c * $CELL; $sy = $j.src * $CELL
        $dx = $c * $CELL; $dy = $j.dst * $CELL

        # ---- ① 몸을 통째로 옮기면서 칼날의 **열별 범위**를 잰다 ----
        $lo = New-Object int[] $CELL     # 이 열에서 칼날이 시작하는 y
        $hi = New-Object int[] $CELL     # 이 열에서 칼날이 끝나는 y
        for ($i = 0; $i -lt $CELL; $i++) { $lo[$i] = 999; $hi[$i] = -1 }

        $minX = 999; $maxX = -1
        for ($y = 0; $y -lt $CELL; $y++) {
            for ($x = 0; $x -lt $CELL; $x++) {
                $p = $src.GetPixel($sx + $x, $sy + $y)
                $out.SetPixel($dx + $x, $dy + $y, $p)

                if (Same $p $STEEL) {
                    if ($y -lt $lo[$x]) { $lo[$x] = $y }
                    if ($y -gt $hi[$x]) { $hi[$x] = $y }
                    if ($x -lt $minX) { $minX = $x }
                    if ($x -gt $maxX) { $maxX = $x }
                }
            }
        }

        # ---- ② 칼이 없는 프레임(준비·후딜)은 그대로 둔다 ----
        #   ★ 여기서 억지로 칼을 그리면 **없던 프레임에 칼이 생긴다.**
        #     원본이 「이 프레임엔 칼이 안 보인다」고 말하고 있으면 따른다.
        if ($maxX -lt 0) { $skipped++; continue }

        # ---- ③ 열마다 위아래로 넓힌다 ----
        #   앞뒤로 한 열씩 더 나간다 — 옛 칼의 **테두리**가 거기 있다.
        #   그리고 오른쪽으로 $EXTEND 만큼, 끝 열의 두께를 이어서 뻗는다.
        $from = [Math]::Max($minX - 1, 0)
        $to   = [Math]::Min($maxX + $EXTEND, $CELL - 1)

        # 테두리를 **전부 먼저** 긋고 날을 채운다.
        #   ★ 열마다 테두리->날 순으로 하면 다음 열의 테두리가 앞 열의 날을 덮어
        #     칼에 세로 줄무늬가 생긴다. **패스를 나누는 것이 곧 레이어다.**
        for ($pass = 0; $pass -lt 2; $pass++) {
            for ($x = $from; $x -le $to; $x++) {
                # 범위 밖의 열은 가장 가까운 칼날 열의 두께를 빌린다
                $sxc = [Math]::Min([Math]::Max($x, $minX), $maxX)
                if ($hi[$sxc] -lt 0) { continue }

                $y0 = $lo[$sxc] - $GROW
                $y1 = $hi[$sxc] + $GROW

                if ($pass -eq 0) {
                    Fill $dx $dy $x ($y0 - 1) $x ($y1 + 1) $OUTLINE
                } else {
                    Fill $dx $dy $x $y0 $x $y1 $HEAVY
                    Fill $dx $dy $x $y0 $x $y0 $EDGE     # 윗면 한 줄만 밝게
                }
            }
        }

        $made++
    }
}

$src.Dispose()
$out.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$out.Dispose()

Write-Output ("OK  player.png  {0}x{1}  (대검 {2}칸 · 칼 없는 프레임 {3}칸은 그대로)" -f `
    ($CELL * $COLS), ($CELL * $OUT_ROWS), $made, $skipped)
Write-Output "다음: gen_player_arms.ps1 을 다시 돌릴 것"
