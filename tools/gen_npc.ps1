# ============================================================================
#  gen_npc.ps1
#    NPC 그림.  assets/textures/npc.png  (256x64 — 64x64 네 칸 = 숨쉬기)
#
#      줄 0  늙은 순례자 (OLD PILGRIM) — 들판 시작 화톳불 옆
#
#  ---- ★ 새로 그리지 않고 **플레이어의 idle 을 다시 칠한다** ----
#    같은 몸집 · 같은 숨쉬기라 같은 세계의 사람으로 읽힌다. 새로 그리면
#    도트 크기와 비율이 어긋나 다른 게임에서 온 사람이 된다(gen_death 와 같은 판단).
#    ★ 그래서 칸 크기 · 프레임 수 · 속도가 플레이어 idle 과 **같다**
#      (PlayScene 의 kNpcFrames / kNpcTicksPerFrame).
#
#  ---- 플레이어와 **구별되게** ----
#    색만 바꾸면 「색이 다른 나」다. 실루엣을 바꾸는 것 둘을 더한다:
#      · 두건 — 머리 윗줄과 뒤통수를 덮는다 (얼굴은 앞쪽만 보인다)
#      · 지팡이 — 앞손 바로 앞에 세로로 하나. 머리 위까지 올라온다
#
#  ---- ★ 몸 시트에는 팔이 없다 ----
#    player.png 에는 팔이 안 그려져 있다(팔은 레이어 — gen_player_arms.ps1).
#    그래서 몸 + 앞팔 + 뒷팔 세 장을 **먼저 겹친 뒤** 칠한다. 몸만 칠하면
#    팔 없는 사람이 된다.
#
#  ※ 멱등이다 — 몇 번 돌려도 같은 그림이 나온다(원본 셋만 읽는다).
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_npc.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$tex  = Join-Path $root "assets\textures"
$out  = Join-Path $tex "npc.png"

$CELL   = 64
$FRAMES = 4        # 플레이어 idle(0행) 의 프레임 수

$srcBody  = [System.Drawing.Bitmap]::FromFile((Join-Path $tex "player.png"))
$srcFront = [System.Drawing.Bitmap]::FromFile((Join-Path $tex "player_arm_front.png"))
$srcBack  = [System.Drawing.Bitmap]::FromFile((Join-Path $tex "player_arm_back.png"))

function C($r, $g, $b) { [System.Drawing.Color]::FromArgb(255, $r, $g, $b) }

# ---- 다시 칠하기 : 원래 색 → 순례자 색 ----
#   ★ 원래 색을 **열쇠로** 쓴다. 칸 위치로 칠하면 숨쉬기로 몸이 1픽셀
#     오르내릴 때 어긋난다 — 색은 따라다닌다.
$recolor = @{
    "78,92,124"   = (C 118  96  66)    # 몸통 · 앞팔  → 겉옷(갈색)
    "52,58,80"    = (C  82  64  46)    # 다리 · 뒷팔  → 겉옷 그늘
    "224,198,168" = (C 206 186 162)    # 살갗         → 조금 바랜 살갗(늙었다)
}
$HOOD    = C  96  78  56     # 두건 — 겉옷보다 한 단 어둡게(같은 천)
$STAFF   = C 150 112  66     # 지팡이 나무
$OUTLINE = C  18  18  26     # 플레이어의 테두리와 같은 색
$SKIN    = "224,198,168"

function Key($c) { "{0},{1},{2}" -f $c.R, $c.G, $c.B }

$bm = New-Object System.Drawing.Bitmap(($CELL * $FRAMES), $CELL,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

for ($f = 0; $f -lt $FRAMES; $f++) {
    $ox = $f * $CELL

    # ---- ① 세 장 겹치기 (몸 → 앞팔 → 뒷팔 = 게임이 겹치는 순서) ----
    $px = New-Object 'System.Drawing.Color[,]' $CELL, $CELL
    $isFront = New-Object 'bool[,]' $CELL, $CELL
    for ($y = 0; $y -lt $CELL; $y++) {
        for ($x = 0; $x -lt $CELL; $x++) {
            $c = [System.Drawing.Color]::Transparent
            foreach ($s in $srcBody, $srcFront, $srcBack) {
                $p = $s.GetPixel($ox + $x, $y)
                if ($p.A -gt 0) { $c = $p }
            }
            $px[$x, $y] = $c
            $isFront[$x, $y] = ($srcFront.GetPixel($ox + $x, $y).A -gt 0)
        }
    }

    # ---- ② 머리(살갗)와 앞팔의 범위 — 칸마다 잰다(숨쉬기로 오르내린다) ----
    $hl = 99; $hr = -1; $ht = 99; $hb = -1
    $fr = -1; $fb = -1
    $feet = -1
    for ($y = 0; $y -lt $CELL; $y++) {
        for ($x = 0; $x -lt $CELL; $x++) {
            $c = $px[$x, $y]
            if ($c.A -eq 0) { continue }
            if ($y -gt $feet) { $feet = $y }
            if ((Key $c) -eq $SKIN) {
                if ($x -lt $hl) { $hl = $x }; if ($x -gt $hr) { $hr = $x }
                if ($y -lt $ht) { $ht = $y }; if ($y -gt $hb) { $hb = $y }
            }
            if ($isFront[$x, $y]) {
                if ($x -gt $fr) { $fr = $x }; if ($y -gt $fb) { $fb = $y }
            }
        }
    }
    if ($hr -lt 0 -or $fr -lt 0) { throw ("{0}번 칸: 머리나 앞팔을 못 찾았다" -f $f) }

    # ---- ③ 칠하기 + 두건 ----
    #   두건 = 살갗 중 **윗줄 넷** 과 **뒤쪽(왼쪽) 절반**. 시트는 오른쪽을 본다 —
    #   얼굴(눈이 있는 쪽)은 앞(오른쪽)이라 남는다.
    $hoodBackX = $hl + [int](($hr - $hl) / 2) - 1
    for ($y = 0; $y -lt $CELL; $y++) {
        for ($x = 0; $x -lt $CELL; $x++) {
            $c = $px[$x, $y]
            if ($c.A -eq 0) { continue }
            $k = Key $c
            if ($k -eq $SKIN -and ($y -le $ht + 3 -or $x -le $hoodBackX)) {
                $bm.SetPixel($ox + $x, $y, $HOOD)
            }
            elseif ($recolor.ContainsKey($k)) {
                $bm.SetPixel($ox + $x, $y, $recolor[$k])
            }
            else {
                $bm.SetPixel($ox + $x, $y, $c)   # 테두리 · 눈은 그대로
            }
        }
    }

    # ---- ④ 지팡이 — 앞팔 바로 앞, 머리 위에서 발끝까지 ----
    #   ★ 테두리를 양옆에 둔다. 1픽셀 나무색만 세우면 배경에 묻힌다.
    #     단 **비어 있는 칸에만** 테두리를 친다 — 팔을 덮으면 손이 지팡이 뒤로 숨는다.
    $sx = $fr + 2
    for ($y = $ht - 4; $y -le $feet; $y++) {
        $bm.SetPixel($ox + $sx, $y, $STAFF)
        foreach ($ex in ($sx - 1), ($sx + 1)) {
            if ($bm.GetPixel($ox + $ex, $y).A -eq 0) { $bm.SetPixel($ox + $ex, $y, $OUTLINE) }
        }
    }
    $bm.SetPixel($ox + $sx, $ht - 5, $OUTLINE)   # 지팡이 머리
}

$srcBody.Dispose(); $srcFront.Dispose(); $srcBack.Dispose()

$bm.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bm.Dispose()

Write-Output ("OK  npc.png  {0}x{1}  (줄 0 = 늙은 순례자, {2}프레임)" -f ($CELL * $FRAMES), $CELL, $FRAMES)
