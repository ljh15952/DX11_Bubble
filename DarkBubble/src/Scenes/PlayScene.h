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

    // 구르기. 이동이 목적이 아니라 무적 프레임이 목적이다.
    Roll,

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


// ============================================================================
//  RollData — 구르기 프레임 데이터
//
//    공격과 구조가 똑같다. 이름만 다르다.
//
//      t0        windup           +invincible          +recovery
//      │─ 준비 ─│──── ★무적★ ────│───── 후딜 ──────│
//      │  맞는다 │    안 맞는다     │     맞는다        │
//
//    ★ 앞뒤가 취약한 것이 핵심이다.
//      너무 일찍 굴리면 준비 구간에 맞고, 너무 늦으면 후딜에 맞는다.
//      무적이 처음부터 끝까지 있으면 「구르기 연타 = 무적」이 되어 게임이 무너진다.
//
//      공격의 startup/active/recovery 와 완전히 같은 발상이다.
//      하나를 이해하면 다른 하나가 공짜로 따라온다.
// ============================================================================
struct RollData
{
    int windup     = 4;    // 무적 전 (맞는다)
    int invincible = 12;   // 무적 구간
    int recovery   = 10;   // 후딜 (맞는다)

    float distance    = 80.0f;   // 총 이동 거리 (캔버스 픽셀)
    int   staminaCost = 30;      // 공격(28)보다 살짝 비싸다 = 구르기는 공짜가 아니다

    int TotalTicks() const { return windup + invincible + recovery; }
};


// ============================================================================
//  부위 파괴
//
//    ★ 적은 「HP 하나」가 아니다. 부위마다 HP 를 가진다.
//
//                 ┌──────┐  머리   HP 20   부수면 시야 상실
//                 ├──────┤
//      공격 ────→ │      │  몸통   HP 100  부수면 격파
//                 ├──┬───┤
//                 │  │   │  다리   HP 30 × 2  부수면 이동 불가
//                 └──┴───┘
//
//    기획서의 출발점: 「다리만 베었는데 격파는 비현실적」
// ============================================================================
enum PartIndex
{
    Part_Head = 0,
    Part_Torso,
    Part_LegL,
    Part_LegR,
    Part_Count
};


struct PartDef
{
    const char* name;
    // 발밑 원점 기준 상대 좌표. 적은 좌우 대칭이라 facing 을 고려하지 않는다.
    float left, top, right, bottom;
    int   maxHp;
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

    // 지금 무적인가 (구르기의 invincible 구간인가)
    //
    // ★ hurtbox 를 빈 사각형으로 만드는 방법도 있지만, 그러면 F1 에서
    //   「지금 무적인가」를 눈으로 볼 수 없다. hurtbox 는 그대로 두고
    //   판정하는 쪽에서 이것을 확인하면, 색만 바꿔서 표시할 수 있다.
    bool Invincible() const;

    // 구르기 이동. Roll 상태에서만 불린다.
    void UpdateRoll();

    // ---- 적 ----
    AABB EnemyPartBox(int part) const;

    // 공격 히트박스와 가장 크게 겹치는 부위를 고른다. -1 = 안 맞음.
    //
    // ★ 「겹친 부위 전부」로 하면 한 번 휘두를 때 온몸이 깎여 부위 파괴가
    //   무의미해진다. 「가장 위 부위」로 하면 항상 머리만 맞아 다리를 못 벤다.
    //   겹침 면적이 가장 큰 곳을 고르면 「낮게 휘두르면 다리」가 되어
    //   플레이어가 조준하게 된다.
    int PickHitPart(const AABB& attack) const;

    void DrawEnemyDebug(Renderer& renderer) const;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sheet;

    // ---- 임시 플레이어. 적이 등장하면 Gameplay/Character 로 뺀다 ----
    //   좌표는 캔버스(640x360) 기준이고, x / y 는 "발밑 가운데" 다.
    struct Player
    {
        float x      = 120.0f;
        float y      = 260.0f;
        int   facing = 1;     // +1 = 오른쪽, -1 = 왼쪽
        int   flash  = 0;     // 남은 번쩍임 틱 (피격 표현용)

        // ★ 구르기 시작 시점에 고정되는 방향.
        //   중간에 방향키를 바꿔도 무시된다 — 「한 번 구르면 끝까지 간다」.
        float rollDirX = 1.0f;
        float rollDirY = 0.0f;
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

    // ---- 적 (아직 움직이지 않는다. 5-e-2 에서 상태 머신이 들어온다) ----
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_enemySheet;
    AnimationPlayer m_enemyAnim;

    struct Enemy
    {
        float x = 470.0f;   // 발밑 가운데 (캔버스 좌표)
        float y = 270.0f;
        int   hp[Part_Count]{};
        int   flash = 0;    // 피격 번쩍임 남은 틱
        bool  dead  = false;
    };
    Enemy m_enemy;

    // 몸이 적에 닿아 있는가 (무적 프레임 확인용)
    bool m_touching = false;

    // ★ 한 번 휘두를 때 한 번만 맞게 하는 장치.
    //   active 가 3틱이면 판정이 3틱 동안 존재하므로,
    //   이 표시가 없으면 한 번 휘둘렀는데 데미지가 3 번 들어간다.
    //   적이 여러 마리가 되면 이것이 "이미 맞춘 대상 목록" 이 된다.
    bool m_hitThisSwing = false;

    // F1 로 켜고 끄는 히트박스 표시.
    bool m_showDebug = false;

    // 발소리 간격 카운터 (틱 단위)
    int m_stepCooldown = 0;
};
