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
#include "Core/AABB.h"
#include "Graphics/Renderer.h"
#include "Graphics/Animation.h"
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

    // 스프라이트가 차지하는 화면 영역 (64×64 전체)
    AABB SpriteBounds() const;

    // ★ 실제 충돌 판정에 쓰는 영역. 스프라이트보다 작다.
    //   스프라이트 전체를 쓰면 투명한 여백에 스쳐도 "맞았다" 가 되어
    //   플레이어가 "안 맞았는데?!" 라고 느낀다.
    AABB PlayerHitbox() const;

    Window   m_window;
    Renderer m_renderer;
    Input    m_input;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sheet;

    // ---- 임시 플레이어. 나중에 제대로 된 엔티티로 바뀐다 ----
    //   좌표는 내부 해상도(640x360) 기준.
    struct Player
    {
        float x =  50.0f;
        float y = 150.0f;
    };
    Player          m_player;
    AnimationPlayer m_playerAnim;

    // 화면에 고정된 장애물. 겹치면 색이 바뀐다. (캔버스 좌표)
    AABB m_obstacle{ 280.0f, 160.0f, 360.0f, 210.0f };
    bool m_touching = false;

    // F1 로 켜고 끄는 히트박스 표시.
    // 보이지 않는 것은 디버깅할 수 없다.
    bool m_showDebug = false;

    // 게임 시작 이후 흐른 틱 수. 로그에 찍혀서 시간 순서를 보여준다.
    unsigned long long m_tickCount = 0;
};
