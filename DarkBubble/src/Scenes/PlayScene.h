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
#include "Core/Level.h"

#include <memory>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>
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
    // ★ 지형은 Scene 이 소유한다. BodyComponent 에는 **참조로** 넘긴다 —
    //   `Add<EnemyBrain>(m_playerObj.transform)` 과 같은 모양이다.
    //   SceneContext(엔진 표면적)에 올리는 것은 맵이 여러 장이 되는 6-e 에서.
    Level m_level;
    void BuildLevel();

    // ---- ★ 시차(parallax) 배경 ----
    //   넓은 맵인데 배경이 단색이면 **카메라가 움직이는지 알 수 없다.**
    //   발판만 스쳐 지나가고 세계는 멈춰 있는 것처럼 보인다.
    //   느리게 흐르는 층을 얹으면 그것만으로 「내가 움직이고 있다」가 읽힌다.
    struct BackPillar
    {
        float x, top, width;
        float depth;   // 0 = 아주 멀다(거의 안 움직인다), 1 = 눈앞
    };
    std::vector<BackPillar> m_backdrop;
    void BuildBackdrop();

    // 카메라 추적. Enter 와 Update 가 **같은 것**을 불러야 첫 프레임이 안 튄다.
    void UpdateCamera(SceneContext& ctx);

    // ---- ★ 어둠 (design.md §3.9 A) ----
    //   가운데가 투명하고 바깥이 검은 그림을 **플레이어 위에 한 장 덮는다.**
    //   기획서는 「3패스로 늘린다」고 적어 두었지만 패스는 안 늘어났다 —
    //   알파 블렌딩이 그대로 「보이는 만큼만 보인다」가 되기 때문이다.
    //   ★ 「어떻게 만들까」보다 **「무엇이면 충분한가」**를 먼저 물으면 싸진다.
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_lightMask;
    bool m_dark = true;                  // F5 로 껐다 켠다(임시)
    void DrawDarkness(Renderer& renderer);

    // 손에 든 것을 보여 주는 아이콘 (빈손 / 단검 / 잘림 / 이빨)
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_icons;
    void DrawHandSlots(Renderer& renderer);

    // Render 는 SceneContext 를 못 받으므로 Update 에서 적어 둔다.
    float m_viewX = 0.0f;
    float m_viewY = 0.0f;

    GameObject m_playerObj{ "player" };
    GameObject m_weaponObj{ "weapon" };   // 땅에 떨어진 무기

    // ========================================================================
    //  ★ 적은 여럿이다
    //
    //    ★★ `std::vector<GameObject>` 이면 **안 된다.**
    //      GameObject::Add 가 `component.m_owner = this` 로 **주인의 주소**를
    //      컴포넌트에 심는다. 벡터가 커지며 재배치하는 순간 그 주소가 전부
    //      댕글링이다 — 컴파일도 되고 한동안 돌기까지 한다.
    //      unique_ptr 은 편의가 아니라 **필수**다.
    // ========================================================================
    struct Enemy
    {
        std::unique_ptr<GameObject> obj;

        // 조립할 때 받아 둔다. obj 가 살아 있는 한 유효하다.
        EnemyBrain*     brain = nullptr;
        PartsComponent* parts = nullptr;
        PoiseComponent* poise = nullptr;

        Transform&       Tr()       { return obj->transform; }
        const Transform& Tr() const { return obj->transform; }
    };
    std::vector<Enemy> m_enemies;

    // ★ **이 목록이 곧 앞으로의 맵 파일이다.**
    //   지금은 코드에 적혀 있지만, 7단계에서 `Level` 의 사각형 목록과 함께
    //   map.json 으로 나간다. 그래서 지금부터 **데이터 모양**으로 둔다.
    //   ★ 바라보는 방향도 스폰 데이터다. 등을 보이고 선 적은 **몰래 접근**할
    //     수 있고, 마주 보고 선 적은 정면으로 붙어야 한다 —
    //     배치만으로 §3.9 B 의 시야가 전술이 된다.
    struct EnemySpawn { float x; int facing; };
    void SpawnEnemies(SceneContext& ctx);

    // 가장 가까운 살아 있는 적. 디버그 표시가 쓴다(없으면 nullptr).
    const Enemy* NearestEnemy() const;

    // Start 에서 캐시한다. 매번 dynamic_cast 하지 않기 위해서다.
    //   ★ 스프라이트와 스태미나는 여기 없다. Scene 이 쓸 일이 없기 때문이다 —
    //     컨트롤러가 자기 Start 에서 Require 로 찾아 쓴다.
    //     Scene 이 들고 있으면 「누가 누구를 쓰는가」가 흐려진다.
    //   ※ 적 쪽 포인터는 여기 없다 — Enemy 구조체가 들고 있다.
    PlayerController* m_player      = nullptr;
    PartsComponent*   m_playerParts = nullptr;
    WeaponPickup*     m_pickup      = nullptr;
};
