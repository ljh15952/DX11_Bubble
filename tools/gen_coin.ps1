# ============================================================================
#  gen_coin.ps1
#    동전 그림.  assets/textures/coin.png  (32x16, 16x16 두 칸)   ★ 10-c-1
#
#      0  동전 하나  — 화면 오른쪽 아래 「가진 동전」 옆
#      1  동전 더미  — **죽은 자리**에 떨군 동전(되찾기 전까지 남는다)
#
#  ---- ★ 왜 icons.png 에 안 넣었나 ----
#    아이콘은 칸마다 **슬롯 테두리**를 그린다(장비 칸으로 읽히게). 동전은
#    장비가 아니다 — HUD 숫자 옆에 테두리가 붙으면 「동전 칸」이 하나 있는 것처럼
#    읽히고, 땅에 놓으면 액자에 든 동전이 된다. 상자(gen_chest)와 같은 판단이다.
#
#  ---- 그리는 법 : 글자 격자 (gen_chest 와 같다) ----
#    #  테두리   y  금   l  금 빛   s  금 그늘   .  비움
#    ★ 금색은 상자 자물쇠 · 초점 안내(Gold)와 **같은 계열**이다 — 「값나가는 것」이
#      화면 어디서나 같은 색으로 읽힌다.
#    ★ 더미의 맨 아랫줄(15)이 **바닥에 닿는 줄**이다. 그리는 쪽이 죽은 자리의
#      발끝에 그림 아랫변을 맞춘다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_coin.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$out  = Join-Path $root "assets\textures\coin.png"

$W = 16; $H = 16; $COUNT = 2

$pal = @{
    '#' = [System.Drawing.Color]::FromArgb(255,  18,  18,  26)
    'y' = [System.Drawing.Color]::FromArgb(255, 204, 168,  72)   # 상자 자물쇠와 같다
    'l' = [System.Drawing.Color]::FromArgb(255, 244, 214, 120)
    's' = [System.Drawing.Color]::FromArgb(255, 150, 112,  40)
}

$bm = New-Object System.Drawing.Bitmap(($W * $COUNT), $H,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

# ★ 매개변수를 `$index` 로 둔다 — `$w` 로 두면 PowerShell 이 대소문자를 안 가려서
#   위의 `$W`(칸 폭)와 같은 변수가 된다(gen_icons 에서 밟은 함정).
function Stamp($index, [string[]]$rows) {
    if ($rows.Count -ne $H) { throw ("{0}번 칸: {1}줄이어야 한다 ({2})" -f $index, $H, $rows.Count) }
    $ox = $W * $index
    for ($y = 0; $y -lt $H; $y++) {
        $r = $rows[$y]
        if ($r.Length -ne $W) { throw ("{0}번 칸 {1}줄: {2}글자여야 한다 ({3})" -f $index, $y, $W, $r.Length) }
        for ($x = 0; $x -lt $W; $x++) {
            $ch = [string]$r[$x]
            if ($ch -eq '.') { continue }
            $bm.SetPixel($ox + $x, $y, $pal[$ch])
        }
    }
}

# ---- 0 : 동전 하나 — 왼쪽 위가 빛, 오른쪽 아래가 그늘. 가운데 새김 한 줄 ----
Stamp 0 @(
"................",
"................",
"................",
".....######.....",
"....#llyyyy#....",
"...#lyyyyyyy#...",
"...#lyyysyyy#...",
"...#yyyysyyy#...",
"...#yyyysyys#...",
"...#yyyysyys#...",
"...#yyyyyyss#...",
"....#yyysss#....",
".....######.....",
"................",
"................",
"................")

# ---- 1 : 동전 더미 — 아래가 넓은 무더기. 「s」 점이 동전 테두리로 읽힌다 ----
Stamp 1 @(
"................",
"................",
"................",
"................",
"................",
"................",
"................",
"................",
"................",
"......####......",
".....#llyy#.....",
"....#lyyyys#....",
"...#lyyysyys#...",
"..#lyyysyyyss#..",
".#yyysyyyysyss#.",
".##############.")

$bm.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bm.Dispose()

Write-Output ("OK  coin.png  {0}x{1}  (동전 하나 / 더미)" -f ($W * $COUNT), $H)
