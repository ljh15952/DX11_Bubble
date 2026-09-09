# ============================================================================
#  gen_enemy_attack.ps1
#    assets/textures/enemy.png 에 공격 애니메이션 2행을 추가한다.
#
#      row 0  idle  (4 프레임)   ← 기존. 그대로 복사한다
#      row 1  crawl (4 프레임)   ← 기존. 그대로 복사한다
#      row 2  swing (5 프레임)   ← 새로 그린다. 서서 팔 휘두르기
#      row 3  bite  (6 프레임)   ← 새로 그린다. 엎드려 물어뜯기
#
#    384x128  ->  384x256
#
#  ---- ★ 프레임 데이터에 애니메이션을 맞추는 규칙 ----
#
#    이 프로젝트의 원칙은 「상태 길이는 프레임 데이터가 정하고,
#    애니메이션이 거기에 맞춘다」다. 그래서 두 조건을 만족시켜야 한다.
#
#      ① frameCount * ticksPerFrame  ==  startup + active + recovery
#         (애니메이션이 상태보다 먼저 끝나거나 잘리지 않는다)
#
#      ② startup / ticksPerFrame     ==  타격 프레임의 인덱스  (정수여야 한다)
#         (팔이 뻗는 그림이 판정이 켜지는 바로 그 틱에 시작한다)
#
#    ②를 정수로 만들기 위해 프레임 데이터를 2~3틱 조정했다.
#    데이터가 진실이지만, 「보기와 판정이 어긋나는」 것보다는
#    데이터를 반올림하는 편이 싸다.
#
#      swing  startup 24  active 4  recovery 32  = 60 틱  <-  5 프레임 x 12 틱
#             24 / 12 = 2   -> 프레임 2 가 타격
#      bite   startup 18  active 3  recovery 33  = 54 틱  <-  6 프레임 x  9 틱
#             18 /  9 = 2   -> 프레임 2 가 타격
#
#    두 공격 모두 타격이 프레임 2 다. 우연이 아니라 그렇게 맞췄다.
#
#  ---- 그림을 새로 그리지 않고 기존 셀을 재사용하는 이유 ----
#
#    Chase(row 0) -> Attack(row 2) -> Chase 로 전환될 때 몸통 실루엣이
#    조금이라도 다르면 화면에서 "톡" 튀어 보인다.
#    그래서 몸은 기존 셀을 그대로 복사하고, **팔만 위에 얹는다.**
#    player.png 의 공격 행도 같은 방식이다 (몸은 고정, 칼만 움직인다).
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_enemy_attack.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$path = Join-Path $root "assets\textures\enemy.png"

$CELL = 64
$COLS = 6

# ---- 원본 팔레트 (기존 시트에서 뽑은 값. 바꾸면 새 행만 색이 달라진다) ----
$OUTLINE = [System.Drawing.Color]::FromArgb(255,  16,  12,  14)
$MAROON  = [System.Drawing.Color]::FromArgb(255, 112,  58,  58)   # 몸통 본색
$LIGHT   = [System.Drawing.Color]::FromArgb(255, 175, 175, 175)   # 발톱 / 이빨

# ---- 원본을 메모리로 읽는다 ----
#   FromFile 은 파일을 잠그기 때문에 같은 경로에 저장할 수 없다.
$bytes = [System.IO.File]::ReadAllBytes($path)
$ms    = New-Object System.IO.MemoryStream(,$bytes)
$srcImg = [System.Drawing.Image]::FromStream($ms)

$src = New-Object System.Drawing.Bitmap($srcImg.Width, $srcImg.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($src)
$g.DrawImage($srcImg, 0, 0, $srcImg.Width, $srcImg.Height)
$g.Dispose()
$srcImg.Dispose()
$ms.Dispose()

if ($src.Width -ne ($CELL * $COLS)) { throw "예상 밖의 폭: $($src.Width)" }

# ★ 원본에서 **앞 2행만** 옮긴다.
#   이미 4행이 된 파일에 다시 돌려도 같은 결과가 나오게 하기 위해서다.
#   전부 옮기면 지난번에 그린 팔이 남아 새 팔과 겹친다.
$KEEP_ROWS = 2

$out = New-Object System.Drawing.Bitmap(($CELL * $COLS), ($CELL * 4), [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

# ---- 기존 2행을 픽셀 단위로 그대로 옮긴다 ----
#   DrawImage 는 보간이 끼어들 수 있으므로 SetPixel 로 옮긴다.
#   도트 그림에서 1픽셀이 흐려지면 그 자체로 버그다.
for ($y = 0; $y -lt ($CELL * $KEEP_ROWS); $y++) {
    for ($x = 0; $x -lt $src.Width; $x++) {
        $out.SetPixel($x, $y, $src.GetPixel($x, $y))
    }
}

# ----------------------------------------------------------------------------
#  셀 복사
#    y0 부터 아래만 복사한다. 기존 시트는 셀 위쪽(y 0..8)에 프레임 번호를
#    디버그용으로 구워 놓았는데, 그것까지 새 행에 옮기면 화면에 색 점으로 보인다.
#    shiftX 는 몸 전체를 가로로 밀어 "달려들기" 를 만든다.
# ----------------------------------------------------------------------------
function Copy-Cell($srcCol, $srcRow, $dstCol, $dstRow, $y0, $shiftX) {
    $sx = $srcCol * $CELL
    $sy = $srcRow * $CELL
    $dx = $dstCol * $CELL
    $dy = $dstRow * $CELL
    for ($y = $y0; $y -lt $CELL; $y++) {
        for ($x = 0; $x -lt $CELL; $x++) {
            $c = $src.GetPixel($sx + $x, $sy + $y)
            if ($c.A -eq 0) { continue }
            $tx = $x + $shiftX
            if ($tx -lt 0 -or $tx -ge $CELL) { continue }
            $out.SetPixel($dx + $tx, $dy + $y, $c)
        }
    }
}

function Fill-Rect($col, $row, $x0, $y0, $x1, $y1, $color) {
    $ox = $col * $CELL
    $oy = $row * $CELL
    for ($y = $y0; $y -le $y1; $y++) {
        for ($x = $x0; $x -le $x1; $x++) {
            if ($x -lt 0 -or $x -ge $CELL -or $y -lt 0 -or $y -ge $CELL) { continue }
            $out.SetPixel($ox + $x, $oy + $y, $color)
        }
    }
}

# 팔 하나. 테두리를 먼저 크게 칠하고 그 안을 본색으로 덮는 도트 관례를 따른다.
#
#  ★ 함정: 팔의 테두리가 **몸통 안쪽에 떨어지면 몸이 갈라져 보인다.**
#    처음에 뒤로 젖힌 팔을 x 8..23 으로 그렸더니, 테두리 오른쪽 끝(x 24)이
#    몸통(x 21..42) 안에 들어가 세로로 검은 선이 생겼다.
#    도트 그림에서 1픽셀 선은 「팔」이 아니라 「금」으로 읽힌다.
#
#    해법: 팔의 끝을 몸통 테두리에 **정확히 붙인다.**
#      뒤로 젖힌 팔  -> 채움을 x 20 에서 끝내면 테두리가 x 21 = 몸통 왼쪽 테두리
#      앞으로 뻗은 팔 -> 채움을 x 43 에서 시작하면 테두리가 x 42 = 몸통 오른쪽 테두리
#    같은 색이 같은 자리에 겹치므로 선이 생기지 않는다.
function Draw-Arm($col, $row, $x0, $y0, $x1, $y1, $claw) {
    Fill-Rect $col $row ($x0-1) ($y0-1) ($x1+1) ($y1+1) $OUTLINE
    Fill-Rect $col $row  $x0     $y0     $x1     $y1    $MAROON
    if ($claw) {
        # 끝에 밝은 점 = 발톱. 어디가 위험한지를 1프레임 안에 알려 준다.
        Fill-Rect $col $row ($x1-3) ($y0+1) $x1 ($y1-1) $LIGHT
    }
}

# ============================================================================
#  row 2 — swing (5 프레임 x 12 틱 = 60)
#
#    f0  t 0..11   팔을 뒤로 당기기 시작
#    f1  t12..23   최대로 젖힘  = 예고의 절정
#    f2  t24..35   ★ 팔을 뻗는다  (판정 t24..27 이 이 프레임 안에서 시작한다)
#    f3  t36..47   뻗은 팔이 처진다 (후딜)
#    f4  t48..59   회수
#
#    몸통은 x 21..42 / y 26..46. 팔은 그 중간 높이(y 31..36)에 둔다.
#    히트박스가 heightFromFoot 30 이므로 셀 좌표로 y 32 가 중심 —
#    그림과 판정이 같은 높이에 있다.
# ============================================================================
$IDLE_BASE = 0     # idle 프레임 0 을 몸통으로 쓴다

for ($f = 0; $f -lt 5; $f++) { Copy-Cell $IDLE_BASE 0 $f 2 9 0 }

#   x 끝값이 20 / 43 인 것이 위 함정의 해법이다. 임의의 숫자가 아니다.
Draw-Arm 0 2 14 31 20 36 $false     # f0  뒤로       (테두리 13..21)
Draw-Arm 1 2  8 31 20 36 $false     # f1  최대로 뒤로 (테두리  7..21)
Draw-Arm 2 2 43 31 60 36 $true      # f2  ★ 타격     (테두리 42..61)
Draw-Arm 3 2 43 34 56 39 $true      # f3  처진다
Draw-Arm 4 2 43 31 50 36 $false     # f4  회수 (짧게 남는다)

# ============================================================================
#  row 3 — bite (6 프레임 x 9 틱 = 54)
#
#    f0  t 0.. 8   몸을 살짝 뒤로
#    f1  t 9..17   최대로 뒤로  = 예고 (swing 보다 짧다. 그래서 더 무섭다)
#    f2  t18..26   ★ 달려들어 문다  (판정 t18..20)
#    f3  t27..35   물린 채
#    f4  t36..44   회수
#    f5  t45..53   원자세
#
#    ★ 엎드린 몸은 머리만 따로 움직이기가 어렵다(머리와 몸이 붙어 있어서
#      머리를 옮기면 원래 자리를 지워야 한다). 그래서 **몸 전체를 밀었다.**
#      기어다니는 것이 달려드는 동작으로 읽히고, 그림을 새로 그릴 필요가 없다.
#
#      대가: 판정 상자(EnemyPartBox)는 고정이므로 최대 5px 어긋난다.
#      F1 로 보면 보이지만, 물기 사거리가 20px 이라 게임에는 영향이 없다.
# ============================================================================
$CRAWL_BASE = 0    # crawl 프레임 0 을 몸통으로 쓴다
$biteShift  = @(-2, -4, 5, 4, 1, 0)

for ($f = 0; $f -lt 6; $f++) {
    Copy-Cell $CRAWL_BASE 1 $f 3 30 $biteShift[$f]
}

# 이빨 — 무는 두 프레임에만.
#
#  ★ 같은 함정의 다른 형태: 처음에 x 49..52(+shift) 에 얹었더니
#    머리의 **오른쪽 테두리(x 52)를 지워** 머리에 구멍이 뚫렸다.
#    머리는 원이라 y 마다 테두리 x 가 달라진다 — 넉넉히 안쪽에 둔다.
Fill-Rect 2 3 (46 + 5) 46 (49 + 5) 48 $LIGHT
Fill-Rect 3 3 (46 + 4) 46 (49 + 4) 48 $LIGHT

# ---- 저장 ----
$src.Dispose()
$out.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$out.Dispose()

Write-Output "OK  $path  ->  384 x 256 (4 rows)"
