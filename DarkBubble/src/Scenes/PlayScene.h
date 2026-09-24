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
#include "Gameplay/EnemyType.h"
#include "Gameplay/MapData.h"

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
    void TryEnemyHit(SceneContext& ctx);

    // ---- ★ 튕김 (§3.12) ----
    //   휘두르는 상자가 **벽**에 걸리면 친 쪽이 튕긴다. 플레이어도 적도.
    //   ★ Scene 이 하는 이유는 판정과 같다 — 지형과 공격자를 **둘 다 아는 쪽**이
    //     둘 사이의 일을 한다. 컨트롤러는 지형을 모른다(몸이 안다).
    void TryDeflect(SceneContext& ctx);

    // 이 상자가 **벽**과 겹치는가. ★ 「발밑보다 위로 솟은 것」만 벽이다 —
    //   딛고 선 바닥까지 세면 내려찍기가 착지하는 순간마다 튕긴다.
    bool HitsWall(const AABB& box, float feetY) const;    // 적 → 플레이어

    // ---- 월드에 떨어진 물건 ----
    //   ★ 컨트롤러는 월드를 모른다. 「떨궈야 한다」는 요청만 하고
    //     어디에 놓을지·주울 수 있는지는 Scene 이 정한다.
    //   ★ 줍기가 여기서 빠졌다 — E 로 옮겨 `Interact()` 가 처리한다.
    //     이름을 바꿔 옛 호출부를 드러냈다(§9.1).
    void UpdateWeaponDrop(SceneContext& ctx);

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

    // ---- ★ 맵 (7-c) ----
    //   지형·적 배치·포탈이 전부 여기서 온다. `BuildLevel` 이 하던 일은
    //   **파일이 하게 되었다** — 이미 데이터 모양이라 형태가 안 바뀌었다.
    MapData     m_map;
    std::string m_mapName;

    // 맵을 통째로 갈아 끼운다. entry = 그 맵의 어느 입구로 들어가는가.
    bool LoadMap(SceneContext& ctx, const std::string& name, const std::string& entry);

    // ---- ★ 상호작용 (E) ----
    //   발밑에 있는 것을 찾아 두고, E 를 누르면 그것이 하는 일을 한다.
    //   ★ **버튼은 하나다.** 무엇을 하는지는 그 자리에 무엇이 있는지가 정한다 —
    //     좌/우클릭이 손을 가리키는 것과 같은 구조다(§3.2.1.1).
    //     나중에 상자·사람이 오면 **종류만** 늘어난다.
    //   ★★ 가리키는 것이 **맵의 것만이 아니다.** 땅에 떨어진 무기도 E 로
    //     줍는다. 그래서 초점은 `MapInteract*` 가 아니라 **「상자 + 글자 +
    //     종류」**다 — 그려 주는 쪽은 그것이 문인지 무기인지 알 필요가 없다.
    struct Drop;   // 아래에 있다. 여기서는 가리키기만 한다
    enum class FocusKind { None, Weapon, Map };

    struct Focus
    {
        FocusKind          kind   = FocusKind::None;
        AABB               box{};              // 안내를 띄울 자리
        const char*        prompt = "";
        const MapInteract* map    = nullptr;   // Map 일 때만
        Drop*              drop   = nullptr;   // Weapon 일 때만
    };
    Focus m_focus;

    void UpdateFocus();
    void Interact(SceneContext& ctx);

    // ★ 맵 전환은 **즉시 하지 않는다.** 판정 도중에 적 목록과 지형을 갈아
    //   끼우면 순회 중인 것이 사라진다. 요청만 적어 두고 틱 끝에서 처리한다 —
    //   SceneManager 가 전환을 미루는 것과 같은 이유다.
    bool        m_portalPending = false;
    std::string m_portalTo, m_portalEntry;

    // ---- ★ 세이브 포인트 ----
    //   부활은 **맵의 시작**이 아니라 **마지막으로 쉰 자리**다.
    //   맵이 다를 수도 있으므로 맵 이름까지 같이 기억한다.
    //   ★ 초기값을 **여기에 안 적는다.** 첫 맵 이름과 시작 좌표는 이미
    //     Enter 와 field.json 에 있다 — 여기에 또 적으면 맵을 옮길 때
    //     이 줄만 옛 값으로 남는다(늘 밟던 「같은 값이 두 곳에」다).
    //   ★ **높이도 같이** 기억한다. 발판 위 화톳불에서 쉬고 죽었는데 아래
    //     지면에서 일어나면, 쉰 자리로 돌아온 것이 아니다.
    std::string m_saveMap;
    float       m_saveX = 0.0f;
    float       m_saveY = 0.0f;

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

    // ========================================================================
    //  ★ 땅에 떨어진 물건도 **여럿이다** (8-b)
    //
    //    손이 둘이 되면 두 자루를 들 수 있고, 그러면 두 자루가 떨어질 수 있다.
    //    하나짜리(`m_weaponObj`)로 두면 **한 자루가 조용히 사라진다.**
    //
    //    ★ 적이 여럿이 될 때와 **완전히 같은 이동**이다(7-a): `unique_ptr` 담은
    //      `vector` + 조립할 때 받아 둔 포인터. `vector<GameObject>` 이면 안 되는
    //      이유도 같다 — 재배치할 때 `component.m_owner` 가 옛 주소를 가리킨다.
    //
    //    ★★ 「있다/없다」가 **목록에 있는가**가 되면서 `m_active` 플래그가
    //      사라졌다. 하나뿐일 때는 숨겼다 보였다 해야 했지만, 여럿이면
    //      **지우면 된다.** 상태가 하나 줄었다.
    // ========================================================================
    struct Drop
    {
        std::unique_ptr<GameObject> obj;
        WeaponPickup*               pickup = nullptr;

        // ★ **어느 맵에 떨어져 있는가.** 없으면 동굴에 떨군 무기가 들판의
        //   같은 좌표에 나타난다 — 맵이 하나일 때만 안 틀렸던 것이다.
        std::string                 map;
    };
    std::vector<Drop> m_drops;

    void DropItem(SceneContext& ctx, const std::string& weaponId, float x, float y);

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

        // 적 종류의 **이름**(enemies.json 의 키). ★ 특수 효과의 대상(`vs`)이
        //   이 이름을 가리킨다 — 「해골에게 추가 데미지」가 여기서 성립한다.
        std::string type;

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
    //
    // ★ 적 종류 카탈로그. **Scene 이 하나만** 들고 적들이 참조로 본다 —
    //   무기는 플레이어 하나가 쓰지만 적 데이터는 여럿이 공유하기 때문이다.
    EnemyCatalog m_enemyTypes;

    // 적을 다시 세울 때 필요하므로 들고 있는다.
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_enemySheet;

    // F6 — 무기와 적 데이터를 다시 읽고 **적을 다시 세운다.**
    //   부위 HP 는 생성 시점에 정해지므로 다시 세우지 않으면 반영이 안 된다.
    void ReloadData(SceneContext& ctx);
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

    // 사망 화면을 띄웠는가. ★ Resume 이 부활 여부를 **이것으로** 정한다 —
    //   `IsDead()` 로 정하면 장비 화면이 닫힐 때도 부활한다(Resume 주석).
    bool m_deathScreenShown = false;
};
