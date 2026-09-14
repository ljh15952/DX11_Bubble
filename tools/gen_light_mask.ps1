# ============================================================================
#  gen_light_mask.ps1
#    어둠 마스크 한 장.  assets/textures/light_mask.png  (640x320)
#
#  ---- ★ 왜 이것만으로 「어둠」이 되는가 ----
#    기획서(§3.9 A)는 「캔버스에 다 그린 뒤 마스크를 곱한다 = 3패스」로
#    적어 두었는데, 실제로는 **패스를 늘릴 필요가 없다.**
#
#      가운데가 투명하고 바깥이 검은 그림을 **한 장 덮으면** 끝이다.
#      알파 블렌딩이 그대로 「보이는 만큼만 보인다」가 된다.
#
#    셰이더도, 새 렌더 타깃도, 곱하기 블렌드도 필요 없다.
#    ★ 「어떻게 만들까」보다 **「무엇이면 충분한가」**를 먼저 묻는 편이 싸다.
#
#  ---- ★ 앞쪽이 더 멀리 보인다 ----
#    원형으로만 만들면 「등불」이지 「시야」가 아니다.
#    앞쪽 반경을 크게 잡아 계란형으로 만들면, 적의 부채꼴과 **대칭**이 되어
#    §3.9 의 「둘은 같은 규칙의 양면」이 실제로 성립한다.
#
#    ★ 그림 한 장이면 된다 — 오른쪽을 보는 모양으로 그려 두고
#      왼쪽을 볼 때는 좌우 반전해서 그린다(스프라이트와 같은 방식).
#
#  ---- ★ 어둠의 진하기는 여기 굽지 않는다 ----
#    가장자리는 **알파 255(완전 불투명)** 로 만들고, 실제 진하기는 그릴 때
#    틴트의 알파로 정한다. 그래야 숫자 하나로 조절할 수 있고,
#    「밝은 곳 / 동굴」처럼 장소마다 다르게 두는 것도 그림을 다시 안 만든다.
#
#  사용법:  powershell -ExecutionPolicy Bypass -File tools/gen_light_mask.ps1
# ============================================================================

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root "assets\textures"
$out  = Join-Path $dir "light_mask.png"

# ---- 크기와 빛의 중심 ----
#   원점을 **한가운데**에 둔다. 좌우 반전이 원점을 기준으로 일어나므로,
#   가운데가 아니면 뒤집을 때 빛이 옆으로 튄다.
$W = 640; $H = 320
$CX = [int]($W / 2); $CY = [int]($H / 2)

# ---- 반경 ----
#   앞(오른쪽)이 뒤보다 2배 이상 멀다. 캔버스가 640x360 이므로
#   앞 300 이면 화면의 절반 가까이를 밝힌다.
$R_FRONT = 300.0
$R_BACK  = 130.0
$R_VERT  = 150.0

# 완전히 밝은 안쪽의 비율. 여기까지는 알파 0.
$INNER = 0.42

$bm = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

# ---- LockBits 로 직접 쓴다 ----
#   SetPixel 로 20만 픽셀은 끝나지 않는다(handoff §8).
$rect = New-Object System.Drawing.Rectangle(0, 0, $W, $H)
$data = $bm.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
                     [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $data.Stride
$buf = New-Object byte[] ($stride * $H)

for ($y = 0; $y -lt $H; $y++) {
    $rowOff = $y * $stride
    $ny = ($y - $CY) / $R_VERT

    for ($x = 0; $x -lt $W; $x++) {
        $dx = $x - $CX
        # ★ 앞뒤로 반경이 다르다 = 좌표를 각각 다른 값으로 나눈다.
        #   그러면 원 하나를 재는 식 그대로 계란형이 된다.
        $nx = if ($dx -ge 0) { $dx / $R_FRONT } else { $dx / $R_BACK }

        $d = [Math]::Sqrt($nx * $nx + $ny * $ny)

        # 안쪽은 완전히 밝고, 거기서부터 부드럽게 어두워진다.
        $t = ($d - $INNER) / (1.0 - $INNER)
        if ($t -lt 0.0) { $t = 0.0 }
        if ($t -gt 1.0) { $t = 1.0 }

        # ★ 제곱해서 **가운데를 넓게** 남긴다. 선형이면 중심부터 바로
        #   어두워져 「등불 안에 있다」는 느낌이 안 난다.
        $a = [int][Math]::Round(255.0 * $t * $t)

        $i = $rowOff + $x * 4
        $buf[$i]     = 0      # B
        $buf[$i + 1] = 0      # G
        $buf[$i + 2] = 0      # R
        $buf[$i + 3] = [byte]$a
    }
}

[System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $data.Scan0, $buf.Length)
$bm.UnlockBits($data)
$bm.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bm.Dispose()

Write-Output ("OK  light_mask.png  {0}x{1}  (앞 {2} / 뒤 {3} / 위아래 {4})" -f `
    $W, $H, $R_FRONT, $R_BACK, $R_VERT)
