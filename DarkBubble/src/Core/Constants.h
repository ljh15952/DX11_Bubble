// ============================================================================
//  Constants.h
//    여러 파일이 공유하는 설정값. 여기 말고 다른 곳에 흩어놓지 않는다.
// ============================================================================
#pragma once

namespace Config
{
    // ---- 클라이언트 영역(실제로 그림이 그려지는 부분)의 크기 ----
    inline constexpr int kClientWidth  = 1280;
    inline constexpr int kClientHeight = 720;

    // ---- 고정 타임스텝 ----
    //   1 틱 = 1/60 초.
    //   앞으로 모든 프레임 데이터(공격 발생, 무적 프레임, 스태미나 회복)의 단위가 된다.
    inline constexpr double kTickSeconds = 1.0 / 60.0;

    //   한 프레임의 경과 시간 상한.
    //   브레이크포인트에 걸렸다 재개할 때 accumulator 가 폭발해서
    //   얼어붙는 것(죽음의 나선)을 막는다.
    inline constexpr double kMaxFrameSeconds = 0.25;
}
