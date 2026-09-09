// ============================================================================
//  PlayScene.h
//    실제로 게임을 플레이하는 화면.
// ============================================================================
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "Core/Scene.h"
#include "Core/AABB.h"
#include "Graphics/Animation.h"


// ============================================================================
//  PlayerState
//    어느 순간에도 캐릭터는 정확히 하나의 상태에 있다.
//
//        ┌──────┐  이동 입력   ┌──────┐
//        │ Idle │ ──────────→ │ Run  │
//        │      │ ←────────── │      │
//        └──────┘  입력 없음   └──────┘
//            │                    │
//            └──── 공격 입력 ──────┘
//                     ↓
//              ┌─────────────┐
//              │   Attack    │  모션이 끝나면 Idle/Run 으로
//              └─────────────┘
//
//    ★ 상태는 늘어나지 않는다.
//      Idle / Run / Attack / Roll / Hurt / Dead 여섯이면 끝이다.
//      「무기마다 다른 액션」의 다양성은 상태가 아니라 데이터(프레임 데이터)에서 나온다.
//      단검도 대검도 전부 Attack 상태를 쓰고, 발생·지속·후딜 숫자만 다르다.
//      그래서 enum + switch 로 충분하고, 상태마다 클래스를 만들 필요가 없다.
// ============================================================================
enum class PlayerState
{
    Idle,
    Run,
    Attack,
};


class PlayScene final : public Scene
{
public:
    const char* Name() const override { return "Play"; }

    bool Enter(SceneContext& ctx) override;
    void Update(SceneContext& ctx, bool consumeEdgeInput) override;
    void Render(Renderer& renderer) override;     // 월드 : 캐릭터 · 장애물 · 히트박스
    void RenderUI(Renderer& renderer) override;   // UI   : 안내 문구

private:
    // 상태를 바꾼다. 같은 상태로의 전이는 무시한다.
    // ★ 여기가 "Enter" 다 — 애니메이션 시작, 소리, 흔들림처럼
    //   들어가는 순간 한 번만 해야 하는 일을 여기서 한다.
    //   매 틱 하면 애니메이션이 프레임 0 에서 멈춘다.
    void ChangeState(SceneContext& ctx, PlayerState next);

    // 이동 처리. Idle / Run 상태에서만 불린다.
    void UpdateMovement(SceneContext& ctx, float moveX, float moveY);

    AABB SpriteBounds() const;
    AABB PlayerHitbox() const;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sheet;

    // ---- 임시 플레이어. 적이 등장하면 Gameplay/Character 로 뺀다 ----
    //   좌표는 캔버스(640x360) 기준이고, x / y 는 "발밑 가운데" 다.
    struct Player
    {
        float x      = 120.0f;
        float y      = 260.0f;
        int   facing = 1;     // +1 = 오른쪽, -1 = 왼쪽
        int   flash  = 0;     // 남은 번쩍임 틱 (피격 표현용)
    };
    Player          m_player;
    AnimationPlayer m_playerAnim;

    // ---- 상태 머신 ----
    PlayerState m_state = PlayerState::Idle;

    // ★ 이 상태에 들어온 뒤 흐른 틱.
    //   이 카운터 하나가 프레임 데이터를 가능하게 한다.
    //     틱 0~7  발생   히트박스 없음
    //     틱 8~10 지속   히트박스 있음
    //     틱 11~  후딜   히트박스 없음
    //   5-b 에서 이 값으로 공격 판정을 켜고 끈다.
    int m_stateTicks = 0;

    // 화면에 고정된 장애물. 겹치면 색이 바뀐다. (캔버스 좌표)
    AABB m_obstacle{ 280.0f, 160.0f, 360.0f, 210.0f };
    bool m_touching = false;

    // F1 로 켜고 끄는 히트박스 표시.
    bool m_showDebug = false;

    // 발소리 간격 카운터 (틱 단위)
    int m_stepCooldown = 0;
};
