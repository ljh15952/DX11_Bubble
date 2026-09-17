# ============================================================================
#  gen_icons.ps1
#    장비 슬롯 아이콘.  assets/textures/icons.png  (64x16, 16x16 4칸)
#
#      0  빈손      — 테두리만
#      1  단검      — 칼날 + 손잡이
#      2  잘림      — 붉은 X
#      3  이빨      — 무기가 없어도 쓸 수 있는 것(물기)
#      4  대검      — 두껍고 긴 날 + 넓은 날밑   ★ 8-a 에서 추가
#
#  ---- ★ 아이콘 번호는 **무기 데이터가 들고 있다** ----
#    `WeaponType::icon`. 코드에 `if (단검) … else if (대검) …` 을 쓰면
#    무기를 하나 추가할 때마다 그 switch 를 찾아 고쳐야 한다 —
#    적 종류를 카탈로그로 만든 것과 같은 이유다.
#
#  ---- ★★ 이 한 장이 **두 곳**에 쓰인다 ----
#    손 슬롯 UI + **땅에 떨어진 무기**. 전에는 바닥의 무기를 사각형 두 개로
#    그렸는데, 무기가 둘이 되는 순간 「어느 쪽이 떨어져 있는지」를 알 수가 없다.
#    같은 그림을 쓰면 「UI 의 그것」과 「바닥의 그것」이 같은 물건으로 읽힌다.
#
#  ---- ★ 왜 글자가 아니라 그림인가 ----
#    폰트가 ASCII 전용이라 이미 몸 상태를 **그림**으로 표시하고 있다(§3.2.3).
#    손에 든 것도 같은 층의 정보이므로 같은 방식이어야 한다 —
#    한쪽만 글자면 눈이 두 번 읽어야 한다.
#
#    그리고 「오른손이 잘려 무기를 떨궜다」는 **한 칸이 X 로 바뀌는 것**으로
#    한눈에 읽힌다. 문장으로 쓰면 읽어야 알 수 있다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_icons.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root "assets\textures"
$out  = Join-Path $dir "icons.png"

$CELL   = 16
$CELLS  = 5

$colFrame = [System.Drawing.Color]::FromArgb(255,  92,  96, 112)
$colSteel = [System.Drawing.Color]::FromArgb(255, 206, 210, 220)
$colGrip  = [System.Drawing.Color]::FromArgb(255,  96,  72,  48)
$colWound = [System.Drawing.Color]::FromArgb(255, 168,  46,  46)
$colTooth = [System.Drawing.Color]::FromArgb(255, 240, 240, 246)

# ★ 대검은 **더 어둡다.** 크기만 키우면 16픽셀 안에서는 「큰 단검」으로 보인다.
#   색이 한 단 어두우면 **무겁다**가 같이 읽힌다 — 도트에서 무게는 명도다.
$colHeavy = [System.Drawing.Color]::FromArgb(255, 158, 166, 186)
$colEdge  = [System.Drawing.Color]::FromArgb(255, 222, 226, 236)

$bm = New-Object System.Drawing.Bitmap(($CELL * $CELLS), $CELL,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

function Px($ox, $x, $y, $tone) {
    if ($x -lt 0 -or $x -ge $CELL -or $y -lt 0 -or $y -ge $CELL) { return }
    $script:bm.SetPixel($ox + $x, $y, $tone)
}

function Frame($ox) {
    # 네 변. 모든 칸이 같은 테두리를 가지면 「슬롯」으로 읽힌다.
    for ($i = 0; $i -lt $CELL; $i++) {
        Px $ox $i 0            $script:colFrame
        Px $ox $i ($CELL - 1)  $script:colFrame
        Px $ox 0           $i  $script:colFrame
        Px $ox ($CELL - 1) $i  $script:colFrame
    }
}

for ($c = 0; $c -lt $CELLS; $c++) { Frame ($c * $CELL) }

# ---- 1 : 단검 ----
#   왼쪽 아래에서 오른쪽 위로. 대각선이라 16픽셀 안에서도 「칼」로 읽힌다.
$ox = $CELL
for ($i = 0; $i -lt 8; $i++) {
    Px $ox (4 + $i) (11 - $i) $colSteel
    Px $ox (5 + $i) (11 - $i) $colSteel
}
for ($i = 0; $i -lt 3; $i++) { Px $ox (3 + $i) (12 - $i) $colGrip }
Px $ox 6 11 $colGrip
Px $ox 3 10 $colGrip

# ---- 2 : 잘림 (X) ----
#   ★ 대각선을 1픽셀로 그으면 **점선으로 보인다.** 도트에서 대각선은
#     옆 픽셀을 하나 더 찍어야 이어진 선이 된다.
$ox = $CELL * 2
for ($i = 0; $i -lt 8; $i++) {
    Px $ox (4 + $i)  (4 + $i) $colWound
    Px $ox (5 + $i)  (4 + $i) $colWound
    Px $ox (11 - $i) (4 + $i) $colWound
    Px $ox (10 - $i) (4 + $i) $colWound
}

# ---- 3 : 이빨 ----
#   위아래로 마주 보는 삼각 두 개. 「문다」가 형태만으로 읽힌다.
#   ★ 가운데를 **비워야** 입으로 읽힌다. 붙으면 그냥 쐐기 하나가 된다.
$ox = $CELL * 3
for ($i = 0; $i -lt 4; $i++) {
    for ($j = 0; $j -le $i; $j++) {
        Px $ox (4 + $i * 2)     (3 + $j)   $script:colTooth
        Px $ox (4 + $i * 2 + 1) (3 + $j)   $script:colTooth
        Px $ox (4 + $i * 2)     (12 - $j)  $script:colTooth
        Px $ox (4 + $i * 2 + 1) (12 - $j)  $script:colTooth
    }
}

# ---- 4 : 대검 ----
#   ★ 단검과 **같은 대각선**이다. 방향까지 바꾸면 「다른 물건」이 아니라
#     「다른 그림」이 되어 버린다 — 비교가 되려면 축이 같아야 한다.
#     다른 것은 **두께 · 길이 · 밝기 · 날밑**뿐이다.
$ox = $CELL * 4
# ① 날 — **줄 단위로** 그린다.
#   ★ 처음엔 단검처럼 「대각선 스탬프」를 겹쳐 찍었는데, 다음 바퀴의 스탬프가
#     앞 바퀴를 덮어 두께가 2픽셀로 줄었다. 대각선을 두껍게 그릴 때는
#     **「한 줄에 몇 픽셀인가」로 적어야** 의도한 두께가 그대로 나온다.
#   한 줄에 4픽셀 = 45도에서 두께 약 3픽셀 (단검은 한 줄 2픽셀).
for ($i = 0; $i -lt 10; $i++) {
    $y = 12 - $i
    Px $ox (3 + $i) $y $colHeavy
    Px $ox (4 + $i) $y $colHeavy
    Px $ox (5 + $i) $y $colHeavy
    # 위쪽(바깥) 모서리에 밝은 선 — 두꺼운 날의 **면**이 보인다
    Px $ox (6 + $i) $y $colEdge
}
# ③ 날밑(가드) — 날에 **직각**으로 가로지른다. 이것 하나가 「대검」을 만든다
for ($i = 0; $i -lt 4; $i++) {
    Px $ox (2 + $i) (9 + $i) $colFrame
    Px $ox (3 + $i) (9 + $i) $colFrame
}
# ④ 자루 — 두 손으로 잡는 길이
Px $ox 2 13 $colGrip
Px $ox 3 13 $colGrip
Px $ox 3 12 $colGrip
Px $ox 4 12 $colGrip
Px $ox 4 13 $colGrip

$bm.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bm.Dispose()

Write-Output ("OK  icons.png  {0}x{1}  ({2}칸)" -f ($CELL * $CELLS), $CELL, $CELLS)
