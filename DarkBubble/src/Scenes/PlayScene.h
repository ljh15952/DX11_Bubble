// ============================================================================
//  PlayScene.h
//    실제로 게임을 플레이하는 화면.
//    이전에 Game 안에 있던 내용이 전부 여기로 옮겨왔다.
//    Game 은 이제 루프만 돌린다.
// ============================================================================
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "Core/Scene.h"
#include "Core/AABB.h"
#include "Graphics/Animation.h"

class PlayScene final : public Scene
{
public:
    const char* Name() const override { return "Play"; }

    bool Enter(SceneContext& ctx) override;
    void Update(SceneContext& ctx, bool consumeEdgeInput) override;
    void Render(Renderer& renderer) override;

private:
    // 스프라이트가 차지하는 화면 영역 (64×64 전체)
    AABB SpriteBounds() const;

    // ★ 실제 충돌 판정에 쓰는 영역. 스프라이트보다 작다.
    //   스프라이트 전체를 쓰면 투명한 여백에 스쳐도 "맞았다" 가 되어
    //   플레이어가 "안 맞았는데?!" 라고 느낀다.
    AABB PlayerHitbox() const;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sheet;

    // ---- 임시 플레이어. 나중에 제대로 된 엔티티로 바뀐다 ----
    //   좌표는 캔버스(640x360) 기준이고,
    //   ★ x / y 는 스프라이트의 좌상단이 아니라 "발밑 가운데" 다.
    struct Player
    {
        float x      = 120.0f;
        float y      = 260.0f;
        int   facing = 1;     // +1 = 오른쪽, -1 = 왼쪽
        int   flash  = 0;     // 남은 번쩍임 틱
    };
    Player          m_player;
    AnimationPlayer m_playerAnim;

    // 화면에 고정된 장애물. 겹치면 색이 바뀐다. (캔버스 좌표)
    AABB m_obstacle{ 280.0f, 160.0f, 360.0f, 210.0f };
    bool m_touching = false;

    // F1 로 켜고 끄는 히트박스 표시.
    // 보이지 않는 것은 디버깅할 수 없다.
    bool m_showDebug = false;

    // 발소리 간격 카운터 (틱 단위)
    int m_stepCooldown = 0;
};
