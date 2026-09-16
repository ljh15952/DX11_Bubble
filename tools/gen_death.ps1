# ============================================================================
#  gen_death.ps1
#    **쓰러진 그림**을 플레이어와 적 양쪽에 추가한다.
#      player.png : 14행   (384x896 -> 384x960)
#      enemy.png  :  4행   (384x256 -> 384x320)
#
#  ---- ★ 왜 필요한가 ----
#    죽을 때 **기어가기 행**을 빌려 쓰고 있었다. 「누운 그림이니 시체로
#    읽히겠지」였는데, 기어가기는 **다리가 잘렸을 때의 자세**다.
#    그래서 어떻게 죽든 **다리가 잘린 채 죽은 것처럼** 보였다.
#
#    ★★ 빌려 쓴 그림은 **원래 뜻을 같이 가져온다.** 「형태가 비슷하다」로
#      고르면 안 되고, **그 그림이 무엇을 뜻하는지**로 골라야 한다.
#
#  ---- ★ 만드는 법 : 서 있는 몸을 90도 눕힌다 ----
#    쓰러진 몸을 새로 그리면 같은 캐릭터로 안 보인다(머리 모양·눈 위치).
#    **idle 0번 칸을 통째로 90도 돌리면** 색도 비율도 그대로다.
#
#    ★ 발끝이 원점이므로 돌린 뒤 **바닥에 붙여** 놓는다.
#      머리가 앞(오른쪽)을 향하게 둔다 — facing 으로 뒤집으면 반대가 된다.
#
#  ★ player.png 를 건드리므로 뒤에 **gen_player_arms.ps1 을 다시** 돌린다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_death.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root "assets\textures"

$CELL   = 64
$FOOT_Y = 62

# (파일, 유지할 행 수, 만들 행)
$jobs = @(
    @{ file = "player.png"; keep = 14; row = 14 },
    @{ file = "enemy.png";  keep =  4; row =  4 }
)

foreach ($j in $jobs) {
    $path = Join-Path $dir $j.file

    $bytes = [System.IO.File]::ReadAllBytes($path)
    $ms    = New-Object System.IO.MemoryStream(,$bytes)
    $img   = [System.Drawing.Image]::FromStream($ms)
    $W = $img.Width
    $src = New-Object System.Drawing.Bitmap($W, $img.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g0 = [System.Drawing.Graphics]::FromImage($src)
    $g0.DrawImage($img, 0, 0, $W, $img.Height)
    $g0.Dispose(); $img.Dispose(); $ms.Dispose()

    if ($src.Height -lt $j.keep * $CELL) {
        throw ("{0} 가 너무 작다 ({1})" -f $j.file, $src.Height)
    }

    # ---- idle 0번 칸에서 몸의 경계를 찾는다 ----
    $rect = New-Object System.Drawing.Rectangle(0, 0, $src.Width, $src.Height)
    $data = $src.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $data.Stride
    $buf = New-Object byte[] ($stride * $src.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
    $src.UnlockBits($data)

    $minX = 999; $maxX = -1; $minY = 999; $maxY = -1
    for ($y = 0; $y -lt $CELL; $y++) {
        $rowOff = $y * $stride
        for ($x = 0; $x -lt $CELL; $x++) {
            if ($buf[$rowOff + $x * 4 + 3] -eq 0) { continue }
            if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
            if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
        }
    }
    if ($maxX -lt 0) { throw ("{0} : idle 0번 칸이 비었다" -f $j.file) }

    $bodyW = $maxX - $minX + 1
    $bodyH = $maxY - $minY + 1

    # ---- 출력 ----
    $outH = ($j.row + 1) * $CELL
    $out = New-Object System.Drawing.Bitmap($W, $outH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($out)
    $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $keep = New-Object System.Drawing.Rectangle(0, 0, $W, ($j.keep * $CELL))
    $g.DrawImage($src, $keep, $keep, [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose()

    # ---- 90도 눕히기 ----
    #   (x, y) -> (돌린 뒤 x, y).  머리(위)가 앞(오른쪽)으로 가게.
    #     원래 y 가 작을수록(머리) 새 x 가 커진다
    #     원래 x 가 클수록          새 y 가 커진다
    $oy = $j.row * $CELL
    $destBottom = $FOOT_Y                 # 바닥에 붙인다
    $destTop    = $destBottom - $bodyW + 1   # 누우면 **몸 너비가 높이**가 된다
    $destLeft   = 32 - [int]($bodyH / 2)     # 가운데 정렬

    for ($y = $minY; $y -le $maxY; $y++) {
        $rowOff = $y * $stride
        for ($x = $minX; $x -le $maxX; $x++) {
            $i = $rowOff + $x * 4
            if ($buf[$i + 3] -eq 0) { continue }

            $nx = $destLeft + ($maxY - $y)
            $ny = $destTop  + ($x - $minX)
            if ($nx -lt 0 -or $nx -ge $CELL -or $ny -lt 0 -or $ny -ge $CELL) { continue }

            $tone = [System.Drawing.Color]::FromArgb($buf[$i+3], $buf[$i+2], $buf[$i+1], $buf[$i])
            $out.SetPixel($nx, $oy + $ny, $tone)
        }
    }

    $src.Dispose()
    $out.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $out.Dispose()

    Write-Output ("OK  {0} -> {1}x{2}  ({3}행 = 쓰러짐, 몸 {4}x{5})" -f `
        $j.file, $W, $outH, ($j.row + 1), $bodyW, $bodyH)
}

Write-Output "다음: gen_player_arms.ps1 을 다시 돌릴 것"
