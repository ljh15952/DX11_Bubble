#include "Gameplay/PlayerController.h"

#include "Core/BodyComponent.h"
#include "Core/Constants.h"
#include "Core/GameObject.h"
#include "Core/Log.h"
#include "Core/Motion.h"
#include "Core/Scene.h"
#include "Audio/Audio.h"
#include "Gameplay/PartsComponent.h"
#include "Gameplay/PoiseComponent.h"
#include "Gameplay/StaminaComponent.h"
#include "Graphics/Camera.h"
#include "Graphics/Renderer.h"
#include "Graphics/SpriteComponent.h"
#include "Input/Input.h"

#include <DirectXColors.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <random>
#include <string>

namespace
{
    constexpr int kCellW = 64;
    constexpr int kCellH = 64;

    // ---- 애니메이션 클립 ----
    //   ※ 공격 클립은 AttackData 안에 있다 — 공격마다 길이가 다르기 때문이다.
    constexpr AnimationClip kIdleClip { /*row*/ 0, 4, 10, /*loop*/ true  };   //  6fps
    constexpr AnimationClip kRunClip  { /*row*/ 1, 6,  5, /*loop*/ true  };   // 12fps
    constexpr AnimationClip kRollClip { /*row*/ 3, 6,  4, /*loop*/ false };   // 15fps

    // ★ 다리가 부서졌을 때. tools/gen_player_crawl.ps1 로 만든다.
    //   적의 기어가기 행을 플레이어 팔레트로 다시 칠한 임시 그림이다.
    constexpr AnimationClip kCrawlClip{ /*row*/ 7, 4, 10, /*loop*/ true };

    // ★ 웅크린 자세. tools/gen_player_crouch.ps1 로 만든다.
    //   전에는 서 있는 그림을 세로로 눌러서(SetScale) 표현했는데, 그러면
    //   **판정 상자는 안 눌려서** 그림과 판정이 다른 말을 했다.
    //   전용 그림을 그리면 둘이 같은 높이(발끝 기준 27)에서 만난다.
    //   ※ 웅크려 걷는 전용 그림은 없다 — 같은 자세를 쓴다.
    constexpr AnimationClip kCrouchClip{ /*row*/ 10, 4, 10, /*loop*/ true };

    // ========================================================================
    //  단검의 무브셋 — 무기 하나 = 공격 여러 개 (기획서 §3.2.1)
    //
    //    heightFromFoot 하나가 닿는 부위를 정한다(겹침 면적이 큰 쪽에 맞으므로).
    //    적의 부위 상자는 발밑 기준 머리 -55..-37 / 몸통 -37..-18 / 다리 -18..0.
    //
    //      light    34  몸통      기본. **이동 중에도 이것이 나온다**
    //      crouch   10  다리      Ctrl. 느리고 약한 대신 **조준할 수 있다**
    //      dash     34  몸통      ★ 구르기 직후에만. 길게 뻗지만 비싸다
    //      combo2   48  머리      1타를 맞춘 뒤에만. 머리는 즉사다
    //      jump      6  머리(위에서)  공중에서만
    //
    //    ★ 「언제 나오는가」가 전부 **직전에 무엇을 했는가**로 정해진다.
    //      키가 하나(좌클릭)인데 무브셋이 다섯인 이유다.
    //
    //    ★ 「머리를 노리려면 콤보를 성공시켜야 한다」가 데이터만으로 성립한다.
    //    ★ light(28) + combo2(34) = 62. 100 중 62를 쓰면 38 이 남고 구르기는 30 —
    //      콤보 뒤에는 한 번밖에 못 구른다. 기획서 §3.1 이 요구한 긴장이다.
    //    ★ clip 은 frames × ticks == TotalTicks 가 되도록 맞춰 두었다.
    //
    //    6-g 에서 이 네 덩어리가 그대로 weapons.json 이 된다.
    // ========================================================================
    constexpr AttackData kDaggerLight{
        /*name*/     "LIGHT",
        /*startup*/  8,
        /*active*/   3,
        /*recovery*/ 13,            // 합계 24 = 6프레임 × 4틱
    };

    constexpr AttackData kDaggerCrouch{
        /*name*/     "CROUCH",
        /*startup*/  10,
        /*active*/   3,
        /*recovery*/ 17,            // 합계 30 = 6프레임 × 5틱. light 보다 느리다
        /*reach*/    10.0f,
        /*width*/    26.0f,
        /*height*/   16.0f,
        /*heightFromFoot*/ 10.0f,   // ★ 다리 상자 한가운데
        /*damage*/   10,            // 약하다 — 조준의 대가
        /*staminaCost*/ 26,
        /*impact*/   14,
        /*clip*/     { /*row*/ 4, 6, 5, false },
    };

    // ---- ★ 대시 공격 — **구르기 뒤에만** 나온다 ----
    //   전에는 「달리다 치면」이었는데, 그러면 **걸으면서 치는 평타가 아예
    //   안 나왔다.** 이동은 거의 항상 하고 있으므로 「기본 공격」이 기본이
    //   아니게 된 것이다. 무브셋이 선택지가 아니라 사고가 되어 있었다.
    //
    //   구르기 뒤로 옮기면 **대가를 먼저 치른 사람만** 쓴다:
    //       구르기 30 + 대시 34 = 64        (100 중)
    //       거기서 THRUST 까지 = 98         거의 전부
    //   길게 뻗는 공격이 「굴러서 파고든 뒤」에 붙으므로 의미도 맞는다.
    constexpr AttackData kDaggerDash{
        /*name*/     "DASH",
        /*startup*/  8,
        /*active*/   4,
        /*recovery*/ 12,            // 합계 24
        /*reach*/    18.0f,         // 멀리서 닿는다
        /*width*/    34.0f,
        /*height*/   24.0f,
        /*heightFromFoot*/ 34.0f,
        /*damage*/   14,
        /*staminaCost*/ 34,         // 비싸다
        /*impact*/   16,
        /*clip*/     { /*row*/ 6, 6, 4, false },   // 6행 = 원래 「달리며 치기」 그림
    };

    constexpr AttackData kDaggerCombo2{
        /*name*/     "THRUST",
        /*startup*/  8,
        /*active*/   3,
        /*recovery*/ 13,            // 합계 24
        /*reach*/    12.0f,
        /*width*/    26.0f,
        /*height*/   22.0f,
        /*heightFromFoot*/ 48.0f,   // ★ 머리 상자 한가운데
        /*damage*/   15,
        /*staminaCost*/ 34,
        /*impact*/   18,
        /*clip*/     { /*row*/ 5, 6, 4, false },
    };

    // ========================================================================
    //  ★ 물기 — 팔이 없어도 쓸 수 있는 최후의 수단 (우클릭 / 패드 Y)
    //
    //    무기를 쥐지 않으므로 **팔이 다 잘려도** 나간다. 언제든 쓸 수 있다.
    //
    //    ★ 높이 48 = 머리다. 그리고 머리는 즉사다(design.md §3.2.2).
    //      「팔을 다 잃으면 목을 물어뜯는 수밖에 없다」 —
    //      아주 위험하지만 **즉사를 노린다.** 패배 직전이 가장 큰 보상을
    //      노리는 순간이 되는 것이 소울류의 리듬이다.
    //
    //    THRUST(콤보 2타)와 높이가 같지만 성격이 갈린다:
    //        BITE   사거리 4 · 데미지 8   — 붙어야 하지만 **언제든**
    //        THRUST 사거리 12 · 데미지 15 — 1타를 맞춰야 하지만 강하다
    // ========================================================================
    constexpr AttackData kBite{
        /*name*/     "BITE",
        /*startup*/  8,
        /*active*/   3,
        /*recovery*/ 13,            // 합계 24 = 6프레임 × 4틱 (8/4 = 2 -> 프레임 2 가 타격)
        /*reach*/     4.0f,         // 아주 짧다 — 목을 물려면 붙어야 한다
        /*width*/    18.0f,
        /*height*/   18.0f,
        /*heightFromFoot*/ 48.0f,   // ★ 머리 높이
        /*damage*/    8,            // 무기보다 나쁘다
        /*staminaCost*/ 18,         // 최후의 수단이 비싸면 안 된다
        /*impact*/   10,            // 적(15)을 못 끊는다
        /*clip*/     { /*row*/ 8, 6, 4, false },
    };

    // 엎드려서 무는 것과 서서 무는 것은 **높이가 다르다.**
    //   ★ 이 한 줄이 없으면 기어가는 중에 물기 상자가 공중에 뜬다 —
    //     자세별 판정 상자에서 이미 두 번 밟은 함정과 같은 종류다.
    constexpr AttackData kBiteProne = []{
        AttackData a = kBite;
        a.heightFromFoot = 18.0f;   // 엎드린 머리(-27..-11)의 한가운데
        a.clip = { /*row*/ 7, 4, 6, true };   // 전용 그림이 없어 기어가기 자세 유지
        return a;
    }();

    // ---- 점프 공격 (design.md §3.8.2) ----
    //   ★ heightFromFoot 이 **6** 이다. 발끝 바로 위 — 아래를 향해 찍는다.
    //     공중에 있으면 그 상자가 적의 머리 높이를 지나간다:
    //         적 머리 상자 = 발끝에서 37~55 위
    //         점프 정점    = 55
    //     즉 궤적의 중간쯤에서 머리에 닿는다. 머리는 즉사(§3.2.2)다.
    //     **「높은 데서 뛰어내려 머리를 노린다」가 숫자 하나로 성립한다.**
    //     지형이 전술이 되는 지점이고, 플랫포머로 옮기는 진짜 이득이다.
    //
    //   impact 20 > 잡몹 강인도 15 이므로 적의 공격을 끊는다. 대신 가장 비싸고,
    //   공중에서는 구를 수 없어 빗나가면 착지 후딜을 그대로 맞는다.
    constexpr AttackData kDaggerJump{
        /*name*/     "JUMP",
        /*startup*/  8, /*active*/ 4, /*recovery*/ 12,   // 24틱 = 6프레임 x 4틱
        /*reach*/    10.0f,
        /*width*/    26.0f,
        /*height*/   30.0f,
        /*heightFromFoot*/ 6.0f,
        /*damage*/   16,
        /*staminaCost*/ 30,
        /*impact*/   20,
        /*clip*/     { /*row*/ 9, 6, 4, false },
    };

    // 공중 전용 몸 그림은 아직 없다. idle 첫 프레임을 **멈춰서** 쓴다 —
    //   팔다리가 파닥이지 않아 오히려 낫고, 전용 행은 나중에 얹으면 된다.
    constexpr AnimationClip kJumpClip{ /*row*/ 0, /*frames*/ 1, /*ticks*/ 8, /*loop*/ true };

    constexpr RollData kRoll{ /*windup*/ 4, /*invincible*/ 12, /*recovery*/ 10 };
    constexpr HurtData kHurt{ /*ticks*/ 18, /*invuln*/ 24, /*knockback*/ 22.0f };

    // ★ 죽은 뒤 사망 화면이 뜨기까지의 한 박자.
    //   즉시 덮으면 무엇에 죽었는지 안 보여 플레이어가 배울 수 없다.
    constexpr int kDeathScreenDelay = 45;   // 0.75 초

    // ---- 갑옷 (임시. F2 로 갈아입어 강인도의 효과를 비교한다) ----
    //   적 공격의 impact 는 swing 18 / bite 12.
    //     CLOTH(10) → 둘 다에 휘청인다   PLATE(24) → 둘 다 버텨낸다
    constexpr ArmorData kArmors[] = { { "CLOTH", 10 }, { "PLATE", 24 } };
    constexpr int kArmorCount = static_cast<int>(std::size(kArmors));

    constexpr float kSpeedPerTick     = 150.0f / 60.0f;   // 틱당 2.5 픽셀
    constexpr float kCrouchSpeedScale     = 0.45f;        // 조준의 대가
    constexpr float kProneSpeedScale      = 0.30f;        // 다리 파괴 = 기어간다

    constexpr float kOriginX = kCellW * 0.5f;
    constexpr float kOriginY = static_cast<float>(kCellH);

    constexpr float kMoveEpsilon      = 0.01f;
    constexpr int   kStepIntervalTicks = 15;
    constexpr int   kFlashTicks        = 9;

    constexpr float kShakeStrength = 2.0f;
    constexpr int   kShakeTicks    = 8;

    // ---- 점프 (design.md §3.8 의 「미결정」 세 칸이 여기서 숫자가 되었다) ----
    //   정점 = v0² / (2g) = 6.2² / 0.7 ≈ 55픽셀,  체공 ≈ 35틱(0.6초)
    constexpr float kJumpSpeed  = 6.2f;
    //   ★ 공중 제어력. 0 이면 「뛰면 끝」이라 답답하고 1 이면 무게가 사라진다.
    constexpr float kAirControl = 0.6f;
    //   ★ 넉백에 섞는 상승. 착지까지 약 11틱이라 경직(18틱)이 끝나기 **전에**
    //     발이 땅에 닿는다 — 「경직은 풀렸는데 아직 공중」이 생기지 않는다.
    constexpr float kHurtLift   = 2.0f;

    constexpr float kStartX = 120.0f;

    float RandomPitch(float spread)
    {
        static std::mt19937 rng{ 12345 };
        std::uniform_real_distribution<float> dist(-spread, spread);
        return dist(rng);
    }

    const char* AttackPhase(int t, const AttackData& a)
    {
        if (t <  a.startup)            return "startup";
        if (t <  a.startup + a.active) return "ACTIVE";
        return "recovery";
    }

    const char* RollPhase(int t, const RollData& r)
    {
        if (t <  r.windup)                return "windup";
        if (t <  r.windup + r.invincible) return "INVINCIBLE";
        return "recovery";
    }
}


const char* PlayerStateName(PlayerState s)
{
    switch (s)
    {
    case PlayerState::Run:       return "RUN";
    case PlayerState::Jump:      return "JUMP";
    case PlayerState::Attack:    return "ATTACK";
    case PlayerState::Roll:      return "ROLL";
    case PlayerState::Exhausted: return "EXHAUSTED";
    case PlayerState::Hurt:      return "HURT";
    case PlayerState::Dead:      return "DEAD";
    default:                     return "IDLE";
    }
}


// ----------------------------------------------------------------------------
//  PostureClip — 지금 자세에 맞는 기본 그림
//
//    ★ 자세를 고르는 곳이 한 곳이어야 한다. 전에는 `Prone() ? 기어가기 : 서기`
//      가 다섯 군데에 복사되어 있었고, 웅크리기를 넣으려면 다섯 곳을 다 고쳐야
//      했다 — 그러다 한 곳을 빠뜨리면 「웅크렸는데 서 있는 그림」이 된다.
// ----------------------------------------------------------------------------
const AnimationClip& PlayerController::PostureClip(bool moving) const
{
    if (m_parts->Prone()) return kCrawlClip;
    if (Crouched())       return kCrouchClip;
    return moving ? kRunClip : kIdleClip;
}


void PlayerController::Start(SceneContext& ctx)
{
    m_body    = &Owner().Require<BodyComponent>();
    m_sprite  = &Owner().Require<SpriteComponent>();
    m_poise   = &Owner().Require<PoiseComponent>();
    m_parts   = &Owner().Require<PartsComponent>();
    m_stamina = &Owner().Require<StaminaComponent>();
    Respawn(ctx);
}


const ArmorData& PlayerController::Armor() const { return kArmors[m_armorIndex]; }


// ----------------------------------------------------------------------------
//  ★ 무기 팔이 잘리면 공격할 수 없다
//
//    §3.10 의 슬롯 구조 그대로다 — **무기는 오른손**, 왼손은 보조(횃불·방패).
//    오른팔이 잘리면 무기를 떨궜으므로 휘두를 것이 없다.
//
//    ★ 이것이 「팔 = 완충재」와 맞물려 진짜 선택을 만든다.
//      적을 마주 보고 막으면 **앞팔 = 무기팔**을 잃는다.
//      등을 돌려 막으면 왼팔을 잃고 무기는 지킨다 — 대신 등을 보인다.
//      규칙 두 개(완충재 · 무기는 오른손)가 곱해져 전술이 나왔다.
//
//    ※ 맨손 공격과 왼손으로 옮겨 들기는 8단계(인벤토리)의 몫이다.
// ----------------------------------------------------------------------------
bool PlayerController::CanAttack() const
{
    // 무기가 없으면 휘두를 것이 없고, 든 손이 잘려도 마찬가지다.
    switch (m_weaponHand)
    {
    case WeaponHand::Right: return !m_parts->IsBroken(Part_RightArm);
    case WeaponHand::Left:  return !m_parts->IsBroken(Part_LeftArm);
    default:                return false;
    }
}


bool PlayerController::ConsumeWeaponDropRequest()
{
    if (!m_weaponDropRequested) return false;
    m_weaponDropRequested = false;
    return true;
}


bool PlayerController::ConsumePickupRequest()
{
    if (!m_pickupRequested) return false;
    m_pickupRequested = false;
    return true;
}


void PlayerController::EquipWeapon(WeaponHand hand)
{
    m_weaponHand = hand;
    Log::Info("[play] 무기를 {} 손에 들었다",
              hand == WeaponHand::Right ? "오른" : "왼");
}


bool PlayerController::CanRoll() const
{
    // 다리가 부서지면 구를 수 없다. 회피 수단을 통째로 잃는다.
    //
    // ★ 공중에서도 못 구른다(design.md §3.8). 구르기는 땅을 박차는 동작이고,
    //   무엇보다 「점프 + 구르기」가 되면 **무적으로 날아다니게** 된다.
    return !m_parts->LegsBroken() && m_body->Grounded();
}


bool PlayerController::Crouched() const
{
    // ★ 공격 중에도 웅크린 채다. 전에는 공격을 제외했는데, 그건 「공격 행에
    //   눌린 자세가 구워져 있으니 또 누르면 두 번 눌린다」는 **스케일 시절의
    //   사정**이었다. 전용 그림을 그린 지금은 4행이 이미 웅크린 높이라
    //   예외가 필요 없다 — 그리고 웅크려 찌르는 동안 판정만 일어서는 것이
    //   오히려 이상했다.
    return m_crouching;
}


bool PlayerController::CanJump() const
{
    // ★ 웅크린 채로는 못 뛴다. 웅크리기는 상태가 아니라 **수식자**라,
    //   조합을 늘리기 시작하면 (웅크린 점프 공격 같은) 경우의 수가 폭발한다.
    return !m_parts->LegsBroken() && m_body->Grounded() && !m_crouching;
}


void PlayerController::SetArmLayers(int armFront, int armBack,
                                    int stumpFront, int stumpBack)
{
    m_layerArmFront   = armFront;
    m_layerArmBack    = armBack;
    m_layerStumpFront = stumpFront;
    m_layerStumpBack  = stumpBack;
}


// ----------------------------------------------------------------------------
//  UpdateArmLayers — 잘린 팔을 그림에 반영한다
//
//    ★ 앞팔 시트는 「바라보는 쪽 팔」이고, 그건 **항상 오른팔**이다.
//      시트가 facing 으로 통째로 뒤집히기 때문이다 — 여기서 facing 을
//      한 번 더 보면 두 번 뒤집혀 제자리로 돌아온다.
//      대응은 Part_FrontArm / Part_BackArm 이 **한 곳에서** 정한다.
//
//    ★★ 처음엔 그 대응을 이 함수에 **베껴 썼다.** 「PickHit 과 같은 규칙을
//      쓸 것」이라고 주석까지 달아 놓고 베꼈고, PickHit 쪽이 이미 틀려 있어서
//      틀린 규칙이 두 벌이 되었다. 같은 규칙은 같은 자리에 있어야 한다 —
//      주석으로 「맞춰 쓰라」고 적는 것은 맞춘 것이 아니다.
// ----------------------------------------------------------------------------
void PlayerController::UpdateArmLayers()
{
    const bool frontLost = m_parts->IsBroken(Part_FrontArm);
    const bool backLost  = m_parts->IsBroken(Part_BackArm);

    // ★ 팔과 상처가 정확히 반대다. 조건을 두 번 쓰지 않고 한 값에서 뽑는다 —
    //   따로 쓰면 「팔도 없고 상처도 없는」 상태가 생길 수 있다.
    m_sprite->SetLayerVisible(m_layerArmFront,   !frontLost);
    m_sprite->SetLayerVisible(m_layerStumpFront,  frontLost);
    m_sprite->SetLayerVisible(m_layerArmBack,    !backLost);
    m_sprite->SetLayerVisible(m_layerStumpBack,   backLost);
}


void PlayerController::Respawn(SceneContext& ctx)
{
    Transform& tr = Owner().transform;
    tr.x = kStartX;
    tr.facing = 1;

    // ★ 세로는 바닥이 정한다. 부활 좌표에 y 를 적어 두면
    //   지면 높이를 바꿀 때 여기만 옛 값으로 남는다.
    m_body->SnapToGround();

    m_parts->Reset();
    m_flash       = 0;
    m_invulnTicks = 0;

    m_rollDirX  =  1.0f;
    m_knockDirX = -1.0f;

    m_currentAttack = nullptr;
    m_biteRequested = false;
    m_comboQueued   = false;
    m_hitThisSwing  = false;
    m_crouching     = false;
    m_crouchedLast  = false;
    m_stepCooldown  = 0;

    m_deathScreenRequested = false;

    m_stamina->Reset();
    m_poise->Reset();
    m_poise->SetValue(Armor().poise);   // ★ 값의 출처는 방어구다

    m_pickupAvailable     = false;
    m_pickupRequested     = false;
    m_weaponDropRequested = false;

    // ---- 남는다 ----
    //   ★ m_weaponHand 도 되돌리지 않는다. 무기를 떨군 채 죽었다면
    //     **빈손으로 부활**하고, 무기는 떨어진 그 자리에 그대로 있다.
    //     되돌리면 「죽으면 무기가 손으로 돌아오는」 게임이 되어
    //     §3.6.1 의 「남는다」가 무의미해진다.
    //
    //   ★ m_armorIndex 는 **일부러 되돌리지 않는다.** 장비는 죽어도 그대로다 —
    //     design.md §3.6.1 의 「남는다」 칸에 실제로 들어간 첫 항목이다.
    //     되돌리면 「죽을 때마다 장비가 벗겨지는」 게임이 된다.

    // ★ ChangeState 를 쓰지 않는다 — 「같은 상태로의 전이는 무시」에 걸린다.
    //   리셋은 상태를 직접 놓고 애니메이션을 forceRestart 로 다시 건다.
    m_state      = PlayerState::Idle;
    m_stateTicks = 0;
    m_sprite->Play(kIdleClip, true);
    m_sprite->ClearTint();
    m_sprite->SetScale(1.0f, 1.0f);

    (void)ctx;
}


// ----------------------------------------------------------------------------
//  SelectAttack — ★ 무브셋의 전부
//
//    ★ 명시적 입력이 암묵적 맥락을 이긴다.
//      Ctrl 을 누르고 있다는 것은 「다리를 노리겠다」는 의사표시이고,
//      「방금 달리고 있었다」보다 최신 의도다.
// ----------------------------------------------------------------------------
const AttackData& PlayerController::SelectAttack(SceneContext& ctx, PlayerState prev) const
{
    // ⓪ 물기는 **모든 것보다 위**다. 다른 키로 들어왔으므로 해석의 여지가 없다.
    //   자세만 반영한다 — 엎드리면 무는 높이가 달라진다.
    if (m_biteRequested)
        return m_parts->Prone() ? kBiteProne : kBite;

    // ★ 쓰러져 있으면 낮게 휘두르는 것밖에 못 한다.
    //   자세가 선택지를 지운다 — 「명시적 입력이 이긴다」보다도 위다.
    if (m_parts->Prone())                return kDaggerCrouch;

    // ★ 공중이면 무조건 내려찍기다. 아래의 선택지(웅크리기·콤보·달리기)는
    //   전부 **발이 땅에 있다**는 전제 위에 있다.
    if (!m_body->Grounded())             return kDaggerJump;

    if (ctx.input.CrouchHeld())          return kDaggerCrouch;   // ① 명시적 입력

    // ② 공격 중이었다 -> 2타
    //   ★ 무한 연타 방지는 **예약하는 쪽** 한 곳에만 있다.
    //     전에 여기에도 조건을 하나 더 걸었다가 콤보가 아예 안 나왔다 —
    //     막을 곳은 한 곳이면 충분하다.
    // ★ **기본 공격 뒤에만** 2타가 나온다. DASH·CROUCH·JUMP 뒤에는 안 나온다.
    //   ※ SelectAttack 은 m_currentAttack 이 갱신되기 **전에** 불리므로,
    //     여기서 보는 것은 아직 **직전 공격**이다.
    if (prev == PlayerState::Attack && m_currentAttack == &kDaggerLight)
        return kDaggerCombo2;

    // ③ ★ 구르기 직후 -> 대시. 「달리는 중」이 아니라 **구르기 뒤**다.
    //   달리는 중으로 두었더니 이동이 거의 항상이라 평타가 안 나왔다.
    if (prev == PlayerState::Roll)       return kDaggerDash;

    return kDaggerLight;                                          // ④ 기본
}


const AttackData& PlayerController::CurrentAttack() const
{
    return m_currentAttack ? *m_currentAttack : kDaggerLight;
}


// ----------------------------------------------------------------------------
//  ChangeState — 상태 머신의 "Enter"
//    들어가는 순간 한 번만 해야 하는 일을 모아 둔다.
//    이게 없으면 매 틱 Play() 를 부르게 되어 애니메이션이 프레임 0 에서 멈춘다.
// ----------------------------------------------------------------------------
void PlayerController::ChangeState(SceneContext& ctx, PlayerState next, bool force)
{
    if (m_state == next && !force)
        return;

    // ★ 덮기 전에 붙잡는다. 무브셋 선택이 「직전에 무엇을 하고 있었나」를 본다.
    const PlayerState prev = m_state;

    m_state      = next;
    m_stateTicks = 0;

    // ★ 예약은 **전이할 때 한 곳에서** 지운다.
    //   전에는 Attack 에 들어갈 때만 지웠는데, 예약해 둔 채 고갈(Exhausted)로
    //   빠지면 플래그가 살아남아 **다음 공격이 끝날 때 공짜 콤보**가 나갔다.
    //   구르기도 예약을 받게 되면서 그런 경로가 더 늘어난다 — 한 곳으로 모은다.
    m_comboQueued = false;

    Transform& tr = Owner().transform;

    switch (next)
    {
    case PlayerState::Idle:
        // ★ 그림은 상태가 아니라 **자세**가 정한다.
        m_sprite->Play(PostureClip(false));
        break;

    case PlayerState::Run:
        // ★ 여기서 m_stepCooldown 을 0 으로 되돌리면 안 된다 —
        //   UpdateMovement 가 이미 발소리를 내고 쿨다운을 채워 놓았고,
        //   되돌리면 1/60초 간격으로 두 번 울린다.
        m_sprite->Play(PostureClip(true));
        break;

    case PlayerState::Jump:
        // ★ 발을 떼는 소리. 착지 소리와 같은 샘플을 피치만 달리해 쓴다 —
        //   「뜬다 / 내린다」가 귀로도 구분된다.
        m_sprite->Play(kJumpClip, true);
        ctx.audio.Play("step", 0.5f, 0.35f, PanFromCanvasX(tr.x));
        break;

    case PlayerState::Attack:
    {
        // ★ 물기는 콤보에 들어가지 않는다. 무기 콤보의 일부가 아니기 때문이다.

        // ★ 어느 공격인지 **여기서 고정한다.**
        m_currentAttack = &SelectAttack(ctx, prev);
        m_biteRequested = false;          // 한 번 쓰면 지운다
        const AttackData& atk = *m_currentAttack;

        // ★ 쓰러진 채로는 서서 휘두르는 그림을 쓸 수 없다.
        //   전용 기어가며 공격 행은 아직 없어서 자세만 유지한다 —
        //   판정(crouch 상자)은 이미 낮으므로 게임은 성립한다. 그림은 6-c-5.
        m_sprite->Play(m_parts->Prone() ? kCrawlClip : atk.clip, true);
        m_hitThisSwing = false;

        // ★ 부족해도 공격은 나간다. 0 미만이면 끝난 뒤 Exhausted 로 간다.
        //   「부족하면 안 나감」이 아니라 「나가고 대가를 치름」이 이 게임의 규칙이다.
        m_stamina->Spend(atk.staminaCost);

        // 공격마다 피치를 달리해 무엇이 나갔는지 소리로도 구분되게 한다.
        const float pitch = (atk.heightFromFoot > 40.0f) ?  0.22f    // 찌르기 = 높게
                          : (atk.heightFromFoot < 20.0f) ? -0.25f    // 웅크리기 = 낮게
                          :  0.0f;
        ctx.audio.Play("swing", 0.55f, pitch + RandomPitch(0.10f), PanFromCanvasX(tr.x));

        Log::Info("[play] {} 발동  [{} {} {}]  높이 {:.0f}  stam -{}",
                  atk.name, atk.startup, atk.active, atk.recovery,
                  atk.heightFromFoot, atk.staminaCost);
        break;
    }

    case PlayerState::Roll:
    {
        m_sprite->Play(kRollClip, true);

        // ★ 방향을 여기서 고정한다. 입력이 없으면 바라보는 방향으로 굴러간다.
        const Input::MoveIntent mv = ctx.input.Move();
        m_rollDirX = (std::abs(mv.x) > kMoveEpsilon)
            ? mv.x
            : static_cast<float>(tr.facing);

        m_stamina->Spend(kRoll.staminaCost);
        ctx.audio.Play("swing", 0.4f, -0.35f, PanFromCanvasX(tr.x));  // 낮은 피치
        break;
    }

    case PlayerState::Exhausted:
        m_sprite->Play(PostureClip(false));
        ctx.audio.Play("ui_cancel", 0.45f);
        Log::Info("[play] 스태미나 고갈 — 경직 (stam {:.1f})", m_stamina->Current());
        break;

    case PlayerState::Hurt:
        // ★ 소리는 여기서 내지 않는다 — 버텼는지 휘청였는지에 따라 다르므로
        //   원인을 아는 TakeHit 가 낸다.
        m_sprite->Play(PostureClip(false), true);
        break;

    case PlayerState::Dead:
        m_sprite->Play(PostureClip(false), true);
        ctx.audio.Play("ui_cancel", 0.9f, -0.6f);
        Log::Info("[play] ★★ 플레이어 사망 — {}틱 뒤 사망 화면", kDeathScreenDelay);
        break;
    }
}


// ----------------------------------------------------------------------------
//  UpdateMovement — Idle / Run 에서만 불린다.
//    "공격 중에는 이동 불가" 를 !attacking 조건으로 흩뿌리지 않고
//    아예 호출하지 않는 것으로 표현한다. 이것이 상태 머신의 요점이다.
// ----------------------------------------------------------------------------
void PlayerController::UpdateMovement(SceneContext& ctx, float moveX)
{
    Transform& tr = Owner().transform;

    if (moveX < -kMoveEpsilon)      tr.facing = -1;
    else if (moveX > kMoveEpsilon)  tr.facing = +1;

    // ★ 속도를 **몸의 성질**로 정한다 — 「Jump 상태인가」가 아니라
    //   「발이 땅에 있는가」로 묻는다. 상태를 더 늘려도 여기는 다시 안 고친다.
    //
    //   ★ 웅크리면 느려진다 — 「다리를 노리려면 멈춰서 노려야 한다」가 한 줄로.
    //   ★ 다리가 부서지면 **쓰러져 기어간다**(design.md §3.2.2).
    //     플레이어에게 가장 무서운 부위 파괴다 — 구르기까지 잃으면
    //     §3.1 의 「흘려서 산다」가 통째로 사라진다.
    float speed = kSpeedPerTick;
    if (!m_body->Grounded())
    {
        speed *= kAirControl;   // 공중에서는 약하게만 조종된다
    }
    else
    {
        if (m_crouching)      speed *= kCrouchSpeedScale;
        if (m_parts->Prone()) speed *= kProneSpeedScale;
    }

    // ★ 가로 이동도 **몸을 거친다.** 전에는 여기서 tr.x 를 직접 썼는데,
    //   그러면 걷기만 벽을 통과한다. 화면 밖으로 못 나가게 하던 clamp 도
    //   사라졌다 — 화면 좌우 끝이 이제 **지형(벽)** 이기 때문이다.
    m_body->MoveX(moveX * speed);

    // ★ 세로는 **건드리지 않는다.** 높이를 정하는 것은 BodyComponent 다.
    //   전에는 여기서 tr.y 를 입력으로 옮겼다 — 그게 벨트스크롤이었다.

    // 발소리는 **땅에 있을 때만.** 공중에서 뛰는 소리가 나면 안 된다.
    if (std::abs(moveX) <= kMoveEpsilon || !m_body->Grounded())
    {
        m_stepCooldown = 0;
        return;
    }

    if (--m_stepCooldown <= 0)
    {
        // 웅크려 걸으면 발소리도 그만큼 뜸해야 한다.
        m_stepCooldown = m_crouching
            ? static_cast<int>(kStepIntervalTicks / kCrouchSpeedScale)
            : kStepIntervalTicks;
        ctx.audio.Play("step", 0.45f, RandomPitch(0.15f), PanFromCanvasX(tr.x));
    }
}


// ----------------------------------------------------------------------------
//  SlideDecaying — 감속하며 미끄러진다
//
//      속도
//       │▓▓▓▓▓▓
//       │▓▓▓▓
//       │▓▓
//       └────────→ 틱
//
//    ★ 구르기와 넉백이 공유한다. 카메라 흔들림 감쇠와 같은 발상이다.
// ----------------------------------------------------------------------------
void PlayerController::SlideDecaying(float dirX, float distance, int totalTicks)
{
    // ★ 공식은 Core/Motion.h 로 올렸다 — 적의 넉백이 같은 것을 쓰게 되었다.
    //   상태를 갖지 않는 계산이라 컴포넌트가 아니라 자유 함수다.
    const float step = DecayingStep(m_stateTicks, totalTicks, distance);
    if (step <= 0.0f)
        return;

    // ★ 가로만 민다. 세로는 BodyComponent 의 속도가 담당한다 —
    //   같은 축을 두 방식이 동시에 밀면 반드시 어긋난다.
    //   ★★ 구르기·넉백도 **같은 문**(MoveX)을 지난다. 하나라도 직접 옮기면
    //     그 이동만 벽을 통과해 「구르면 벽을 뚫는」 게임이 된다.
    m_body->MoveX(dirX * step);
}


// ----------------------------------------------------------------------------
//  RestingState — 행동이 끝난 뒤 돌아갈 곳
//
//    ★ 공격·구르기·경직이 끝나는 자리마다 `moving ? Run : Idle` 을 적었더니,
//      공중에서 끝났을 때 **한 틱 동안 서 있는 그림**이 나왔다.
//      한 곳으로 모으면 그런 곳이 다시 생기지 않는다.
// ----------------------------------------------------------------------------
PlayerState PlayerController::RestingState(bool moving) const
{
    if (!m_body->Grounded())
        return PlayerState::Jump;

    return moving ? PlayerState::Run : PlayerState::Idle;
}


AABB PlayerController::SpriteBounds() const
{
    const Transform& tr = Owner().transform;
    return { tr.x - kOriginX, tr.y - kOriginY, tr.x - kOriginX + kCellW, tr.y };
}


// ★ 용어를 나눠 쓴다. 섞으면 "내 공격이 나를 때리는" 코드를 쓰게 된다.
//   hurtbox = 내가 맞는 범위 (몸) / hitbox = 내가 때리는 범위 (무기)


AABB PlayerController::AttackHitbox() const
{
    const Transform& tr = Owner().transform;
    return MakeAttackBox(tr.x, tr.y, tr.facing, CurrentAttack());
}


bool PlayerController::AttackActive() const
{
    if (m_state != PlayerState::Attack)
        return false;

    const AttackData& a = CurrentAttack();
    return m_stateTicks >= a.startup
        && m_stateTicks <  a.startup + a.active;
}


bool PlayerController::RollInvincible() const
{
    if (m_state != PlayerState::Roll)
        return false;

    return m_stateTicks >= kRoll.windup
        && m_stateTicks <  kRoll.windup + kRoll.invincible;
}


bool PlayerController::Invincible() const
{
    // ② 피격 무적 — 경직(18)보다 길게(24) 남는다.
    //   그 6틱의 여유가 「경직이 풀리는 그 틱에 다시 맞는」 스턴락을 막는다.
    if (m_invulnTicks > 0)
        return true;

    return RollInvincible();   // ①
}


bool PlayerController::ConsumeDeathScreenRequest()
{
    if (m_state != PlayerState::Dead)      return false;
    if (m_deathScreenRequested)            return false;
    if (m_stateTicks < kDeathScreenDelay)  return false;

    m_deathScreenRequested = true;
    return true;
}


// ----------------------------------------------------------------------------
//  TakeHit — ★ 맞았을 때 무슨 일이 일어나는지를 **데이터가 정한다**
//
//      poise >= impact  ->  데미지만. 자리에서 버틴다 (상태를 바꾸지 않는다)
//      poise <  impact  ->  Hurt(경직) + 넉백 + 피격 무적
//
//    ※ 무적 판정은 이 함수에 오기 전에 끝나 있다(PlayScene::TryEnemyHit).
// ----------------------------------------------------------------------------
void PlayerController::TakeHit(SceneContext& ctx, const AttackData& atk, int part,
                               float fromX, float /*fromY*/)
{
    // ※ fromY 는 지금 쓰이지 않지만 인자에 남겨 둔다 —
    //   「위에서 맞으면 더 세게 눕는다」 같은 규칙이 오면 여기서 쓴다.
    const Transform& tr = Owner().transform;

    m_parts->Damage(part, atk.damage);
    m_flash = kFlashTicks;

    Log::Info("[play] {} 피격  dmg {}  남은 {}/{}",
              m_parts->Name(part), atk.damage,
              std::max(0, m_parts->Hp(part)), m_parts->MaxHp(part));

    // ---- 부위가 부서졌다 ----
    if (m_parts->IsBroken(part))
    {
        Log::Info("[play] ★ {} 절단!", m_parts->Name(part));

        // ★ 팔이 잘리면 **그 손의** 무기를 떨군다(design.md §3.2.2).
        //   다른 손이면 아무 일도 없다 — 어느 손인지가 결과를 바꾼다.
        const bool lostHand =
               (part == Part_RightArm && m_weaponHand == WeaponHand::Right)
            || (part == Part_LeftArm  && m_weaponHand == WeaponHand::Left);

        if (lostHand)
        {
            m_weaponHand          = WeaponHand::None;
            m_weaponDropRequested = true;   // 어디에 떨어뜨릴지는 Scene 이 정한다
            Log::Info("[play]   -> 무기를 떨궜다! 주우러 가야 한다");
        }
        else if (part == Part_Legs)
            Log::Info("[play]   -> 다리 상실 : 이동 대폭 감소 + 구르기 불가");
    }

    // ---- 사망이 가장 먼저다. 강인도로 버텨도 부위는 깎였다 ----
    //   머리 또는 몸통이 부서지면 즉사.
    if (m_parts->Fatal())
    {
        ctx.camera.Shake(kShakeStrength * 2.5f, kShakeTicks * 3);
        ctx.audio.Play("hit", 1.0f, -0.55f, PanFromCanvasX(tr.x));
        ChangeState(ctx, PlayerState::Dead);
        return;
    }

    // ---- ★ 경직 여부를 강인도가 정한다 ----
    //   판정 자체는 PoiseComponent 로 옮겼다 — 적도 같은 규칙을 쓰기 때문이다.
    //   나중에 게이지 방식으로 바꾸면 이 줄은 그대로 두고 그쪽만 고친다.
    if (!m_poise->WouldStagger(atk.impact))
    {
        // 버텨냈다 — **상태를 바꾸지 않는다.** 공격 중이었다면 그대로 이어진다.
        // ★ 피격 무적을 주지 않는다. 못 움직이는 구간이 없으므로 스턴락 위험이
        //   없고, 무적은 그 위험을 막기 위한 장치이기 때문이다.
        //   대신 맞을 때마다 HP 가 확실히 깎인다 — 이것이 버티기의 비용이다.
        ctx.camera.Shake(kShakeStrength * 0.6f, kShakeTicks);
        ctx.audio.Play("hit", 0.5f, -0.75f, PanFromCanvasX(tr.x));   // 둔탁하게
        Log::Info("[play] 버텨냄  poise {} >= impact {}", m_poise->Value(), atk.impact);
        return;
    }

    // ---- 휘청였다 : 넉백 방향 = 공격자 -> 나 ----
    //   ★ 수평 성분만 쓴다. 거리를 대각선으로 나눠 가지면 **뒤로 밀리는 거리가
    //     줄어**, 「밀려나서 적의 다음 사거리 밖으로 나간다」는 의도가 약해진다.
    const float dx = tr.x - fromX;
    m_knockDirX = (std::abs(dx) > 0.0001f)
        ? ((dx > 0.0f) ? 1.0f : -1.0f)
        : -static_cast<float>(tr.facing);   // 완전히 겹쳐 있으면 바라보는 반대쪽

    // ★ 살짝 뜬다. 착지까지 약 11틱이라 경직(18틱)이 끝나기 **전에** 발이 닿는다 —
    //   그래서 「경직은 풀렸는데 아직 공중」이라는 어정쩡한 순간이 생기지 않는다.
    //
    //   ★★ 수직 속도를 **덮어쓴다.** 점프로 올라가는 중에 맞으면 상승이 끊기고
    //     작게 튄 뒤 떨어진다. 더하면 그 반대가 되어 더 높이 날아오른다.
    m_body->Lift(kHurtLift);

    m_invulnTicks = kHurt.invuln;
    m_poise->OnStaggered();

    ctx.camera.Shake(kShakeStrength * 1.8f, kShakeTicks * 2);
    ctx.audio.Play("hit", 0.95f, -0.25f, PanFromCanvasX(tr.x));
    Log::Info("[play] 휘청  poise {} < impact {}   경직 {}틱 / 무적 {}틱",
              m_poise->Value(), atk.impact, kHurt.ticks, kHurt.invuln);

    ChangeState(ctx, PlayerState::Hurt);
}


void PlayerController::Tick(SceneContext& ctx, bool consumeEdgeInput)
{
    // ---- 시간은 상태와 무관하게 매 틱 흐른다 ----
    ++m_stateTicks;

    if (m_flash > 0)        --m_flash;
    if (m_invulnTicks > 0)  --m_invulnTicks;

    const Input::MoveIntent move = ctx.input.Move();

    // ★ 「움직이는 중」은 이제 **가로만** 본다. 세로 입력은 몸을 움직이지 않는다 —
    //   ↑ 를 누른 채 서 있는데 RUN 으로 보이면 안 된다.
    const bool moving = (std::abs(move.x) > kMoveEpsilon);

    // 웅크리기는 **지속 입력**이라 엣지가 아니다. 매 틱 물어봐도 된다.
    m_crouching = ctx.input.CrouchHeld();

    // ★ 자세를 몸에게 알려 준다. 판정 상자가 여기서 낮아진다.
    //   엎드리기는 몸이 스스로 알지만(다리가 부서졌다), 웅크리기는
    //   키를 누르는 동안만이라 알려 주지 않으면 몸이 알 방법이 없다.
    m_parts->SetCrouching(Crouched());

    // ★ 자세가 바뀌면 **그림도** 바꾼다.
    //   Ctrl 은 상태를 바꾸지 않으므로 ChangeState 가 안 불린다.
    //   그냥 두면 「웅크렸는데 서 있는 그림」이 되어, 방금 고친 것과
    //   똑같은 어긋남이 반대 방향으로 생긴다.
    if (Crouched() != m_crouchedLast)
    {
        m_crouchedLast = Crouched();

        // 서 있거나 걷는 중에만. 공격·구르기 도중에 그림을 갈아치우면
        // 프레임 데이터와 그림이 어긋난다.
        if (m_state == PlayerState::Idle || m_state == PlayerState::Run)
            m_sprite->Play(PostureClip(m_state == PlayerState::Run), true);
    }

    const bool attackPressed = (consumeEdgeInput && ctx.input.AttackPressed());
    const bool jumpPressed   = (consumeEdgeInput && ctx.input.JumpPressed());
    const bool rollPressed   = (consumeEdgeInput && ctx.input.RollPressed());
    const bool bitePressed   = (consumeEdgeInput && ctx.input.BitePressed());

    switch (m_state)
    {
    case PlayerState::Idle:
    case PlayerState::Run:
        UpdateMovement(ctx, move.x);

        // ★ 「발밑이 없어졌다」를 입력보다 **먼저** 본다.
        //   넉백으로 떠올랐을 때도, 나중에 발판 끝에서 걸어 나갔을 때도(6-d)
        //   이 한 줄이 처리한다 — **원인을 묻지 않고 몸의 상태만 본다.**
        if (!m_body->Grounded())
        {
            ChangeState(ctx, PlayerState::Jump);
        }
        else if (jumpPressed && CanJump())
        {
            m_body->Jump(kJumpSpeed);
            ChangeState(ctx, PlayerState::Jump);
        }
        // 구르기를 공격보다 먼저 본다. 둘이 동시에 눌리면 회피가 우선이다.
        // ★ 다리가 부서지면 구를 수 없다 — 회피 수단을 통째로 잃는다.
        else if (rollPressed && CanRoll())
        {
            ChangeState(ctx, PlayerState::Roll);
        }
        else if (bitePressed)
        {
            // ★ 물기는 CanAttack() 을 보지 않는다 — 무기가 필요 없기 때문이다.
            m_biteRequested = true;
            ChangeState(ctx, PlayerState::Attack, true);
        }
        else if (attackPressed && m_pickupAvailable)
        {
            // ★ 발밑에 주울 것이 있으면 Space 는 **줍기**가 된다.
            //   맥락이 같은 입력의 뜻을 바꾸는 것 — 무브셋에서 이미 쓴 방식이다.
            //   못 줍는 상황(양팔 절단)에서는 이 갈래로 안 들어와 공격이 살아난다.
            m_pickupRequested = true;
        }
        else if (attackPressed && CanAttack())
        {
            ChangeState(ctx, PlayerState::Attack);
        }
        else                    ChangeState(ctx, RestingState(moving));
        break;

    // ========================================================================
    //  Jump — 공중
    //    ★ 「올라가는 중」과 「떨어지는 중」을 나누지 않는다. 규칙이 같기 때문이다.
    //      나눠야 할 이유(다른 조작 · 다른 판정)가 생기면 그때 쪼갠다.
    // ========================================================================
    case PlayerState::Jump:
        UpdateMovement(ctx, move.x);   // 공중 제어력은 UpdateMovement 가 안다

        // ★ 공중에서는 **줍기 갈래가 없다.** 그래서 발밑에 무기가 있어도
        //   뛰어넘으며 주워지지 않는다 — Space 를 점프로 옮긴 대가를 여기서 치른다.
        if (bitePressed)
        {
            m_biteRequested = true;
            ChangeState(ctx, PlayerState::Attack, true);
        }
        else if (attackPressed && CanAttack())
        {
            ChangeState(ctx, PlayerState::Attack);
        }
        else if (m_body->Grounded())
        {
            // ★ 착지도 「상태가 끝났다」가 아니라 **「발이 닿았다」**로 판정한다.
            ctx.audio.Play("step", 0.6f, -0.35f,
                           PanFromCanvasX(Owner().transform.x));
            ChangeState(ctx, RestingState(moving));
        }
        break;

    case PlayerState::Attack:
    {
        // ★ 이동 입력을 처리하지 않는다 = 공격 중에는 못 움직인다.
        //   소울류의 "한 번 휘두르면 끝까지 간다" 감각.
        const AttackData& atk = CurrentAttack();

        // ---- 콤보 예약 ----
        //   ★ active 가 끝난 뒤부터만 받는다. startup 중에도 받으면 연타만으로
        //     2타가 확정되어 「1타를 내보고 이어칠지 판단한다」가 사라진다.
        //   ★ 예약해 두었다가 상태가 끝날 때 꺼낸다 — 지금 전이하면 1타의 후딜을
        //     건너뛴다.
        //
        //   ★★ **이어지는 것은 기본 공격뿐이다.** DASH·CROUCH·JUMP 뒤에는
        //     예약 자체가 안 걸린다. 이 한 줄이 「2타째인가」를 세던 카운터
        //     (m_comboStep)를 통째로 지웠다 —
        //     THRUST 는 m_currentAttack != &kDaggerLight 라 스스로 못 잇는다.
        //     **세는 대신 물으면 셀 것이 없어진다.**
        if (attackPressed && CanAttack() && m_currentAttack == &kDaggerLight
            && m_stateTicks >= atk.startup + atk.active)
        {
            m_comboQueued = true;
        }

        // ※ 적을 실제로 때렸는지는 PlayScene 이 검사한다(헤더 주석 참조).

        // ★ 상태의 길이는 애니메이션이 아니라 프레임 데이터가 정한다.
        if (m_stateTicks >= atk.TotalTicks())
        {
            // ★ 고갈이 콤보보다 우선한다. 「2타를 예약해 두면 고갈을 피한다」가
            //   되면 스태미나 시스템에 구멍이 생긴다.
            if (m_stamina->Depleted())
                ChangeState(ctx, PlayerState::Exhausted);
            else if (m_comboQueued && CanAttack())
                // ★ force = true. Attack -> Attack 이라 「같은 상태면 무시」에 걸린다.
                ChangeState(ctx, PlayerState::Attack, true);
            else
                ChangeState(ctx, RestingState(moving));
        }
        break;
    }

    case PlayerState::Roll:
        // ★ 이동 입력을 처리하지 않는다. 시작할 때 고정한 방향으로만 간다.
        SlideDecaying(m_rollDirX, kRoll.distance, kRoll.TotalTicks());

        // ---- ★ 구르기 -> 대시 공격 예약 ----
        //   공격 콤보의 예약과 **같은 모양**이다: 「위험 구간이 끝난 뒤부터」.
        //     공격은 active 가 끝난 뒤부터,  구르기는 **무적이 끝난 뒤부터.**
        //   무적 구간에서도 받으면 「구르면서 공격 확정」이 되어
        //   「굴러서 빠져나갈까, 붙어서 칠까」라는 판단이 사라진다.
        if (attackPressed && CanAttack()
            && m_stateTicks >= kRoll.windup + kRoll.invincible)
        {
            m_comboQueued = true;
        }

        if (m_stateTicks >= kRoll.TotalTicks())
        {
            // 고갈이 콤보보다 우선한다 — 공격이 끝날 때와 같은 순서다.
            if (m_stamina->Depleted())
                ChangeState(ctx, PlayerState::Exhausted);
            else if (m_comboQueued && CanAttack())
                ChangeState(ctx, PlayerState::Attack);
            else
                ChangeState(ctx, RestingState(moving));
        }
        break;

    case PlayerState::Exhausted:
        // ★ 아무 입력도 처리하지 않는다. 완전히 무방비.
        //   상태 머신 덕분에 이 한 줄이 「모든 입력에 !exhausted 붙이기」를 대신한다.
        if (!m_stamina->Depleted())
            ChangeState(ctx, RestingState(moving));
        break;

    case PlayerState::Hurt:
        // ★ 입력을 처리하지 않는다 — Exhausted 와 같은 구조. 대신 밀려난다.
        SlideDecaying(m_knockDirX, kHurt.knockback, kHurt.ticks);

        if (m_stateTicks >= kHurt.ticks)
        {
            // ★ 「고갈 경직 중에 맞으면 경직이 풀린다」를 막는 코드가 여기 있었는데,
            //   고갈 래치를 StaminaComponent 로 옮기면서 **필요 없어졌다.**
            //   Depleted() 는 25 이상 회복될 때까지 참으로 남으므로,
            //   경직에서 나와도 고갈이면 그대로 Exhausted 로 돌아간다.
            //   상태를 올바른 곳에 두면 방어 코드가 저절로 사라진다.
            if (m_stamina->Depleted())
                ChangeState(ctx, PlayerState::Exhausted);
            else
                ChangeState(ctx, RestingState(moving));
        }
        break;

    case PlayerState::Dead:
        // 사망 화면 요청은 PlayScene 이 ConsumeDeathScreenRequest 로 가져간다.
        break;
    }

    // ---- 상태와 무관한 입력 ----
    //   ★ 임시 키. 강인도의 두 분기를 눈으로 비교하기 위한 것이다.
    //     같은 공격에 CLOTH 는 튕겨나가고 PLATE 는 그대로 서서 휘두른다.
    if (consumeEdgeInput && ctx.input.ArmorSwapPressed())
    {
        m_armorIndex = (m_armorIndex + 1) % kArmorCount;
        m_poise->SetValue(Armor().poise);   // 방어구가 바뀌면 강인도도 바뀐다
        ctx.audio.Play("ui_confirm", 0.5f);
        Log::Info("[play] 갑옷 → {}  (poise {})   swing impact 18 / bite impact 12",
                  Armor().name, Armor().poise);
    }

    // ★ 임시 디버그 키(F4) — 다리를 부러뜨렸다 되돌린다.
    //
    //   엎드린 자세의 판정 상자와 「엎드리면 중단을 흘린다」를 **바로** 볼 수
    //   있게 하려는 것이다. 실제로 부러뜨리려면 잡몹의 물기를 네 번 맞아야 해서
    //   확인 한 번에 십수 초가 걸린다. 만든 사람이 확인하기 어려운 기능은
    //   있어도 없는 것과 같다.
    //
    //   ※ 죽은 뒤에는 받지 않는다. RestingState 로 되돌리면 되살아나 버린다.
    if (consumeEdgeInput && ctx.input.LegBreakPressed() && !IsDead())
    {
        if (m_parts->LegsBroken()) m_parts->Restore(Part_Legs);
        else                       m_parts->Damage(Part_Legs, m_parts->MaxHp(Part_Legs));

        // ★ 자세가 바뀌었으니 그림을 다시 건다.
        //   상자는 Prone() 을 매번 보므로 즉시 바뀌지만, 애니메이션은
        //   **전이할 때만** 갈린다 — 그래서 force 로 한 번 흔들어 준다.
        ChangeState(ctx, RestingState(moving), true);

        Log::Info("[play] (F4) 다리 {} — 엎드림 {}",
                  m_parts->LegsBroken() ? "파괴" : "복구",
                  m_parts->Prone() ? "ON" : "OFF");
    }
}


void PlayerController::Render(Renderer&)
{
    // ★ 그리는 것은 SpriteComponent 다. 여기서는 **표현만 넘긴다.**
    DirectX::XMVECTOR tint = DirectX::Colors::White;
    if (IsDead())
        tint = DirectX::XMVectorSet(0.30f, 0.28f, 0.30f, 1.0f);   // 사망 = 회색
    else if (m_state == PlayerState::Exhausted)
        tint = DirectX::XMVectorSet(0.45f, 0.45f, 0.55f, 1.0f);   // 지쳐서 어둡게
    else if (RollInvincible())
        tint = DirectX::XMVectorSet(0.55f, 0.75f, 1.00f, 1.0f);   // ① 구르기 무적 = 푸르게
    else if (m_flash > 0)
        tint = DirectX::XMVectorSet(1.00f, 0.35f, 0.30f, 1.0f);   // 피격 순간 = 붉게
    else if (m_invulnTicks > 0 && (m_invulnTicks / 3) % 2 == 0)
        // ★ ② 피격 무적 = 깜빡임. ①과 다른 표현을 쓰는 이유는 성격이 달라서다 —
        //   푸른색은 「벌어낸 무적」, 깜빡임은 「봐주는 무적」.
        tint = DirectX::XMVectorSet(1.00f, 1.00f, 1.00f, 0.45f);

    m_sprite->SetTint(tint);

    // ※ 웅크린 자세를 세로로 눌러서 표현하던 코드가 여기 있었다.
    //   **전용 그림(10행 · 4행)이 생기면서 사라졌다.**
    //   눌러서 만든 자세는 판정 상자를 데리고 오지 못한다 — 그림만 낮아진다.

    // ★ 틴트·눌림과 같은 「표현 넘기기」다. 매 틱이 아니라 매 프레임 한 번이면
    //   충분하다 — 잘린 팔은 상태에서 **파생**되는 것이라 따로 기억할 게 없다.
    UpdateArmLayers();
}


void PlayerController::RenderDebug(Renderer& renderer)
{
    if (!renderer.DebugDraw())
        return;

    // ★ 맞는 범위를 여기서 그리지 않는다 — **PartsComponent 가 그린다.**
    //   전에는 여기서 고정 크기 사각형을 하나 더 그렸는데, 자세를 따라가지
    //   않아서 「엎드렸는데 상자는 서 있는」 거짓 표시가 되었다.
    //   같은 것을 두 곳에서 그리면 언젠가 한쪽이 거짓말을 한다.
    //
    //   대신 **무적 여부를 바깥 테두리 색**으로 옮겼다. 정보는 남기고
    //   거짓말만 지운다.
    DirectX::XMVECTOR frameColor = DirectX::Colors::SlateGray;
    if (RollInvincible())       frameColor = DirectX::Colors::DeepSkyBlue;  // ① 구르기
    else if (m_invulnTicks > 0) frameColor = DirectX::Colors::Yellow;       // ② 피격
    renderer.DrawRectOutline(SpriteBounds(), frameColor,
                             (RollInvincible() || m_invulnTicks > 0) ? 2.0f : 1.0f);

    // ★ 공격 히트박스 — active 구간에서만 나타난다.
    //   , 로 멈추고 . 로 밟으면 t8 에 나타나 t10 까지 있는 것을 볼 수 있다.
    if (AttackActive())
    {
        renderer.DrawFilledRect(AttackHitbox(),
            DirectX::XMVectorSet(1.0f, 0.2f, 0.2f, 0.35f));
        renderer.DrawRectOutline(AttackHitbox(), DirectX::Colors::Red, 2.0f);
    }

    // 원점(발밑)을 십자로
    const Transform& tr = Owner().transform;
    renderer.DrawFilledRect({ tr.x - 5.0f, tr.y - 1.0f, tr.x + 5.0f, tr.y + 1.0f },
                            DirectX::Colors::Magenta);
    renderer.DrawFilledRect({ tr.x - 1.0f, tr.y - 5.0f, tr.x + 1.0f, tr.y + 5.0f },
                            DirectX::Colors::Magenta);
}


void PlayerController::RenderUI(Renderer& renderer)
{
    // ---- ★ 몸 그림 (HP 바를 대신한다, design.md §3.2.3) ----
    //   좌하단. 부위 HP 가 낮을수록 붉어지고, 잘리면 테두리만 남는다.
    m_parts->DrawBodyDiagram(renderer, 12.0f, Config::kCanvasHeight - 78.0f);

    if (renderer.DebugDraw())
        m_parts->DrawHpList(renderer, 48.0f, Config::kCanvasHeight - 78.0f);

    // ★ 무기를 잃었다는 것은 **반드시 보여야 한다.**
    //   「왜 공격이 안 되지」를 플레이어가 추측하게 두면 안 된다.
    if (m_pickupAvailable)
    {
        // 주울 수 있을 때만 뜬다. 항상 떠 있으면 아무도 안 읽는다.
        renderer.DrawString("SPACE : PICK UP",
                            12.0f, Config::kCanvasHeight - 92.0f,
                            DirectX::Colors::Gold, 1);
    }
    else if (!CanAttack())
    {
        // ★ 「할 수 있는 것」을 같이 알려 준다. 못 하는 것만 말하면 막힌 느낌이 든다.
        renderer.DrawString("NO WEAPON - RMB TO BITE",
                            12.0f, Config::kCanvasHeight - 92.0f,
                            DirectX::Colors::Crimson, 1);
    }

    // ---- 방어구 ----
    //   ★ F2 로 바뀌는 값이므로 **항상** 보여야 한다.
    //     안 보이면 「같은 공격에 왜 이번엔 안 밀렸지?」를 확인할 수 없다.
    renderer.DrawString(
        std::format("ARMOR {} (poise {})  WEAPON {}", Armor().name, Armor().poise,
                    m_weaponHand == WeaponHand::Right ? "R.HAND"
                  : m_weaponHand == WeaponHand::Left  ? "L.HAND" : "-none-"),
        12.0f, Config::kCanvasHeight - 52.0f, DirectX::Colors::SlateGray, 1);

    // ---- 상태 ----
    //   ★ 상태 머신을 눈으로 보기 위한 표시.
    //     , 로 멈추고 . 로 밟으며 t 를 세면 프레임 데이터를 직접 확인할 수 있다.
    if (m_state == PlayerState::Attack)
    {
        const AttackData& a = CurrentAttack();
        renderer.DrawString(
            std::format("{}{}  t{:<3}{}   [{} {} {}]  h{:.0f}{}",
                        a.name, (m_currentAttack == &kDaggerCombo2) ? " (2nd)" : "",
                        m_stateTicks, AttackPhase(m_stateTicks, a),
                        a.startup, a.active, a.recovery, a.heightFromFoot,
                        m_comboQueued ? "  >> NEXT" : ""),
            6.0f, 6.0f,
            AttackActive() ? DirectX::Colors::Red : DirectX::Colors::Orange, 1);
    }
    else if (m_state == PlayerState::Roll)
    {
        renderer.DrawString(
            std::format("STATE ROLL  t{:<3}{}   [{} {} {}]",
                        m_stateTicks, RollPhase(m_stateTicks, kRoll),
                        kRoll.windup, kRoll.invincible, kRoll.recovery),
            6.0f, 6.0f,
            RollInvincible() ? DirectX::Colors::DeepSkyBlue : DirectX::Colors::Orange, 1);
    }
    else if (m_state == PlayerState::Hurt)
    {
        // ★ 경직 18틱 / 무적 24틱이 **둘 다** 보여야 한다.
        renderer.DrawString(
            std::format("STATE HURT  t{:<3}stagger {}   invuln {}",
                        m_stateTicks, kHurt.ticks, m_invulnTicks),
            6.0f, 6.0f, DirectX::Colors::Yellow, 1);
    }
    else
    {
        renderer.DrawString(
            std::format("STATE {}{}  t{}{}", PlayerStateName(m_state),
                        m_crouching ? " (CROUCH)" : "", m_stateTicks,
                        (m_invulnTicks > 0) ? std::format("   invuln {}", m_invulnTicks)
                                            : std::string{}),
            6.0f, 6.0f,
            (m_state == PlayerState::Exhausted || IsDead()) ? DirectX::Colors::Red
                                                            : DirectX::Colors::Orange, 1);
    }
}
