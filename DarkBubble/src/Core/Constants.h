// ============================================================================
//  Constants.h
//    여러 파일이 공유하는 설정값. 여기 말고 다른 곳에 흩어놓지 않는다.
// ============================================================================
#pragma once

namespace Config
{
    // ---- 내부 해상도 ----
    //   게임은 이 크기의 텍스처에 그려진다.
    //   ★ 좌표 / 이동 속도 / 히트박스는 모두 이 기준으로 적는다.
    //     창 크기(1280x720) 기준으로 적으면 안 된다.
    inline constexpr int kCanvasWidth  = 640;
    inline constexpr int kCanvasHeight = 360;

    // ---- 지면 ----
    //   ★ 플랫포머로 바뀌면서 `y` 의 의미가 **깊이에서 높이로** 바뀌었다.
    //     전에는 플레이어가 260, 적이 270 에 있었다 — 벨트스크롤이라
    //     「깊이가 달랐던」 것이다. 이제 둘은 **같은 바닥**에 서야 한다.
    //
    //   ★ 발판이 여러 개가 되는 것은 6-d 다. 그때까지는 바닥 한 줄이면 된다 —
    //     쓰지 않을 구조(타일맵)를 미리 만들지 않는다.
    //   ★ 280 인 이유: 캐릭터 키가 62 이므로 발끝 280 = 머리 218.
    //     화면 아래 ~66픽셀은 몸 UI·스태미나 바가 쓰므로 거기에 겹치지 않는다.
    inline constexpr float kGroundY = 280.0f;

    // ---- 창 크기 ----
    //   내부 해상도의 정수배여야 픽셀이 지글거리지 않는다.
    inline constexpr int kWindowScale  = 2;
    inline constexpr int kClientWidth  = kCanvasWidth  * kWindowScale;   // 1280
    inline constexpr int kClientHeight = kCanvasHeight * kWindowScale;   // 720

    // ---- 고정 타임스텝 ----
    //   1 틱 = 1/60 초.
    //   앞으로 모든 프레임 데이터(공격 발생, 무적 프레임, 스태미나 회복)의 단위가 된다.
    inline constexpr double kTickSeconds = 1.0 / 60.0;

    //   한 프레임의 경과 시간 상한.
    //   브레이크포인트에 걸렸다 재개할 때 accumulator 가 폭발해서
    //   얼어붙는 것(죽음의 나선)을 막는다.
    inline constexpr double kMaxFrameSeconds = 0.25;
}
