# ============================================================================
#  gen_player_jumpatk.ps1
#    player.png 에 **9행 = 점프 공격(내려찍기)** 을 추가한다.
#    384x576 (9행) -> 384x640 (10행)
#
#  ---- ★ 왜 내려찍기인가 ----
#    design.md §3.8.2 : 공중에서는 적보다 **위에** 있으므로 아래를 향해 찍으면
#    머리에 닿기 쉽다. 머리는 즉사(§3.2.2)다.
#    「높은 데서 뛰어내려 머리를 노린다」 — 지형이 전술이 되는 지점이다.
#    그래서 칼이 **아래로** 향해야 그림과 판정이 같은 말을 한다.
#
#  ---- 프레임 구성 (6프레임 x 4틱 = 24틱) ----
#      0-1  준비  : 칼을 위로 든다
#      2-3  ★타격 : 아래앞으로 내려찍는다   (startup 8 / 4틱 = 프레임 2)
#      4-5  후딜  : 칼이 돌아온다
#
#    ★ startup / ticksPerFrame 이 정수여야 **타격 그림과 판정이 같은 프레임**에
#      온다. 8 / 4 = 2 로 맞춰 두었다(handoff §8).
#
#  ---- ★ 멱등 ----
#    몇 번 돌려도 결과가 같다. 0~8행은 원본에서 그대로 옮기고 9행만 다시 그린다.
#    이미 10행이어도 앞 9행만 읽으므로 결과가 변하지 않는다.
#
#  ★ 이 스크립트를 돌린 뒤에는 **gen_player_arms.ps1 을 다시 돌려야 한다.**
#    팔 레이어 시트는 몸 시트와 크기·칸이 1:1 로 맞아야 하기 때문이다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_player_jumpatk.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root "assets\textures"
$srcPath = Join-Path $dir "player.png"

$CELL     = 64
$KEEP_ROWS = 9    # 0~8 은 그대로 옮긴다
$JUMP_ROW  = 9
$FRAMES    = 6

$colOutline = [System.Drawing.Color]::FromArgb(255,  18,  18,  26)
$colSteel   = [System.Drawing.Color]::FromArgb(255, 206, 210, 220)

$TORSO_R = 78; $TORSO_G = 92; $TORSO_B = 124

# ---- 프레임마다의 몸 기울기와 칼끝 위치 ----
#   ★ 손 기준점에서 칼끝까지의 상대 좌표다. 몸이 기울면 손도 같이 움직이므로
#     칼은 자동으로 따라간다 — gen_player_arms.ps1 과 같은 발상이다.
$bodyDX  = @( 0,  0,  2,  3,  1,  0)
$bodyDY  = @( 0, -1,  1,  2,  1,  0)
$tipDX   = @( 4,  2, 18, 20, 14, 10)
$tipDY   = @(-16,-20, 12, 14,  8,  0)


# ---- 원본을 32bpp 로 읽는다 ----
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

# ---- idle 0번 칸의 몸통 경계를 찾는다 (칼을 붙일 기준) ----
$rect = New-Object System.Drawing.Rectangle(0, 0, $bmSrc.Width, $bmSrc.Height)
$data = $bmSrc.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $data.Stride
$buf = New-Object byte[] ($stride * $bmSrc.Height)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
$bmSrc.UnlockBits($data)

$minX = 999; $maxX = -1; $minY = 999; $maxY = -1
for ($y = 0; $y -lt $CELL; $y++) {
    $rowOff = $y * $stride
    for ($x = 0; $x -lt $CELL; $x++) {
        $i = $rowOff + $x * 4
        if ($buf[$i + 3] -eq 0) { continue }
        if ($buf[$i + 2] -ne $TORSO_R -or $buf[$i + 1] -ne $TORSO_G -or $buf[$i] -ne $TORSO_B) { continue }
        if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
        if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
    }
}
if ($maxX -lt 0) { throw "idle 0번 칸에서 몸통 색을 못 찾았다" }
Write-Output ("idle torso  x{0}..{1}  y{2}..{3}" -f $minX, $maxX, $minY, $maxY)


# ---- 출력 시트 ----
$outH = ($JUMP_ROW + 1) * $CELL
$bmOut = New-Object System.Drawing.Bitmap($W, $outH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmOut)
$g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half

# 0~8행을 그대로
$keep = New-Object System.Drawing.Rectangle(0, 0, $W, ($KEEP_ROWS * $CELL))
$g.DrawImage($bmSrc, $keep, $keep, [System.Drawing.GraphicsUnit]::Pixel)

# 9행 : idle 0번 칸을 프레임마다 조금씩 기울여 복사
$srcCell = New-Object System.Drawing.Rectangle(0, 0, $CELL, $CELL)
for ($c = 0; $c -lt $FRAMES; $c++) {
    $ox = $c * $CELL; $oy = $JUMP_ROW * $CELL
    $g.Clip = New-Object System.Drawing.Region(
        (New-Object System.Drawing.Rectangle($ox, $oy, $CELL, $CELL)))
    $dst = New-Object System.Drawing.Rectangle(
        ($ox + $bodyDX[$c]), ($oy + $bodyDY[$c]), $CELL, $CELL)
    $g.DrawImage($bmSrc, $dst, $srcCell, [System.Drawing.GraphicsUnit]::Pixel)
}
$g.ResetClip(); $g.Dispose(); $bmSrc.Dispose()


# ----------------------------------------------------------------------------
#  Blob — 칸 안에 정사각형 덩어리 하나. 칸 밖으로 나가면 잘라 낸다.
# ----------------------------------------------------------------------------
function Blob($bmp, $ox, $oy, $cx, $cy, $half, $tone)
{
    for ($y = $cy - $half; $y -le $cy + $half; $y++) {
        if ($y -lt 0 -or $y -ge $CELL) { continue }
        for ($x = $cx - $half; $x -le $cx + $half; $x++) {
            if ($x -lt 0 -or $x -ge $CELL) { continue }
            $bmp.SetPixel($ox + $x, $oy + $y, $tone)
        }
    }
}

# ----------------------------------------------------------------------------
#  Blade — 손에서 칼끝까지 두꺼운 선 하나
#
#    ★ 테두리를 **전부 깐 뒤에** 본색을 덮는다. 한 덩어리씩 「테두리+본색」을
#      그리면 다음 덩어리의 테두리가 앞 덩어리의 본색을 갉아먹어
#      칼날이 얼룩덜룩해진다.
# ----------------------------------------------------------------------------
function Blade($bmp, $ox, $oy, $hx, $hy, $tx, $ty)
{
    $steps = 24
    for ($pass = 0; $pass -lt 2; $pass++) {
        $half = if ($pass -eq 0) { 2 } else { 1 }
        $tone = if ($pass -eq 0) { $script:colOutline } else { $script:colSteel }
        for ($s = 0; $s -le $steps; $s++) {
            $f = $s / $steps
            $x = [int][Math]::Round($hx + ($tx - $hx) * $f)
            $y = [int][Math]::Round($hy + ($ty - $hy) * $f)
            Blob $bmp $ox $oy $x $y $half $tone
        }
    }
}

for ($c = 0; $c -lt $FRAMES; $c++) {
    $ox = $c * $CELL; $oy = $JUMP_ROW * $CELL

    # 손 = 몸통 오른쪽 어깨. 몸이 기울면 손도 같이 움직인다.
    $handX = $maxX + 2 + $bodyDX[$c]
    $handY = $minY + 5 + $bodyDY[$c]

    Blade $bmOut $ox $oy $handX $handY ($handX + $tipDX[$c]) ($handY + $tipDY[$c])
}

$bmOut.Save($srcPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmOut.Dispose()

Write-Output ("OK  player.png -> {0}x{1} ({2}행). 다음: gen_player_arms.ps1 을 다시 돌릴 것" -f $W, $outH, ($JUMP_ROW + 1))
