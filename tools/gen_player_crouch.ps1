# ============================================================================
#  gen_player_crouch.ps1
#    웅크린 자세의 **전용 그림**을 만든다.
#      10행 = 웅크리기 (4프레임, 미세한 호흡)
#       4행 = 웅크린 공격 (6프레임)   ← 기존 행을 덮어쓴다
#    384x640 (10행) -> 384x704 (11행)
#
#  ---- ★ 왜 스케일이 아니라 그림인가 ----
#    전에는 서 있는 그림을 세로로 눌러서(SetScale 0.78) 웅크림을 표현했다.
#    그런데 판정 상자는 안 눌려서 **그림은 낮아졌다고 하는데 판정은 서 있다**고
#    말하는 상태가 되었다. 눌리는 비율을 0.45 로 키워 맞출 수도 있었지만,
#    그러면 머리까지 납작해져 「같은 캐릭터」로 안 보인다.
#
#    **그림을 그리면 둘 다 해결된다.** 머리는 그대로 두고 몸만 접으면 된다 —
#    실제로 쪼그려 앉으면 그렇게 된다.
#
#  ---- ★ 높이가 규칙이다 ----
#    실루엣 위끝을 **27** 로 맞춘다. 이 숫자가 게임 규칙이다:
#
#        37 ~ 55   서 있는 머리      ← 잡몹 평타가 닿으면 두 대에 죽는다
#        중단 띠 (28~37)             팔 20~36 · 몸통 18~37 에 닿는다
#      ★ 27        웅크린 몸 전체    ← 여기까지 낮아야 흘린다
#        ~ 25      엎드린 몸 전체
#
#    판정 상자(PartsComponent 의 kBoxCrouch)도 같은 숫자를 쓴다.
#    **그림과 판정이 같은 곳에서 나와야** 또 어긋나지 않는다.
#
#  ---- ★ 머리는 원본에서 그대로 오려 온다 ----
#    다시 그리면 눈 위치나 동그라미 모양이 미묘하게 달라져 다른 캐릭터가 된다.
#    idle 0번 칸에서 몸통보다 위에 있는 픽셀을 통째로 오려 아래로 옮긴다.
#
#  ★ 이 스크립트를 돌린 뒤에는 **gen_player_arms.ps1 을 다시 돌려야 한다.**
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_player_crouch.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root "assets\textures"
$srcPath = Join-Path $dir "player.png"

$CELL       = 64
$FOOT_Y     = 62                 # 발끝. 모든 높이의 기준
$KEEP_ROWS  = 10                 # 0~9 를 옮긴다 (4행은 아래에서 덮어쓴다)
$CROUCH_ROW = 10
$ATTACK_ROW = 4

# ---- 높이 배치 (칸 좌표) ----
#   머리 아래끝을 몸통 위에 얹고, 몸통과 다리를 접는다.
$HEAD_TOP   = 35                 # 실루엣 위끝 -> 발끝 기준 62-35 = 27
$TORSO_TOP  = 49
$TORSO_BOT  = 58
$LEGS_TOP   = 56

$colOutline = [System.Drawing.Color]::FromArgb(255,  18,  18,  26)
$colTorso   = [System.Drawing.Color]::FromArgb(255,  78,  92, 124)
$colLegs    = [System.Drawing.Color]::FromArgb(255,  52,  58,  80)
$colSteel   = [System.Drawing.Color]::FromArgb(255, 206, 210, 220)

$TORSO_R = 78; $TORSO_G = 92; $TORSO_B = 124

# ---- 웅크린 공격 : 칼끝이 앞으로 나가는 길이 ----
#   startup 10 / 5틱 = 프레임 2 가 타격이다. 거기서 가장 길게 뻗는다.
$bladeLen = @( 6,  3, 20, 22, 14,  8)
#   호흡용 위아래 흔들림 (웅크리기 행)
$breathDY = @( 0,  0,  1,  1)


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

$rect = New-Object System.Drawing.Rectangle(0, 0, $bmSrc.Width, $bmSrc.Height)
$data = $bmSrc.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $data.Stride
$buf = New-Object byte[] ($stride * $bmSrc.Height)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
$bmSrc.UnlockBits($data)

# ---- idle 0번 칸에서 몸통 경계와 머리 영역을 찾는다 ----
$torsoMinX = 999; $torsoMaxX = -1; $torsoMinY = 999
for ($y = 0; $y -lt $CELL; $y++) {
    $rowOff = $y * $stride
    for ($x = 0; $x -lt $CELL; $x++) {
        $i = $rowOff + $x * 4
        if ($buf[$i + 3] -eq 0) { continue }
        if ($buf[$i + 2] -ne $TORSO_R -or $buf[$i + 1] -ne $TORSO_G -or $buf[$i] -ne $TORSO_B) { continue }
        if ($x -lt $torsoMinX) { $torsoMinX = $x }
        if ($x -gt $torsoMaxX) { $torsoMaxX = $x }
        if ($y -lt $torsoMinY) { $torsoMinY = $y }
    }
}
if ($torsoMaxX -lt 0) { throw "idle 0번 칸에서 몸통 색을 못 찾았다" }

# 머리 = 몸통보다 **위**에 있는 불투명 픽셀 전부 (테두리 포함)
$headTop = 999
for ($y = 0; $y -lt $torsoMinY; $y++) {
    $rowOff = $y * $stride
    for ($x = 0; $x -lt $CELL; $x++) {
        if ($buf[$rowOff + $x * 4 + 3] -ne 0) { if ($y -lt $headTop) { $headTop = $y }; break }
    }
}
if ($headTop -eq 999) { throw "머리를 못 찾았다" }

$headH = $torsoMinY - $headTop
Write-Output ("idle : torso x{0}..{1} y{2}-  /  head y{3}..{4} ({5}px)" -f `
    $torsoMinX, $torsoMaxX, $torsoMinY, $headTop, ($torsoMinY - 1), $headH)
Write-Output ("crouch : 머리 y{0}..{1}  실루엣 위끝 = 발끝 기준 {2}" -f `
    $HEAD_TOP, ($HEAD_TOP + $headH - 1), ($FOOT_Y - $HEAD_TOP))


# ---- 출력 시트 ----
$outH = ($CROUCH_ROW + 1) * $CELL
$bmOut = New-Object System.Drawing.Bitmap($W, $outH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmOut)
$g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
$keep = New-Object System.Drawing.Rectangle(0, 0, $W, ($KEEP_ROWS * $CELL))
$g.DrawImage($bmSrc, $keep, $keep, [System.Drawing.GraphicsUnit]::Pixel)

# 4행(웅크린 공격)을 비운다 — 옛 그림이 남으면 안 된다
$g.Clip = New-Object System.Drawing.Region(
    (New-Object System.Drawing.Rectangle(0, ($ATTACK_ROW * $CELL), $W, $CELL)))
$g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
$g.ResetClip(); $g.Dispose()


function Fill($bmp, $ox, $oy, $x0, $y0, $x1, $y1, $tone)
{
    for ($y = [Math]::Max($y0, 0); $y -le [Math]::Min($y1, $CELL - 1); $y++) {
        for ($x = [Math]::Max($x0, 0); $x -le [Math]::Min($x1, $CELL - 1); $x++) {
            $bmp.SetPixel($ox + $x, $oy + $y, $tone)
        }
    }
}

# ----------------------------------------------------------------------------
#  DrawCrouchBody — 웅크린 몸 하나. 머리는 원본에서 오려 오고 나머지는 그린다.
# ----------------------------------------------------------------------------
function DrawCrouchBody($bmp, $ox, $oy, $dy)
{
    # 다리 — 접혀서 짧다
    Fill $bmp $ox $oy ($script:torsoMinX)      ($script:LEGS_TOP + $dy) ($script:torsoMaxX + 1) ($script:FOOT_Y + $dy) $script:colOutline
    Fill $bmp $ox $oy ($script:torsoMinX + 1)  ($script:LEGS_TOP + 1 + $dy) ($script:torsoMaxX) ($script:FOOT_Y - 1 + $dy) $script:colLegs
    # 두 다리를 가르는 선
    Fill $bmp $ox $oy 32 ($script:LEGS_TOP + 1 + $dy) 32 ($script:FOOT_Y - 1 + $dy) $script:colOutline

    # 몸통 — 접혀서 짧다
    Fill $bmp $ox $oy ($script:torsoMinX - 1) ($script:TORSO_TOP + $dy) ($script:torsoMaxX + 1) ($script:TORSO_BOT + $dy) $script:colOutline
    Fill $bmp $ox $oy ($script:torsoMinX)     ($script:TORSO_TOP + 1 + $dy) ($script:torsoMaxX) ($script:TORSO_BOT - 1 + $dy) $script:colTorso

    # 머리 — 원본에서 그대로 오려 온다
    for ($y = 0; $y -lt $script:headH; $y++) {
        $srcRow = ($script:headTop + $y) * $script:stride
        $dstY   = $script:HEAD_TOP + $y + $dy
        if ($dstY -lt 0 -or $dstY -ge $CELL) { continue }
        for ($x = 0; $x -lt $CELL; $x++) {
            $i = $srcRow + $x * 4
            if ($script:buf[$i + 3] -eq 0) { continue }
            $tone = [System.Drawing.Color]::FromArgb(
                $script:buf[$i + 3], $script:buf[$i + 2], $script:buf[$i + 1], $script:buf[$i])
            $bmp.SetPixel($ox + $x, $oy + $dstY, $tone)
        }
    }
}

# ---- 10행 : 웅크리기 ----
for ($c = 0; $c -lt 4; $c++) {
    DrawCrouchBody $bmOut ($c * $CELL) ($CROUCH_ROW * $CELL) $breathDY[$c]
}

# ---- 4행 : 웅크린 공격 ----
#   칼은 **낮게** 앞으로 나간다. 판정(heightFromFoot 10)과 같은 높이다.
$bladeY = $FOOT_Y - 10
for ($c = 0; $c -lt 6; $c++) {
    $ox = $c * $CELL; $oy = $ATTACK_ROW * $CELL
    DrawCrouchBody $bmOut $ox $oy 0

    $len = $bladeLen[$c]
    if ($len -gt 0) {
        $x0 = $torsoMaxX + 1
        Fill $bmOut $ox $oy $x0 ($bladeY - 2) ($x0 + $len)     ($bladeY + 2) $colOutline
        Fill $bmOut $ox $oy $x0 ($bladeY - 1) ($x0 + $len - 1) ($bladeY + 1) $colSteel
    }
}

$bmSrc.Dispose()
$bmOut.Save($srcPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmOut.Dispose()

Write-Output ("OK  player.png -> {0}x{1} ({2}행). 다음: gen_player_arms.ps1 을 다시 돌릴 것" -f `
    $W, $outH, ($CROUCH_ROW + 1))
