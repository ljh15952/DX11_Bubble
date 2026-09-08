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
#include "Graphics/Renderer.h"
#include "Input/Input.h"

class Game
{
public:
    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    int  Run();
    void Shutdown();

private:
    Window       m_window;
    Renderer     m_renderer;
    Input        m_input;
    SceneManager m_scenes;

    // 게임 시작 이후 흐른 틱 수. 로그에 찍혀서 시간 순서를 보여준다.
    unsigned long long m_tickCount = 0;
};
