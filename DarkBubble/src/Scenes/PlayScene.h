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

    // ★ 스태미나가 0 미만으로 내려간 뒤의 경직.
    //   아무 입력도 받지 않는다. 회복될 때까지 완전히 무방비다.
    //
    //   「스태미나가 부족하면 행동이 안 나간다」로 만들 수도 있지만
    //   그러면 긴장이 없다. 「부족해도 나가고, 그 결과 무방비가 된다」가
    //   욕심에 벌을 주는 구조를 만든다.
    Exhausted,
};


// ============================================================================
//  AttackData — 프레임 데이터
//
//    공격 하나를 세 구간으로 나눈다. 단위는 전부 틱(1/60초).
//
//      t0                 startup   +active                    +recovery
//      │──── startup ────│ active  │──────── recovery ────────│
//      │   판정 없음      │ ★판정★  │        판정 없음           │
//
//    ★ 이 세 숫자가 무기의 성격 전부다.
//
//        │ startup │ active │ recovery │  총    │
//      단검 │    8   │   3    │    13    │  24틱  │  빠르고 안전
//      대검 │   22   │   6    │    34    │  62틱  │  느리고 위험
//
//    같은 Attack 상태를 쓰는데 완전히 다른 무기가 된다.
//    6단계에서 이 구조체가 그대로 weapons.json 이 된다.
// ============================================================================
struct AttackData
{
    // ---- 프레임 데이터 (틱) ----
    int startup  = 8;    // 판정이 나오기까지
    int active   = 3;    // 판정이 존재하는 구간
    int recovery = 13;   // 판정 끝 ~ 다시 움직일 수 있기까지

    // ---- 히트박스 (발밑 원점 기준. facing 으로 좌우 반전된다) ----
    float reach          = 12.0f;   // 몸 중심에서 히트박스 안쪽 끝까지
    float width          = 28.0f;   // 히트박스 폭
    float height         = 24.0f;   // 히트박스 높이
    float heightFromFoot = 34.0f;   // 발끝에서 히트박스 중심까지

    int damage      = 12;
    int staminaCost = 28;   // 100 짜리 스태미나로 3 번은 되고 4 번째에 고갈된다

    int TotalTicks() const { return startup + active + recovery; }
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

    // 스태미나 회복. 상태와 무관하게 매 틱 불린다.
    void UpdateStamina();

    // 스태미나 바. UI 레이어에 그린다 — 화면이 흔들려도 제자리에 있어야 한다.
    void DrawStaminaBar(Renderer& renderer) const;

    AABB SpriteBounds() const;

    // ★ 용어를 나눠 쓴다. 섞으면 "내 공격이 나를 때리는" 코드를 쓰게 된다.
    //   hurtbox = 내가 맞는 범위 (몸)
    //   hitbox  = 내가 때리는 범위 (무기)
    AABB PlayerHurtbox() const;
    AABB AttackHitbox()  const;

    // 지금 공격 판정이 존재하는가 (active 구간인가)
    bool AttackActive() const;

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

    // ---- 스태미나 ----
    //   ★ float 이다. int 로 하면 틱당 0.9 가 0 으로 잘려 영원히 회복되지 않는다.
    //     이동 좌표와 같은 이유다 — 계산은 소수로, 표시할 때만 정리.
    struct Stamina
    {
        float current = 100.0f;
        int   delay   = 0;    // 회복이 시작되기까지 남은 틱
    };
    Stamina m_stamina;

    // 화면에 고정된 장애물. 몸이 닿으면 색이 바뀌고, 공격이 맞으면 번쩍인다.
    AABB m_obstacle{ 280.0f, 200.0f, 340.0f, 258.0f };
    bool m_touching       = false;
    int  m_obstacleFlash  = 0;    // 타격 표현용 남은 틱

    // ★ 한 번 휘두를 때 한 번만 맞게 하는 장치.
    //   active 가 3틱이면 판정이 3틱 동안 존재하므로,
    //   이 표시가 없으면 한 번 휘둘렀는데 데미지가 3 번 들어간다.
    //   적이 생기면 이것이 "이번 공격에서 이미 맞춘 대상 목록" 이 된다.
    bool m_hitObstacleThisSwing = false;

    // F1 로 켜고 끄는 히트박스 표시.
    bool m_showDebug = false;

    // 발소리 간격 카운터 (틱 단위)
    int m_stepCooldown = 0;
};
