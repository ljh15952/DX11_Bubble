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

    // ★ 피격 경직. 입력을 받지 않는다(Exhausted 와 같은 구조).
    //
    //   여기에 들어갈지 말지는 **숫자가 정한다** — 갑옷의 강인도(poise)가
    //   맞은 공격의 충격력(impact)보다 크면 이 상태에 들어가지 않는다.
    //   「상태는 안 늘어나고, 다양성은 데이터에서」의 또 하나의 적용이다.
    Hurt,

    // HP 0. 아무 입력도 받지 않는다.
    // ★ 5-e-4 에서 DeathScene 전환으로 바뀐다. 지금은 화면에 표시만 한다.
    Dead,
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
    // 화면·로그에 찍는 이름. ★ ASCII 만 — BitmapFont 가 ASCII 전용이다.
    const char* name = "LIGHT";

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

    // ★ 강인도 데미지. 맞는 쪽의 poise 와 비교되어 **경직 여부**를 정한다.
    //   damage 와 일부러 다른 숫자로 둔다. 합치면
    //   「약하지만 크게 휘청이게 하는 공격」(방패 밀치기 같은 것)을 못 만든다.
    int impact      = 14;

    // ★ 공격이 **자기 그림 속도를 직접 들고 다닌다.**
    //
    //   지금까지는 클립이 밖에 하나 있었다(kAttackClip). 공격이 하나뿐이었으니까.
    //   무브셋이 되면 공격마다 길이가 달라서, 밖에 두면
    //   「이 공격은 몇 틱짜리인데 그림은 24틱」이 되어 모션이 잘리거나 남는다.
    //
    //   ★ frameCount × ticksPerFrame == TotalTicks() 가 되도록 짝을 맞춘다.
    //     아래 데이터가 전부 그렇게 되어 있다.
    AnimationClip clip{ /*row*/ 2, /*frames*/ 6, /*ticks*/ 4, /*loop*/ false };

    int TotalTicks() const { return startup + active + recovery; }
};


// ============================================================================
//  ★ 이 구조체를 플레이어와 적이 **같이 쓴다.**
//
//    「공격」은 누가 하든 같은 개념이다 — 발생·지속·후딜이 있고, 상자가 있고,
//    데미지와 충격력이 있다. 방향만 반대다.
//    6단계에서 weapons.json 과 enemies.json 이 **같은 스키마**를 쓰게 된다.
//
//    적은 staminaCost 를 쓰지 않고 플레이어는 아직 impact 를 쓰지 않는다.
//    안 쓰는 칸이 남는 것은 값이 싸다 — 구조를 두 벌 만드는 것보다 훨씬 싸다.
// ============================================================================


// ============================================================================
//  ArmorData — 강인도(poise)
//
//    ★ 「경직을 둘까 말까」를 코드가 아니라 데이터가 결정하게 만드는 장치.
//
//        poise >= impact  →  데미지만 받고 자리에서 버틴다 (경직 없음)
//        poise <  impact  →  Hurt(경직) + 넉백 + 피격 무적
//
//    그래서 갑옷이 「강함」이 아니라 **「지불 방식」**이 된다.
//
//        가벼운 갑옷  poise 낮음 → 경직 O   구르기 빠름  → 「흘려서 산다」
//        무거운 갑옷  poise 높음 → 경직 X   구르기 느림  → 「버텨서 산다」
//
//    버티는 쪽은 안전한 것이 아니다 — 맞을 때마다 HP 가 확실히 깎인다.
//    구르기는 스태미나로, 버티기는 HP 로 지불한다.
//
//    ※ 지금은 **문턱(threshold)** 방식이다(다크소울 1식).
//      나중에 소모(게이지) 방식으로 바꾸려면 위의 비교 한 줄이
//      게이지 감산으로 바뀔 뿐이다. 자리를 잡아 두는 것으로 충분하다.
// ============================================================================
struct ArmorData
{
    const char* name  = "CLOTH";
    int         poise = 10;

    // 방어력(데미지 감산)은 일부러 넣지 않았다.
    // poise 와 한 숫자로 합치면 「가볍지만 튼튼한 갑옷」을 만들 수 없어진다.
};


// ============================================================================
//  HurtData — 피격 경직
//
//      t0            ticks              invuln
//      │─── 경직 (입력 없음) ───│
//      │──────── 피격 무적 ──────────│
//                                 ↑ 여기가 핵심
//
//    ★ invuln > ticks 여야 한다.
//
//      같게 두면 **경직이 풀리는 그 틱에 다시 맞는다.** 적이 두 마리면
//      영원히 못 움직인다 — 이것을 스턴락(stun-lock)이라 하고, 있는 게임은
//      「억울하게 죽었다」는 감각을 준다.
//      「경직이 풀리고 나서도 조금 더 안전」이 스턴락 방지의 실제 조건이다.
//
//      버그처럼 보이지 않는 채로 남기 때문에, 숫자로 못 박아 둔다.
// ============================================================================
struct HurtData
{
    int   ticks     = 18;    // 경직 (입력 없음)
    int   invuln    = 24;    // ★ 반드시 ticks 보다 길게
    float knockback = 22.0f; // 밀려나는 거리 (캔버스 픽셀)

    // 넉백은 타격감 때문만이 아니다.
    // 밀려나면 **적의 다음 공격 사거리에서 벗어난다** —
    // 피격 무적이 시간으로 막는 것을 넉백은 거리로 막는다.
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

    // ★ 다리는 좌/우로 나누지 않고 하나로 둔다.
    //   둘로 나누면 「한쪽만 부서지면 어떻게 되나」가 애매해진다.
    //   하나면 「다리가 부서졌다 = 못 걷는다」로 규칙이 명확하다.
    Part_Legs,

    Part_Count
};


// ★ HP·이름과 상자 좌표를 나눈다.
//
//   HP 는 자세와 무관하다 — 엎드려도 머리 HP 는 그대로다.
//   상자는 자세마다 다르다 — 서 있을 때와 엎드렸을 때 머리 위치가 다르다.
//
//   격투게임의 일반 원칙이다. 웅크리기·점프·공격마다 hurtbox 가 다르다.
//   섞어 두면 「자세가 바뀌었는데 판정 상자가 공중에 떠 있는」 상태가 된다.
struct PartBox
{
    // 발밑 원점 기준 상대 좌표. 오른쪽을 보는 자세로 적는다.
    // 좌우 비대칭인 자세(엎드리기 등)는 facing 에 따라 뒤집힌다.
    float left, top, right, bottom;
};


// ============================================================================
//  EnemyState
//    플레이어와 같은 구조의 상태 머신.
//
//        ┌──────┐  플레이어 발견   ┌───────┐
//        │ Idle │ ──────────────→ │ Chase │
//        └──────┘                 └───┬───┘
//                                     │ 다리 파괴
//                                     ↓
//                                 ┌───────┐
//                                 │ Crawl │  기어서 쫓아온다
//                                 └───────┘
//
//    ★ 부위 파괴의 진짜 의미는 데미지가 아니라 **행동의 변화**다.
//      다리를 부수면 죽지 않고 느려진다. 그래서 「봉쇄」라는 전술이 성립한다.
// ============================================================================
enum class EnemyState
{
    Idle,
    Chase,

    // ★ 사거리 안에서 공격. 플레이어의 Attack 과 완전히 같은 구조다 —
    //   startup / active / recovery 를 쓰고, active 구간에만 판정이 있다.
    //   startup 구간에는 예고(`!`)를 띄운다. 예고가 없으면 회피는 운이 된다.
    Attack,

    Crawl,
    Dead,
};


class PlayScene final : public Scene
{
public:
    const char* Name() const override { return "Play"; }

    bool Enter(SceneContext& ctx) override;
    void Update(SceneContext& ctx, bool consumeEdgeInput) override;

    // ★ 위에 있던 Scene 이 닫혔을 때. DeathScene 이 닫혔으면 여기서 부활한다.
    //   PauseScene 이 닫힌 경우와는 **상태로 구분된다** — Dead 인가 아닌가.
    //   그래서 DeathScene 은 PlayScene 을 몰라도 되고, 둘 사이에 포인터가 없다.
    void Resume(SceneContext& ctx) override;
    void Render(Renderer& renderer) override;     // 월드 : 캐릭터 · 장애물 · 히트박스
    void RenderUI(Renderer& renderer) override;   // UI   : 안내 문구

private:
    // ============================================================================
    //  Respawn — ★ 「무엇을 되돌리고 무엇을 남기는가」의 목록
    //
    //    사망·부활은 처음으로 **틱을 넘어서는 규칙**이다. 그리고 그 규칙의 정체는
    //    이 함수의 내용 그 자체다 —
    //    죽었을 때 무엇이 되돌아가고 무엇이 남는지가 이 게임이 무슨 게임인지를 정한다.
    //
    //      되돌아간다 : 플레이어(HP·스태미나·위치·상태) / 적(부위 HP·위치·상태)
    //      남는다     : (아직 없음. 7단계에서 열어둔 문·얻은 아이템이 여기 들어온다)
    //
    //    적이 되살아나는 것은 확정된 설계다(design.md §3.6.1).
    //    전부 되돌리면 벌이 너무 크고, 아무것도 안 되돌리면 죽음이 무의미해진다.
    //
    //  ★ Enter 가 이 함수를 부른다. 초기화와 부활은 **같은 일**이다.
    //    두 벌로 만들면 반드시 어긋난다 — 나중에 필드를 하나 추가할 때
    //    한쪽만 고치고, 「두 번째 판부터 스태미나가 이상하다」 같은 버그가 된다.
    //    한 곳으로 모아 두면 그 종류의 버그가 존재할 수 없다.
    // ============================================================================
    void Respawn();

    // ============================================================================
    //  무브셋 — 「어느 공격이 나가는가」를 입력 맥락이 정한다
    //
    //    ★ 상태는 **여전히 안 늘어난다.** PlayerState::Attack 하나 그대로다.
    //      바뀌는 것은 「어느 AttackData 를 쓰는가」뿐이다.
    //
    //      이 구조는 5-e-3 에서 적이 이미 증명했다 —
    //      m_enemy.attackIsBite 로 swing / bite 를 골랐던 그것과 같다.
    //      플레이어는 자세 대신 **입력 맥락**으로 고른다는 것만 다르다.
    // ============================================================================

    // 공격에 들어가는 순간 딱 한 번 불린다. prev = 들어오기 직전의 상태.
    const AttackData& SelectAttack(SceneContext& ctx, PlayerState prev) const;

    // 지금 나가고 있는 공격. 판정·히트박스·표시가 전부 이것을 본다.
    const AttackData& CurrentAttack() const;

    // 상태를 바꾼다. 같은 상태로의 전이는 무시한다.
    //
    // ★ force 가 필요해진 이유: 콤보는 Attack -> Attack 이다.
    //   「같은 상태면 무시」가 원래는 애니메이션이 프레임 0 에서 멈추는 것을
    //   막는 장치였는데, 콤보는 그 판정에 정확히 걸린다.
    // ★ 여기가 "Enter" 다 — 애니메이션 시작, 소리, 흔들림처럼
    //   들어가는 순간 한 번만 해야 하는 일을 여기서 한다.
    //   매 틱 하면 애니메이션이 프레임 0 에서 멈춘다.
    void ChangeState(SceneContext& ctx, PlayerState next, bool force = false);

    // 이동 처리. Idle / Run 상태에서만 불린다.
    void UpdateMovement(SceneContext& ctx, float moveX, float moveY);

    // 스태미나 회복. 상태와 무관하게 매 틱 불린다.
    void UpdateStamina();

    // 바(bar) 는 UI 레이어에 그린다 — 화면이 흔들려도 제자리에 있어야 한다.
    void DrawStaminaBar(Renderer& renderer) const;
    void DrawHpBar(Renderer& renderer) const;

    // 지금 입고 있는 갑옷. F2 로 갈아입는다(임시).
    const ArmorData& Armor() const;

    AABB SpriteBounds() const;

    // ★ 용어를 나눠 쓴다. 섞으면 "내 공격이 나를 때리는" 코드를 쓰게 된다.
    //   hurtbox = 내가 맞는 범위 (몸)
    //   hitbox  = 내가 때리는 범위 (무기)
    AABB PlayerHurtbox() const;
    AABB AttackHitbox()  const;

    // 지금 공격 판정이 존재하는가 (active 구간인가)
    bool AttackActive() const;

    // ★ 무적은 두 종류다. 같은 함수로 판정되지만 **성격이 다르다.**
    //
    //     ① 구르기 무적 — 플레이어가 벌어낸 것 (스태미나를 내고 타이밍을 맞췄다)
    //                     → 게임 디자인. 재미를 만든다
    //     ② 피격 무적   — 게임이 주는 안전장치 (스턴락 방지)
    //                     → 게임 공학. 불공평을 막는다
    //
    //   목적이 다르므로 길이를 따로 두고, F1 표시 색도 나눈다.
    //   그래야 「지금 어느 무적인가」가 눈에 보인다.
    //
    //   ★ ② 는 ①의 부속품이 아니라 **경직의 부속품**이다.
    //     경직이 없으면(강인도로 버텨내면) 못 움직이는 구간이 없으므로
    //     스턴락 위험도 없고, 그래서 피격 무적도 주지 않는다.
    bool RollInvincible() const;   // ① 만
    bool Invincible()     const;   // ① 또는 ②

    // ★ hurtbox 를 빈 사각형으로 만드는 방법도 있지만, 그러면 F1 에서
    //   「지금 무적인가」를 눈으로 볼 수 없다. hurtbox 는 그대로 두고
    //   판정하는 쪽에서 위 함수를 확인하면, 색만 바꿔서 표시할 수 있다.

    // 피격 처리. **경직 여부를 여기서 강인도로 결정한다.**
    void HitPlayer(SceneContext& ctx, const AttackData& atk);

    // ★ 남은 틱에 비례해 감속하며 미끄러진다. 총 이동량이 distance 가 되도록 정규화.
    //
    //      속도
    //       │▓▓▓▓▓▓
    //       │▓▓▓▓
    //       │▓▓
    //       └────────→ 틱
    //
    //   구르기와 넉백이 이것을 공유한다. 일정 속도로 움직이면 둘 다 어색하다.
    //   카메라 흔들림 감쇠와 같은 발상이다.
    void SlideDecaying(float dirX, float dirY, float distance, int totalTicks);

    // 구르기 이동. Roll 상태에서만 불린다.
    void UpdateRoll();

    // 넉백 이동. Hurt 상태에서만 불린다.
    void UpdateKnockback();

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

    // ---- 적 공격 : 플레이어의 공격과 대칭이다 ----
    //   AttackActive()      ↔  EnemyAttackActive()
    //   AttackHitbox()      ↔  EnemyAttackHitbox()
    //   m_hitThisSwing      ↔  m_enemy.hitThisSwing
    const AttackData& EnemyAttack() const;   // 자세에 따라 swing / bite
    bool  EnemyAttackActive() const;
    AABB  EnemyAttackHitbox() const;

    // ★ startup 구간 = 예고(telegraph).
    //   적의 startup 이 몇 틱이든, 플레이어가 그 시작을 볼 수 없으면 회피는 운이다.
    //   지금은 그림이 없으므로 머리 위 `!` 로 대신한다.
    //   ※ 이것이 기획서 1.1 의 「初心者の指輪 = 적의 공격 예고」의 원형이다.
    //     나중에 이 표시를 지문 장착 여부로 감싸면 그대로 아이템이 된다.
    bool  EnemyTelegraph() const;

    // 적 공격의 **가로** 사거리. 자세마다 다르다(물어뜯기가 더 짧다).
    float EnemyAttackRange() const;

    // ★ 공격할 수 있는 위치에 있는가.
    //
    //   거리(원형)로 재면 안 된다 — 히트박스가 **가로로 뻗기** 때문이다.
    //
    //          ┌──────────┐
    //     적 ──┤ 히트박스  │   ← 가로로 뻗는다
    //          └──────────┘
    //          ↕ 세로는 좁다
    //
    //   플레이어가 위/아래로 34px 떨어져 있으면 **거리는 사거리 안**이지만
    //   상자는 옆으로 뻗어 있어 영원히 헛친다. 적이 제자리에서
    //   허공을 계속 후려치는 상태가 된다.
    //   그래서 가로(사거리)와 세로(허용폭)를 따로 본다.
    //
    //   ★ 그리고 이 하나가 「멈추는 위치」와 「공격하는 위치」 양쪽에 쓰인다.
    //     둘을 다른 조건으로 두면 「멈췄는데 못 때리는」 적이 생긴다.
    bool EnemyInAttackPosition() const;

    void  TryEnemyAttack(SceneContext& ctx);

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sheet;

    // ---- 임시 플레이어. 적이 등장하면 Gameplay/Character 로 뺀다 ----
    //   좌표는 캔버스(640x360) 기준이고, x / y 는 "발밑 가운데" 다.
    struct Player
    {
        float x      = 120.0f;
        float y      = 260.0f;
        int   facing = 1;     // +1 = 오른쪽, -1 = 왼쪽
        int   flash  = 0;     // 남은 번쩍임 틱 (피격 표현용)

        // ★ 플레이어는 부위별 HP 가 아니라 **단일 HP** 다.
        //   부위 파괴는 적에게만 있는 시스템이다(기획서 3.2).
        //   플레이어까지 부위를 나누면 UI 와 조작이 모두 무거워진다.
        int   hp     = 100;

        // 넉백 방향. 적 → 플레이어 방향으로 피격 시점에 고정된다.
        float knockDirX = -1.0f;
        float knockDirY =  0.0f;

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
        int   facing = -1;  // 플레이어를 바라본다
        int   hp[Part_Count]{};
        int   flash  = 0;   // 피격 번쩍임 남은 틱

        EnemyState state      = EnemyState::Idle;
        int        stateTicks = 0;

        // 다음 공격까지 남은 틱. 없으면 사거리에 들어온 동안 무한 공격이 된다.
        int  attackCooldown = 0;

        // ★ 어느 공격인가를 **시작 시점에 고정한다** (swing / bite).
        //   매번 EnemyLegsBroken() 으로 판정하면, 휘두르는 도중에 다리가
        //   부서지는 순간 프레임 데이터가 통째로 바뀌어(60틱 → 57틱)
        //   active 구간을 건너뛰거나 두 번 지나간다.
        //   구르기 방향을 시작할 때 고정하는 것과 완전히 같은 이유다.
        bool attackIsBite = false;

        // ★ 플레이어의 m_hitThisSwing 과 같은 장치.
        //   active 가 4틱이면 이것 없이는 한 번 휘두를 때 데미지가 4번 들어간다.
        //   ※ 이쪽은 Enemy 안에 둔다 — 적이 여러 마리가 되면
        //     각자 자기 휘두르기를 기억해야 하기 때문이다.
        bool hitThisSwing = false;
    };
    Enemy m_enemy;

    // ctx 를 받는다 — 공격에 들어갈 때 소리를 내야 한다.
    void ChangeEnemyState(SceneContext& ctx, EnemyState next);
    void UpdateEnemy(SceneContext& ctx);

    // 플레이어 쪽으로 speed 만큼 다가간다. Chase / Crawl 이 공유한다.
    // 공격 위치에 도달하면 멈춘다.
    void MoveEnemyTowardPlayer(float speedPerTick);

    bool PlayerDead()      const { return m_state == PlayerState::Dead; }
    bool EnemyDead()       const { return m_enemy.state == EnemyState::Dead; }
    bool EnemyLegsBroken() const { return m_enemy.hp[Part_Legs] <= 0; }

    // ★ 5-e-2 의 m_touching(접촉 판정)은 제거했다.
    //   실제 공격 판정이 들어왔으므로 역할이 끝났고,
    //   접촉 데미지는 두지 않는다(다크소울과 같이 — 맞아야 아프다).

    // ---- 피격 무적 ----
    //   ② 쪽 무적. 남은 틱. 경직과 한 세트로만 주어진다.
    int m_invulnTicks = 0;

    // ---- 무브셋 상태 ----
    //   ★ 공격에 들어가는 순간 고정된다. 매 틱 다시 고르면 안 된다 —
    //     휘두르는 도중에 Ctrl 을 떼는 순간 프레임 데이터가 통째로 갈려
    //     active 구간을 건너뛰거나 두 번 지나간다.
    //     5-e-3 의 attackIsBite 와 완전히 같은 이유다.
    const AttackData* m_currentAttack = nullptr;

    // 콤보. 0 = 1타, 1 = 2타. 2타에서는 더 이어지지 않는다.
    int  m_comboStep   = 0;
    bool m_comboQueued = false;

    // ★ 지금 웅크리고 있나. **입력을 틱에서 갈무리해 둔다.**
    //   Render 는 Renderer 만 받으므로(의도된 설계) 거기서 입력을 읽을 수 없다.
    //   F1 플래그를 Renderer 로 옮긴 것과 같은 종류의 제약이다.
    bool m_crouching = false;

    // DeathScene 을 이미 요청했는가.
    // ★ 한 프레임에 틱이 여러 번 돌 수 있는데 Scene 전환은 프레임 끝에 한 번만
    //   적용되므로, 이 표시가 없으면 같은 프레임에서 Push 를 여러 번 요청해
    //   SceneManager 가 「전환이 두 번 요청됨」 경고를 낸다.
    bool m_deathScreenRequested = false;

    // ★ Hurt 에 들어오기 직전의 상태.
    //   이게 없으면 「고갈 경직 중에 맞으면 경직이 풀린다」가 되어
    //   **피격이 이득**이 되어 버린다.
    PlayerState m_stateBeforeHurt = PlayerState::Idle;

    // 지금 입고 있는 갑옷 번호. F2 로 바뀐다(임시).
    int m_armorIndex = 0;

    // ★ 한 번 휘두를 때 한 번만 맞게 하는 장치.
    //   active 가 3틱이면 판정이 3틱 동안 존재하므로,
    //   이 표시가 없으면 한 번 휘둘렀는데 데미지가 3 번 들어간다.
    //   적이 여러 마리가 되면 이것이 "이미 맞춘 대상 목록" 이 된다.
    bool m_hitThisSwing = false;

    // ★ 히트박스 표시 플래그는 여기 없다 — Renderer 가 가진다.
    //   Scene 에 두면 토글을 틱 안에서 읽어야 하고, 그러면 프레임 정지 중에
    //   F1 이 동작하지 않는다. 자세한 이유는 Renderer::DebugDraw 주석 참조.
    //   그리는 쪽에서 renderer.DebugDraw() 를 읽는다.

    // 발소리 간격 카운터 (틱 단위)
    int m_stepCooldown = 0;
};
