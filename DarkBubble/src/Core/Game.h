// ============================================================================
//  Game.h
//    Window / Renderer / Input 을 묶어 게임 루프를 돌린다.
//    "게임이 무엇인가" 를 아는 유일한 곳.
// ============================================================================
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "Core/Window.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

class Game
{
public:
    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    int  Run();
    void Shutdown();

private:
    // 정확히 1 틱(1/60초) 분량의 게임 로직.
    // 시간 인자가 없는 것에 주의 — 틱 길이가 상수이기 때문이다.
    //
    // consumeEdgeInput:
    //   Update 는 한 프레임에 0~N 회 불릴 수 있다.
    //   「지금 눌려 있다」(이동) 는 여러 번 처리해도 괜찮지만,
    //   「방금 눌렸다」(공격/구르기) 를 매번 처리하면 한 번 눌렀는데 두 번 나간다.
    //   그래서 그런 입력은 첫 번째 틱에서만 소비한다.
    void Update(bool consumeEdgeInput);

    void Render();

    Window   m_window;
    Renderer m_renderer;
    Input    m_input;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_testTexture;

    // ---- 임시 플레이어. 4단계에서 제대로 된 엔티티로 바뀐다 ----
    struct Player
    {
        float x = 100.0f;
        float y = 300.0f;
    };
    Player m_player;
};
