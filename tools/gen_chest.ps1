# ============================================================================
#  gen_chest.ps1
#    상자 그림.  assets/textures/chest.png  (40x16, 20x16 두 칸)
#
#      0  닫힌 상자
#      1  열린 상자  — 뚜껑이 뒤로 젖혀지고 속이 어둡다
#
#  ---- ★ 왜 icons.png 에 안 넣었나 ----
#    아이콘은 칸마다 **슬롯 테두리**를 그린다(장비 칸으로 읽히게).
#    월드에 놓는 상자에 그 테두리가 붙으면 **액자에 든 상자**가 된다.
#    쓰는 곳(UI / 월드)이 다르면 시트도 다르다.
#
#  ---- ★ 열린 그림이 **따로** 있어야 한다 ----
#    열린 상자를 지우면 「여기 상자가 있었나?」를 기억에 맡기게 되고,
#    그대로 두면 이미 연 상자를 또 열러 간다. 열린 모습이 남아야
#    **「여기는 끝났다」**가 눈으로 읽힌다.
#
#  ---- 그리는 법 : 글자 격자 (gen_icons 의 방어구와 같다) ----
#    #  테두리   w  나무   l  나무 빛   s  나무 그늘
#    m  쇠띠     y  자물쇠  d  속(어둠)   .  비움
#
#    ★ 맨 아랫줄(15)이 **바닥에 닿는 줄**이다. 그리는 쪽이 상호작용 상자의
#      아랫변에 그림 아랫변을 맞추므로, 여기서 떠 있으면 상자가 뜬다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_chest.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$out  = Join-Path $root "assets\textures\chest.png"

$W = 20; $H = 16; $COUNT = 2

$pal = @{
    '#' = [System.Drawing.Color]::FromArgb(255,  18,  18,  26)
    'w' = [System.Drawing.Color]::FromArgb(255, 140,  98,  58)
    'l' = [System.Drawing.Color]::FromArgb(255, 176, 128,  78)
    's' = [System.Drawing.Color]::FromArgb(255,  96,  66,  40)
    'm' = [System.Drawing.Color]::FromArgb(255, 120, 124, 140)
    'y' = [System.Drawing.Color]::FromArgb(255, 204, 168,  72)   # 아이콘의 금빛과 같다
    'd' = [System.Drawing.Color]::FromArgb(255,  30,  22,  18)
}

$bm = New-Object System.Drawing.Bitmap(($W * $COUNT), $H,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

# ★ 매개변수를 `$index` 로 둔다. `$w` 로 두면 PowerShell 이 대소문자를 안 가려서
#   위의 `$W`(칸 폭)와 **같은 변수**가 된다 — gen_icons 에서 실제로 밟은 함정이다.
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

# ---- 0 : 닫힘 ----
Stamp 0 @(
"....................",
"....................",
"....................",
"....................",
"..################..",
"..#llllllllllllll#..",
"..#wwwwwwwwwwwwww#..",
"..#mmmmmmmmmmmmmm#..",
"..################..",
"..#wwwwwwyywwwwww#..",
"..#wwwwwwyywwwwww#..",
"..#wwwwwwwwwwwwww#..",
"..#ssssssssssssss#..",
"..#mmmmmmmmmmmmmm#..",
"..#ssssssssssssss#..",
"..################..")

# ---- 1 : 열림 ----
#   ★ 몸통은 **닫힌 것과 같은 줄**이다(9~15). 뚜껑만 위로 올라가고 속이 보인다 —
#     열고 닫을 때 상자가 **움직이지 않아야** 같은 상자로 읽힌다.
Stamp 1 @(
"....................",
"..################..",
"..#ssssssssssssss#..",
"..#wwwwwwwwwwwwww#..",
"..#mmmmmmmmmmmmmm#..",
"..################..",
"..#dddddddddddddd#..",
"..#dddddddddddddd#..",
"..################..",
"..#wwwwwwyywwwwww#..",
"..#wwwwwwyywwwwww#..",
"..#wwwwwwwwwwwwww#..",
"..#ssssssssssssss#..",
"..#mmmmmmmmmmmmmm#..",
"..#ssssssssssssss#..",
"..################..")

$bm.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bm.Dispose()

Write-Output ("OK  chest.png  {0}x{1}  (닫힘 / 열림)" -f ($W * $COUNT), $H)
