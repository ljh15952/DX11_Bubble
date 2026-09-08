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
