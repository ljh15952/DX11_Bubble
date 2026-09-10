// ============================================================================
//  PlayScene.h
//    실제로 게임을 플레이하는 화면.
//
//  ---- ★ 이 Scene 이 하는 일은 세 가지뿐이다 ----
//
//      ① 조립      : 어떤 GameObject 에 어떤 컴포넌트를 붙일지 정한다
//      ② 판정      : **두 몸 사이**의 일 — 누가 누구를 때렸는가
//      ③ Scene 전환: 일시정지 · 사망 화면
//
//    상태 머신 · 무브셋 · 스태미나 · 부위 파괴 · AI · 그리기는 전부
//    컴포넌트가 가져갔다. 6-c-1 이전에는 이 파일이 1,700줄이었다.
//
//  ---- 왜 판정만 Scene 에 남는가 ----
//    PlayerController 는 「지금 판정이 켜져 있고 상자는 여기다」까지만 말하고,
//    EnemyBrain 도 똑같이 말한다. 둘 다 상대의 존재를 모른다.
//
//    **양쪽을 다 보고 있는 것은 이 Scene 하나뿐이다.**
//    그래서 「플레이어 → 적」과 「적 → 플레이어」가 여기서 같은 모양으로 나란히 선다.
//    컴포넌트 안에 넣었다면 한쪽이 다른 쪽을 알아야 하고 대칭이 깨진다.
// ============================================================================
#pragma once

#include "Core/GameObject.h"
#include "Core/Scene.h"

class SpriteComponent;
class StaminaComponent;
class PlayerController;
class PartsComponent;
class PoiseComponent;
class WeaponPickup;
class EnemyBrain;

class PlayScene final : public Scene
{
public:
    const char* Name() const override { return "Play"; }

    bool Enter(SceneContext& ctx) override;
    void Update(SceneContext& ctx, bool consumeEdgeInput) override;

    // ★ 위에 있던 Scene 이 닫혔을 때. DeathScene 이 닫혔으면 여기서 부활한다.
    //   PauseScene 이 닫힌 경우와는 **상태로 구분된다**.
    void Resume(SceneContext& ctx) override;

    void Render(Renderer& renderer) override;     // 월드 : 캐릭터 · 히트박스
    void RenderUI(Renderer& renderer) override;   // UI   : 바 · 상태 표시

private:
    // ---- ② 판정 : 두 몸 사이의 일 ----
    void TryPlayerHit(SceneContext& ctx);   // 플레이어 → 적
    void TryEnemyHit(SceneContext& ctx);    // 적 → 플레이어

    // ---- 월드에 떨어진 물건 ----
    //   ★ 컨트롤러는 월드를 모른다. 「떨궈야 한다」는 요청만 하고
    //     어디에 놓을지·주울 수 있는지는 Scene 이 정한다.
    void UpdateWeaponPickup(SceneContext& ctx);

    // ★ 초기화와 부활은 **같은 일**이다. 두 벌로 만들면 반드시 어긋난다 —
    //   나중에 필드를 하나 추가할 때 한쪽만 고치고, 「두 번째 판부터 뭔가
    //   이상하다」는 재현하기 어려운 버그가 된다. Enter 가 이것을 부른다.
    void Respawn(SceneContext& ctx);

    // ---- ① 조립 ----
    //     플레이어 = [Stamina] [PlayerController] [Sprite]
    //     적       = [Parts]   [EnemyBrain]       [Sprite]
    //
    //   ★ 붙인 순서 = 실행 순서다. 그래서 조립 코드가 곧 실행 순서표이고
    //     따로 외울 것이 없다. (그리기만 예외 — Component::RenderDebug 참조)
    GameObject m_playerObj{ "player" };
    GameObject m_enemyObj { "enemy"  };
    GameObject m_weaponObj{ "weapon" };   // 땅에 떨어진 무기

    // Start 에서 캐시한다. 매번 dynamic_cast 하지 않기 위해서다.
    //   ★ 스프라이트와 스태미나는 여기 없다. Scene 이 쓸 일이 없기 때문이다 —
    //     컨트롤러가 자기 Start 에서 Require 로 찾아 쓴다.
    //     Scene 이 들고 있으면 「누가 누구를 쓰는가」가 흐려진다.
    PlayerController* m_player      = nullptr;
    PartsComponent*   m_playerParts = nullptr;
    PartsComponent*   m_enemyParts = nullptr;
    PoiseComponent*   m_enemyPoise = nullptr;
    WeaponPickup*     m_pickup     = nullptr;
    EnemyBrain*       m_enemyBrain = nullptr;
};
