// ============================================================================
//  Game.h
//    시스템(Window / Renderer / Input / SceneManager)을 소유하고 루프를 돌린다.
//
//    ★ Game 은 이제 "게임 내용" 을 전혀 모른다.
//      플레이어도, 적도, 타이틀 메뉴도 모른다. 그건 각 Scene 의 몫이다.
//      화면을 하나 추가할 때 Game 은 한 줄도 바뀌지 않는다.
// ============================================================================
#pragma once

#include <windows.h>

#include "Core/Window.h"
#include "Core/SceneManager.h"
#include "Audio/Audio.h"
#include "Graphics/Assets.h"
#include "Graphics/Camera.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

class Game
{
public:
    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    int  Run();
    void Shutdown();

private:
    // F3 오버레이. Scene 위에 항상 덮어 그린다.
    void DrawStatsOverlay();

    Window       m_window;
    Renderer     m_renderer;
    Input        m_input;
    Assets       m_assets;
    Audio        m_audio;
    Camera       m_camera;
    SceneManager m_scenes;

    // 게임 시작 이후 흐른 틱 수. 로그에 찍혀서 시간 순서를 보여준다.
    unsigned long long m_tickCount = 0;

    // ---- FPS 측정 ----
    //   1 초 동안 몇 프레임을 그렸는지 세어 나눈다.
    //   매 프레임 1/frameTime 을 쓰면 값이 심하게 튀어서 읽을 수가 없다.
    bool   m_showStats  = false;

    // 프레임 정지( , ) + 1틱 전진( . ) + 슬로우 모션( / ).
    // 디버그 도구라 Scene 은 존재를 모른다.
    bool   m_frozen     = false;
    bool   m_slowMotion = false;

    double m_fpsAccum   = 0.0;
    int    m_fpsFrames  = 0;
    double m_fps        = 0.0;
    double m_lastFrameMs = 0.0;

    // ---- TPS (초당 틱) ----
    //   ★ 정상이면 항상 60 이다. 이 숫자가 어긋나면 시간 처리가 깨진 것이다.
    //     프레임 레이트와 달리 하드웨어에 좌우되지 않아야 하는 값이라,
    //     FPS 보다 오히려 이쪽이 중요한 지표다.
    int    m_tpsTicks   = 0;
    double m_tps        = 0.0;
};
