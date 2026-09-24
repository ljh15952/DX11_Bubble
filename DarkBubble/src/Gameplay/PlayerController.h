// ============================================================================
//  PlayerController.h
//    플레이어의 상태 머신 · 입력 · 무브셋 · 피격. 게임 고유 컴포넌트다.
//
//    PlayScene 에 1,300줄로 남아 있던 것을 여기로 옮겼다.
//    Scene 은 이제 **조립과 두 몸 사이의 판정**만 한다.
//
//  ---- ★ 판정은 여기서 하지 않는다 (EnemyBrain 과 같은 규칙) ----
//    「지금 공격 판정이 켜져 있고 상자는 여기다」까지만 말한다.
//    실제로 적을 때렸는지는 **PlayScene 이** 검사한다.
//    그래야 플레이어가 적의 존재를 몰라도 되고,
//    「플레이어 → 적」과 「적 → 플레이어」가 같은 모양이 된다.
// ============================================================================
#pragma once

#include <array>
#include <string>
#include <vector>

#include "Core/AABB.h"
#include "Core/Component.h"
#include "Gameplay/AttackData.h"
#include "Gameplay/WeaponType.h"

class BodyComponent;
class PartsComponent;
class PoiseComponent;


// ============================================================================
//  WeaponHand — 무기를 어느 손에 들고 있는가 (design.md §3.10 의 슬롯 구조)
//
//      오른손  무기        ← 기본. 잘리면 떨군다
//      왼손    보조        ← 횃불 / 방패 / 없음
//
//    ★ 오른팔이 잘린 뒤 무기를 주우면 **왼손**에 든다. 그런데 왼손은 보조
//      슬롯이므로, 그때부터 횃불을 들 수 없다 —
//      §3.9 A(어둠)가 들어오면 「무기를 되찾은 대가로 어둠 속에서 싸운다」가 된다.
//      §1.2 의 기회비용이 슬롯 구조만으로 또 성립한다. 새 규칙이 필요 없다.
// ============================================================================
enum class WeaponHand { None, Right, Left };

// 맞은 결과. ★ Scene 이 **공격한 쪽**에게 무엇을 해 줄지가 여기서 갈린다 —
//   튕겨 냈으면 치던 적이 튕겨야 하는데, 컨트롤러는 적을 모른다.
enum class HitResult { Hit, Blocked, Deflected };

// 손 -> 슬롯 번호. ★ enum 값에 기대는 유일한 곳으로 **모아 둔다** —
//   흩어 놓으면 enum 에 값을 하나 더할 때 조용히 틀린다.
constexpr int HandSlot(WeaponHand h) { return static_cast<int>(h) - 1; }

// 반대 손. ★ `h == Right ? Left : Right` 를 네 군데에 흩어 놓았더니 그중
//   하나에서 방향을 뒤집어 적어 놓고도 컴파일이 됐다 — 같은 타입이라
//   컴파일러가 못 잡는다(handoff §9.1). 한 곳으로 모은다.
constexpr WeaponHand OtherHand(WeaponHand h)
{
    return (h == WeaponHand::Right) ? WeaponHand::Left
         : (h == WeaponHand::Left)  ? WeaponHand::Right
         :                            WeaponHand::None;
}
class SpriteComponent;
class StaminaComponent;


// ============================================================================
//  PlayerState
//    ★ 상태는 늘어나지 않는다. 다양성은 **데이터**(프레임 데이터)에서 나온다.
//      단검도 대검도 전부 Attack 을 쓰고 발생·지속·후딜 숫자만 다르다.
//      공격이 4종이어도 Attack 은 하나다.
//
//    ※ 원칙의 진짜 내용은 「조건이 여러 곳에 흩어지면 상태로 승격한다」이므로,
//      플랫포머 전환에서 Jump 가 추가되어 **8개**가 되었다(design.md §3.8.1).
//      공중은 그 기준을 넘는다 — 이동 제어가 다르고, 구르기가 안 되고,
//      착지 판정이 필요하고, 공격이 다르다. 넷을 `if (!grounded)` 로
//      흩뿌리면 상태 머신을 만든 이유가 사라진다.
// ============================================================================
enum class PlayerState
{
    Idle,
    Run,
    Jump,        // 공중. **올라가는 중과 떨어지는 중을 나누지 않는다** — 규칙이 같다
    Attack,
    Roll,        // 이동이 목적이 아니라 **무적 프레임**이 목적이다
    Exhausted,   // 스태미나 고갈 경직. 부족해도 행동은 나가고 대가를 뒤에 치른다
    Hurt,        // 피격 경직. 들어갈지 말지는 **강인도(poise)가 정한다**
    Dead,
};

const char* PlayerStateName(PlayerState s);


// ============================================================================
//  RollData — 구르기 프레임 데이터
//
//      t0        windup           +invincible          +recovery
//      │─ 준비 ─│──── ★무적★ ────│───── 후딜 ──────│
//      │  맞는다 │    안 맞는다     │     맞는다        │
//
//    ★ 앞뒤가 취약한 것이 핵심이다. 무적이 처음부터 끝까지 있으면
//      「구르기 연타 = 무적」이 되어 게임이 무너진다.
//      공격의 startup/active/recovery 와 완전히 같은 발상이다.
// ============================================================================
struct RollData
{
    int windup     = 4;    // 무적 전 (맞는다)
    int invincible = 12;   // 무적 구간
    int recovery   = 10;   // 후딜 (맞는다)

    float distance    = 80.0f;
    int   staminaCost = 30;   // 공격(28)보다 살짝 비싸다 = 구르기는 공짜가 아니다

    int TotalTicks() const { return windup + invincible + recovery; }
};


// ============================================================================
//  HurtData — 피격 경직
//
//    ★ invuln > ticks 여야 한다.
//      같게 두면 **경직이 풀리는 그 틱에 다시 맞는다.** 적이 두 마리면
//      영원히 못 움직인다 — 스턴락이다. 버그처럼 보이지 않아 숫자로 못 박아 둔다.
// ============================================================================
struct HurtData
{
    int   ticks     = 18;
    int   invuln    = 24;    // ★ 반드시 ticks 보다 길게
    float knockback = 22.0f; // 넉백은 타격감 때문만이 아니다 —
                             // 밀려나면 적의 다음 공격 사거리에서 벗어난다
};


// ============================================================================
//  ArmorData — 강인도(poise)
//
//        poise >= impact  →  데미지만 받고 자리에서 버틴다 (경직 없음)
//        poise <  impact  →  Hurt(경직) + 넉백 + 피격 무적
//
//    ★ 버티기는 안전한 것이 아니라 **다른 지불 방식**이다.
//      구르기는 스태미나로, 버티기는 HP 로 낸다.
// ============================================================================
struct ArmorData
{
    const char* name  = "CLOTH";
    int         poise = 10;
};


class PlayerController final : public Component
{
public:
    const char* TypeName() const override { return "PlayerController"; }

    void Start(SceneContext& ctx) override;
    void Tick(SceneContext& ctx, bool consumeEdgeInput) override;
    void Render(Renderer& renderer) override;        // 틴트·눌림을 스프라이트에 넘긴다
    void RenderDebug(Renderer& renderer) override;   // hurtbox · 공격 상자 · 원점
    void RenderUI(Renderer& renderer) override;      // 상태 표시 · HP 바 · 방어구

    // ---- 조회 (PlayScene 의 판정이 쓴다) ----
    PlayerState State()      const { return m_state; }
    int         StateTicks() const { return m_stateTicks; }
    bool        IsDead()     const { return m_state == PlayerState::Dead; }

    // ※ Hurtbox() 는 지웠다. 「내가 맞는 범위」는 PartsComponent 의 부위 상자이고,
    //   그것과 **다른 숫자**를 하나 더 들고 있으면 언젠가 서로 어긋난다.
    //   실제로 자세가 바뀌어도 안 변해서 「엎드렸는데 상자는 서 있는」 표시가 났다.
    AABB AttackHitbox() const;                // 내가 때리는 범위 (무기)
    bool AttackActive() const;                // 지금 판정이 존재하는가
    const AttackData& CurrentAttack() const;

    bool HitThisSwing() const { return m_hitThisSwing; }
    void MarkHitThisSwing()   { m_hitThisSwing = true; }

    // ★ 무적은 두 종류다. 같은 함수로 판정되지만 성격이 다르다.
    //     ① 구르기 무적 — 플레이어가 **벌어낸** 것 (스태미나 + 타이밍)
    //     ② 피격 무적   — 게임이 **주는** 안전장치 (스턴락 방지)
    //   목적이 다르므로 길이를 따로 두고 F1 표시 색도 나눈다.
    //   ★ ②는 **경직의 부속품**이다. 강인도로 버텨내면 못 움직이는 구간이
    //     없으므로 스턴락 위험도 없고, 그래서 피격 무적도 주지 않는다.
    bool RollInvincible() const;
    bool Invincible()     const;

    // 사망 화면을 띄울 때가 되었는가. 한 번만 true 를 돌려준다.
    // ★ Scene 전환은 Scene 의 일이라 여기서 Push 하지 않는다 —
    //   Gameplay 가 Scenes 를 알기 시작하면 층이 뒤엉킨다.
    bool ConsumeDeathScreenRequest();

    // ---- 변경 ----
    // fromX / fromY = **공격자의 위치.** 넉백 방향이 여기서 나온다.
    //   ★ 컨트롤러가 「누가 때렸는지」를 알 필요는 없다. 좌표 하나면 충분하다 —
    //     그래서 적이 몇 종이 되든, 나중에 함정이나 투사체가 생겨도 그대로다.
    //   part = 맞은 부위. **누가 어디를 때렸는지는 Scene 이 정한다** —
    //   두 몸 사이의 계산이기 때문이다(TryPlayerHit 와 대칭).
    //   ★★ 공격 **상자**까지 받는다. 「막았는가」는 상자끼리 겹치는지로
    //     정해지고, 그 판단은 **맞는 쪽**이 해야 한다 — Scene 이 대신 판단해
    //     결과만 넘기면 「막았는데 왜 경직이지?」 같은 어긋남이 둘로 갈린다.
    HitResult TakeHit(SceneContext& ctx, const AttackData& atk, const AABB& atkBox,
                      int part, float fromX, float fromY);

    // ★ 휘두르던 것이 튕겼다(§3.12) — 벽을 쳤다.
    //   휘두르기가 거기서 끊기고, 짧게 굳으며 **친 방향의 반대로** 밀린다.
    void Deflect(SceneContext& ctx);
    void Respawn(SceneContext& ctx);

private:
    // 부위 하나가 부서졌을 때 **몸에 일어나는 일**(무기를 떨군다 등).
    //   ★ 피격과 디버그 키가 **같은 길**을 지나야 한다. 한쪽만 부위 HP 를
    //     0 으로 만들면 「잘렸는데 무기는 들려 있는」 몸이 생긴다.
    void OnPartBroken(int part);

    // 막았다 — 스태미나를 치르고 소리를 낸다.
    //   ★ 데미지를 **계산하지 않는다.** 줄이는 비율이 데미지와 impact 를
    //     같이 정하므로, 그 계산은 둘을 다 아는 TakeHit 한 곳에 둔다.
    void PayGuard(SceneContext& ctx, const AttackData& atk, int damage);

    // 그 손에 든 것이 방패인가.
    bool IsShieldHand(WeaponHand hand) const;

    // 그 손을 비운다. 비운 것은 **어디로** 가는가:
    //   ★ 8-b 에서는 「팔이 잘렸다」와 「다른 것을 들었다」가 같은 일이라
    //     하나(Displace)로 합쳤다. 가방이 생기면서 **둘이 갈라졌다** —
    //     잘리면 땅에 떨어지고, 바꿔 들면 가방에 들어간다.
    //     같은 일이던 것이 새 자리가 생기면 다른 일이 된다.
    enum class Release { Ground, Bag };
    void ReleaseHand(WeaponHand hand, Release to);

public:

    // ★ 맵을 옮기면 **부활 지점도 따라간다.** 안 그러면 동굴에서 죽었는데
    //   들판에서 되살아난다 — EnemyBrain 이 「자기 집」을 기억하게 만든 것과
    //   같은 문제이고, 같은 해법이다.
    //   ★★ **높이도 같이 받는다.** 전에는 x 만 받고 바닥은 맵의 지면으로
    //     정했는데, 발판 위 화톳불에서 쉬고 죽으면 **아래 지면에서** 일어났다.
    //     부활 지점이 하나(맵 시작)일 때는 언제나 지면이라 맞았을 뿐이다 —
    //     「하나뿐이라 괜찮았던 것이 둘이 되는 순간 터진다」를 또 밟았다.
    //     ★ 인자를 늘려 옛 호출부가 전부 컴파일 에러가 되게 했다(§9.1).
    void SetHome(float x, float y) { m_homeX = x; m_homeY = y; }

    // 지금 위치만 옮긴다(HP·무기·부위는 그대로). 맵 이동이 쓴다.
    void PlaceAt(float x);

    // ★ 세이브 포인트에서 쉰다 — 몸을 되돌린다. **위치는 안 건드린다.**
    //   무기와 방어구는 그대로다(design.md §3.6.1 의 「남는다」).
    void Rest();

    const ArmorData& Armor() const;

    // ★ 부위 상실이 행동을 막는다. 조건을 흩뿌리지 않고 이름을 붙여 모은다.
    // ★ `CanAttack()` 을 지웠다. 「공격할 수 있는가」는 이제 **질문이 아니다** —
    //   손에 무기가 있으면 휘두르고 없으면 문다. 못 하는 경우가 없다.
    //   대신 **손마다** 묻는다.
    bool HandArmed(WeaponHand hand) const;   // 그 손에 무기가 들려 있는가
    bool CanHold(WeaponHand hand)   const;   // 그 손으로 주울 수 있는가

    //   ★ **무엇을 줍는가**까지 받는다. 양손 무기는 두 팔이 다 성해야 한다.
    // 지금 주우면 **어느 손에 들어가는가.** None = 못 줍는다.
    //   ★ 손 선택이 줍기에서 사라졌으므로 규칙이 필요하다:
    //     **주손(오른손) 우선, 그 팔이 부서졌으면 반대 손.**
    //     §1.2 의 기회비용(왼손을 무기로 채우면 횃불을 못 든다)은
    //     8단계 장비 화면으로 옮긴다 — 줍는 순간에 고르게 하면
    //     「집어 든다」와 「장착한다」가 한 동작에 붙어 되돌릴 수가 없다.
    WeaponHand PickupHand(const std::string& weaponId) const;
    // 땅을 밟고 있는가. ★ E(상호작용)가 이것을 본다 —
    //   공중에서는 줍지도, 쉬지도, 문을 넘지도 못한다.
    bool Grounded() const;

    bool CanRoll()   const;   // 다리가 살아 있는가 + 땅을 밟고 있는가
    bool CanJump()   const;   // 다리가 살아 있는가 + 땅을 밟고 있는가 + 웅크리지 않았는가

    // ★ 지금 「웅크린 자세」인가. **그림과 판정이 이 하나를 같이 본다.**
    //   둘이 다른 조건을 쓰면 「그림은 서 있는데 판정은 웅크린」 상태가 생기고,
    //   그건 화면만 보고는 못 찾는다.
    //
    //   ★ Ctrl 을 떼어도 **천장이 낮으면 웅크린 채**다(m_crouchForced).
    //     없으면 낮은 틈에서 일어서는 순간 몸이 천장 속에 박힌다.
    bool Crouched() const;

    // ========================================================================
    //  ★★ 방어 — **상태가 아니라 수식자**다 (design.md §3.11)
    //
    //    웅크리기와 같은 판단이다. 상태로 만들면 「방어하며 걷기」
    //    「방어하며 앉기」처럼 **경우의 수가 곱해진다.**
    //    수식자로 두면 이동·그림·판정이 각자 이 하나를 보면 된다.
    //
    //    ★ 조작을 새로 만들지 않았다: 왼손에 방패가 있으면 **좌클릭이 곧
    //      방어**다. 「버튼은 손이고, 그 손에 든 것이 무엇을 할지 정한다」
    //      (§3.2.1.1) 가 여기서 또 값을 한다.
    // ========================================================================
    bool Guarding() const;

    // 막는 상자. ★ **자세를 따라 내려간다** — 그래서 「앉으면 하단을 막는다」에
    //   특수 규칙이 없다. 방패가 없거나 방어 중이 아니면 빈 상자.
    AABB GuardBox() const;

    // 방어 중인 손. 없으면 None. ★ 왼손 우선 — 보조 슬롯이 방패 자리다.
    WeaponHand GuardHand() const;

    // ---- Scene 과 주고받는 요청 ----
    //   ★ 컨트롤러는 **월드에 떨어진 물건을 모른다.** 「떨궈야 한다」까지만
    //     말하고, 어디에 어떻게 놓을지는 Scene 이 정한다.
    //     사망 화면을 Scene 이 띄우는 것과 같은 구조다.
    //
    //   ★★ **목록**이다(8-b). 손이 둘이 되면 한 틱에 둘이 떨어질 수 있다 —
    //     지금은 한 대에 한 부위만 부서지므로 실제로는 하나뿐이지만,
    //     「하나뿐이라 괜찮았던 것」을 이번에는 미리 접는다.
    std::vector<std::string> ConsumeDrops();

    // ★★ **줍기는 이제 E 다.** 손 버튼에서 떼어 냈다.
    //   전에는 「발밑에 무기가 있으면 그 버튼이 줍기가 된다」였는데,
    //   그러면 **무기 위에 서 있는 동안 휘두를 수가 없다** — 떨군 무기를
    //   밟고 싸우는 상황에서 손이 통째로 막힌다.
    //   E 로 옮기자 좌/우클릭은 **언제나 손**이 되었다(§3.2.1.1 이 더 깨끗해졌다).
    //
    //   ※ 줍기 요청 큐가 통째로 사라졌다. E 는 Scene 이 직접 받으므로
    //     「컨트롤러가 요청하고 Scene 이 처리한다」는 왕복이 필요 없다.
    //     떨구기(ConsumeWeaponDropRequest)는 남는다 — 그건 **팔이 잘릴 때**
    //     온다(§3.2.2). 무기를 일부러 버리는 키는 **없다.**

    // 무기를 손에 넣었다. Scene 이 줍기를 처리한 뒤 알려 준다.
    //   ★ **무엇을** 들었는지까지 받는다. 무기가 둘이 된 순간 「손에 들었다」
    //     만으로는 부족하다 — 땅에 떨어진 것이 단검인지 대검인지가 결과를 바꾼다.
    void EquipWeapon(WeaponHand hand, const std::string& weaponId);

    // **그 손의** 무기. ★ 카탈로그에 없으면 첫 무기로 대신한다 —
    //   파일에서 무기 이름이 사라져도 게임은 돈다(로더들과 같은 태도).
    //   ※ 빈손인지는 `HandArmed` 로 따로 묻는다. 여기서 널을 돌려주면
    //     부르는 쪽마다 널 검사가 생긴다.
    const WeaponType& Weapon(WeaponHand hand) const;

    // 그 손에 든 것의 이름. 빈손이면 빈 문자열.
    const std::string& HandItem(WeaponHand hand) const;

    // 무기 카탈로그. Scene 이 **땅에 놓을 물건의 생김새**를 물을 때 쓴다.
    //   ★ 「왜 플레이어에게 무기 종류를 묻지?」가 맞다 — 지금 무기를 쓰는 것이
    //     플레이어뿐이라 여기 있을 뿐이다. 상자나 적이 무기를 내놓게 되는
    //     순간 **적 카탈로그처럼 Scene 으로 올라간다**(design.md §9 의
    //     「두 번째 사용자가 생겼을 때 올린다」).
    const WeaponCatalog& Weapons() const { return m_weapons; }

    // ★ F8/F9(카탈로그에서 꺼내 들기)가 **사라졌다**(8-e). 장비 화면이
    //   그 일을 한다 — 허공에서 꺼내던 것이 가방에서 꺼내는 것이 되었다.

    // ========================================================================
    //  ★★ 가방 (8-e, design.md §3.10.3)
    //
    //    손 ⇄ 가방 ⇄ 땅. 「가지고는 있지만 들고 있지 않은 것」의 자리다.
    //    전에는 이것이 없어서, 무기를 바꾸면 들고 있던 것을 **땅에 버려야**
    //    했다.
    //
    //    ★ 칸이 **고정**이다(`array`). 하나를 꺼내도 나머지가 당겨지지 않는다 —
    //      당겨지면 화면에서 커서 밑의 물건이 바뀌어, 「방금 그 칸」을 다시
    //      누르면 다른 것이 나간다.
    //    ★ 6칸으로 **작다.** 무제한이면 전부 들고 다니면 그만이라
    //      「무엇을 가져갈까」가 사라진다(§1.2).
    // ========================================================================
    static constexpr int kBagSize = 6;

    const std::string& BagItem(int slot) const { return m_bag[slot]; }

    // 빈 칸에 넣는다. 가득 차 있으면 false — 넣지 않는다.
    bool StoreInBag(const std::string& id);
    bool BagHasRoom() const;

    // 가방의 것을 그 손에 든다. 손에 있던 것은 **가방으로** 간다.
    //   ★ 꺼낸 칸이 **먼저** 비워지므로, 가방이 가득 차 있어도 맞바꾸기는
    //     된다(손의 것이 방금 빈 그 칸으로 들어간다).
    //   못 들면(팔이 없다 · 양손인데 한 팔뿐) false.
    bool EquipFromBag(int slot, WeaponHand hand);

    // 그 손의 것을 가방에 넣는다. 가방이 차 있으면 false — 손에 그대로 둔다.
    bool StowHand(WeaponHand hand);

    // 버린다 = **발밑에 떨군다.** 없애지 않는다 — 떨어진 물건 목록에 오른다.
    void DiscardBag(int slot);
    void DiscardHand(WeaponHand hand);

    // ---- ★ 팔 레이어 (design.md §8.1) ----
    //   팔을 별도 시트로 겹쳐 두고, 잘리면 그 장을 숨기고 상처 장을 켠다.
    //
    //   ★ 왜 Scene 이 번호를 넘겨 주는가:
    //     컨트롤러는 **에셋을 모른다.** 텍스처를 어디서 읽고 어떤 순서로 겹칠지는
    //     조립하는 쪽(Scene)의 일이다. 사망 화면·무기 떨구기와 같은 경계다.
    //     번호가 -1 이면 레이어가 없는 조립이고, 그래도 동작한다.
    void SetArmLayers(int armFront, int armBack, int stumpFront, int stumpBack);

private:
    void ChangeState(SceneContext& ctx, PlayerState next, bool force = false);
    // ★ ctx 를 받지 않는다. 무브셋 선택이 **입력을 직접 보면 안 되기** 때문이다 —
    //   자세는 입력에서 파생된 것이고(천장까지 본다), 여기서 입력을 다시 보면
    //   파생 전의 값으로 판단하게 된다.
    const AttackData& SelectAttack(PlayerState prev) const;

    // ★ moveY 가 사라졌다. 플랫포머에서 세로 위치를 정하는 것은 **중력**이지
    //   입력이 아니다. 인자를 지우면 옛 호출이 전부 컴파일 에러가 되어
    //   「고쳐야 할 곳 목록」을 컴파일러가 만들어 준다.
    void UpdateMovement(SceneContext& ctx, float moveX);

    // 무브셋을 파일에서 다시 읽는다. 실패하면 이전 값이 그대로 남는다.
    void ReloadWeapons();

    // 지금 자세에 맞는 기본 그림. 자세를 고르는 곳은 여기 한 곳이다.
    const AnimationClip& PostureClip(bool moving) const;

    // 지금 몸이 있어야 할 「기본 상태」. 공중이면 Jump, 아니면 Idle/Run.
    //   ★ 공격·구르기가 끝나는 자리마다 `moving ? Run : Idle` 을 적었더니
    //     공중에서 끝났을 때 한 틱 동안 서 있는 그림이 나왔다. 한 곳으로 모은다.
    PlayerState RestingState(bool moving) const;

    // 잘린 팔을 그림에 반영한다. 틴트·눌림과 같은 「표현」이라 Render 에서 부른다.
    void UpdateArmLayers();

    // ★ 남은 틱에 비례해 감속하며 미끄러진다. 총 이동량이 distance 가 되도록 정규화.
    //   구르기와 넉백이 공유한다 — 일정 속도로 움직이면 둘 다 어색하다.
    //   ★ dirY 를 지웠다. 세로 이동은 이제 BodyComponent 의 속도가 담당한다 —
    //     두 가지 방식이 같은 축을 동시에 밀면 반드시 어긋난다.
    void SlideDecaying(float dirX, float distance, int totalTicks);

    AABB SpriteBounds() const;

    // ★ 무기 **카탈로그**를 값으로 소유한다(8-a). 「직전에 기본 공격을 냈는가」를
    //   `m_currentAttack == &Weapon().light` 로 묻고 있으므로 주소가 안정해야 한다.
    //   ★ map 의 원소는 주소가 안 변한다 — 리로드는 **값만** 덮어쓴다.
    WeaponCatalog m_weapons;

    // 맨손. ★ 무기 **바깥**에 있다 — 「무기가 없을 때」는 몸의 성질이지
    //   무기의 성질이 아니다(WeaponType.h 주석).
    UnarmedSet    m_unarmed;

    // ---- ★★ 손 슬롯 둘 (8-b) ----
    //   전에는 `m_weaponHand`(한 자루를 어느 손에) + `m_weaponId`(그게 뭔지)
    //   였다. 두 변수가 **하나의 「한 자루」를 나눠 들고** 있어서 두 자루를
    //   표현할 수가 없었고, 그래서 `twoHanded` 의 대가가 실은 0 이었다 —
    //   대검을 들든 단검을 들든 왼손은 똑같이 비어 있었다.
    //
    //   ★ 이제 「각 손에 무엇이 있는가」다. 빈 문자열 = 없음.
    //     **양손 무기는 두 칸을 다 차지한다** — 그 한 가지 규칙으로
    //     「좌/우클릭이 둘 다 같은 것을 휘두른다」와 「왼손이 막힌다」가
    //     둘 다 나온다. `twoHanded` 분기가 오히려 줄었다.
    //
    //   ★ 포인터가 아니라 이름인 이유는 적 스폰이 종류를 이름으로 가리키는
    //     것과 같다 — 카탈로그가 바뀌어도 「무엇을 들고 있었는가」는 남는다.
    std::string   m_hand[2];   // [0] = 오른손, [1] = 왼손

    // 가방. 빈 문자열 = 빈 칸. ★ 손 슬롯과 **같은 표현**이다 —
    //   「이름 하나가 곧 물건」이 손·가방·땅에서 똑같다.
    std::array<std::string, kBagSize> m_bag;

    // Start 에서 캐시한다. 널이 될 수 없다(Require).
    BodyComponent*    m_body    = nullptr;
    SpriteComponent*  m_sprite  = nullptr;
    PoiseComponent*   m_poise   = nullptr;
    PartsComponent*   m_parts   = nullptr;
    StaminaComponent* m_stamina = nullptr;

    PlayerState m_state      = PlayerState::Idle;
    int         m_stateTicks = 0;

    // ★ HP 는 여기 없다 — PartsComponent 가 부위별로 갖는다(design.md §3.2.2).
    //   「전체 HP」라는 숫자가 의미를 잃었기 때문이다.
    int m_flash = 0;   // 피격 번쩍임 남은 틱

    // 튕김 번쩍임 남은 틱. ★ 피격(m_flash · 붉은색)과 **따로** 둔다 —
    //   튕긴 것은 맞은 것이 아니다. 같은 색이면 「벽을 쳤다」가 「맞았다」로 읽힌다.
    int m_sparkTicks = 0;

    int m_invulnTicks = 0;

    // 부활 지점. ★ 맵이 아니라 **세이브 포인트**가 정한다. 높이까지 기억한다.
    float m_homeX = 120.0f;
    float m_homeY = 680.0f;

    // 구르기 방향은 **시작 시점에 고정**된다. 중간에 방향키를 바꿔도 무시된다.
    //   ★ 세로 성분이 사라졌다 — 구르기도 넉백도 이제 수평 이동이다.
    float m_rollDirX  =  1.0f;
    float m_knockDirX = -1.0f;

    // ★ 무브셋 : 어느 공격인지도 시작 시점에 고정된다.
    //   매 틱 다시 고르면 휘두르는 도중에 Ctrl 을 떼는 순간 프레임 데이터가 갈려
    //   active 구간을 건너뛰거나 두 번 지나간다.
    const AttackData* m_currentAttack = nullptr;
    // ★ m_comboStep 을 지웠다. 「몇 타째인가」를 세는 대신 「직전에 무엇을
    //   냈는가」(m_currentAttack)를 묻는다 — 이어지는 것은 기본 공격뿐이므로
    //   셀 것이 없다. 상태를 옳은 곳에 두면 필드가 사라진다.
    //
    //   ★★ 그리고 `bool m_comboQueued` 도 사라졌다. 예약에는 「했는가」만이
    //     아니라 **「어느 손으로」**가 필요해졌는데, bool 로는 말할 수 없다.
    //     None 이 「예약 없음」을 겸하므로 필드가 하나로 합쳐졌다 —
    //     bool 하나를 늘리는 대신 **이미 있는 값에 뜻을 얹는다.**
    WeaponHand m_queuedHand = WeaponHand::None;

    // ★ 물기 요청. 다른 키로 들어오므로 무브셋 선택에서 최우선이다.
    // 이번 공격을 **어느 손**이 냈는가. 무엇이 나갈지는 SelectAttack 이 정한다.
    WeaponHand m_pendingHand = WeaponHand::None;
    bool m_hitThisSwing = false;

    // ★★ 이름이 `m_crouching` 이 아니라 `m_crouchHeld` 인 이유가 있다.
    //   이건 **입력**이지 자세가 아니다. 자세는 Crouched() 다(천장까지 본다).
    //
    //   전에 `m_crouching` 이었을 때, 「자세」를 물어야 할 네 곳이 이걸 그대로
    //   읽고 있었다 — 이동 속도 · 발소리 · 무브셋 선택 · 화면 표시.
    //   전부 컴파일되고 전부 동작했다. **낡은 질문에 답했을 뿐이다.**
    //
    //   이름을 바꾸자 그 네 곳이 전부 컴파일 에러가 되었다.
    //   ★ 파생된 답을 만들면 **원본의 이름을 바꿔** 옛 사용처를 드러낸다.
    //     눈으로 훑는 것과 달리 빠뜨릴 수가 없다.
    // ★ 방패를 든 손의 버튼을 누르고 있는가. **입력 그 자체**다 —
    //   「막고 있는가」는 Guarding() 이 답한다(공중·행동 중을 같이 본다).
    //   `m_crouchHeld` 와 **같은 이유로 같은 이름 규칙**을 쓴다.
    bool m_guardHeld    = false;

    bool m_crouchHeld   = false; // 키를 누르고 있는가 (입력 그 자체)
    bool m_crouchForced = false; // 천장이 낮아 못 일어선다
    // ★ 「쉬는 자세의 그림이 바뀌었는가」를 **그림 자체로** 묻는다.
    //   전에는 `m_crouchedLast`(웅크렸었나) 였는데, 방어가 붙으면서 자세는
    //   그대로인데 그림만 바뀌는 경우가 생겼다 — **원인을 세는 대신
    //   답을 비교한다.** 무엇이 그림을 바꾸든 이 줄은 안 바뀐다.
    const AnimationClip* m_restingClipLast = nullptr;
    int  m_stepCooldown = 0;

    // ★ 장비는 **되돌아가지 않는다**(design.md §3.6.1 의 「남는다」 칸).
    //   죽어도 무기는 떨어진 자리에 남고, 손은 빈 채로 부활한다.

    // 이번 틱에 손에서 떨어진 것들. Scene 이 가져가 월드에 놓는다.
    //   ★ 이름이 `m_dropRequests` 인 이유: Scene 에도 `m_drops` 가 있는데
    //     **그쪽은 월드에 실제로 놓인 물건**이다. 같은 이름이면 두 개의 다른
    //     것이 한 단어를 쓰게 되고, 읽는 사람이 반드시 한 번은 헷갈린다.
    std::vector<std::string> m_dropRequests;

    int  m_armorIndex = 0;       // F2 로 바뀐다(임시)
    bool m_deathScreenRequested = false;

    // 팔 레이어 번호. -1 = 안 붙었다.
    int m_layerArmFront   = -1;
    int m_layerArmBack    = -1;
    int m_layerStumpFront = -1;
    int m_layerStumpBack  = -1;
};
