#include "Scenes/PlayScene.h"
#include "Core/GameObject.h"
#include "Gameplay/EnemyBrain.h"
#include "Gameplay/PartsComponent.h"
#include "Graphics/SpriteComponent.h"
#include "Scenes/DeathScene.h"
#include "Scenes/PauseScene.h"

#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Audio/Audio.h"
#include "Graphics/Assets.h"
#include "Graphics/Camera.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

#include <DirectXColors.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <memory>
#include <random>
#include <string>

namespace
{
    // ---- 스프라이트시트 배치 (64×64 셀) ----
    //   player.png : 6 열 × 4 행  idle / run / attack / roll
    //   enemy.png  : 6 열 × 4 행  idle / crawl / swing / bite
    //
    //   적 시트는 tools/gen_enemy_attack.ps1 로 만든다.
    //   ★ 스크립트를 저장소에 남겨 뒀다 — 이전 에셋들은 스크립트가 대화에만
    //     있어서 다시 만들 수 없게 됐다. 같은 일을 반복하지 않기 위해서다.
    constexpr int kCellW = 64;
    constexpr int kCellH = 64;

    // ---- 애니메이션 클립 ----
    //   ticksPerFrame 이 애니메이션 속도. 60 / n = 애니메이션 fps.
    //   ★ Attack 은 loop = false 다. 끝나면 Finished() 가 true 가 되어
    //     상태를 되돌리는 신호가 된다.
    constexpr AnimationClip kIdleClip   { /*row*/ 0, /*frames*/ 4, /*ticks*/ 10, /*loop*/ true  };  //  6fps
    constexpr AnimationClip kRunClip    { /*row*/ 1, /*frames*/ 6, /*ticks*/  5, /*loop*/ true  };  // 12fps
    //   ※ 공격 클립은 이제 AttackData 안에 있다 — 공격마다 길이가 다르기 때문이다.
    constexpr AnimationClip kRollClip   { /*row*/ 3, /*frames*/ 6, /*ticks*/  4, /*loop*/ false };  // 15fps
    //   구르기는 24틱, 상태는 26틱 — 마지막 프레임(일어남)이 2틱 더 유지된다.
    //   상태 길이는 프레임 데이터가 정하고 애니메이션이 거기에 맞춘다는 원칙 그대로다.

    // ============================================================================
    //  단검의 무브셋 — 무기 하나 = 공격 여러 개
    //
    //    ★ 기획서 3.2.1 이 요구한 것이 이 표다.
    //      「무기마다 모션이 다르고, 모션에 따라 닿는 부위가 다르다」
    //
    //      heightFromFoot 하나가 부위를 정한다(겹침 면적이 큰 쪽에 맞으므로).
    //      적의 부위 상자는 발밑 기준으로 머리 -55..-37 / 몸통 -37..-18 / 다리 -18..0 이다.
    //
    //        공격      높이   닿는 부위   비고
    //        light      34     몸통       기본
    //        crouch     10     다리       Ctrl. 느리고 약한 대신 **조준할 수 있다**
    //        running    34     몸통       달리다 치면. 길게 뻗지만 비싸다
    //        combo2     48     머리       1타를 맞춘 뒤에만. 머리는 즉사다
    //
    //    ★ 「머리를 노리려면 콤보를 성공시켜야 한다」가 데이터만으로 성립한다.
    //      새 규칙을 한 줄도 안 썼는데 리스크/보상이 생긴다.
    //
    //    ★ 스태미나 경제(기획서 3.1):
    //      light(28) + combo2(34) = 62. 100 에서 62를 쓰면 38 이 남는데
    //      구르기가 30 이므로 **콤보 뒤에는 한 번밖에 못 구른다.**
    //      「욕심내서 2타를 넣을까, 남겨서 구를까」가 매 순간의 질문이 된다.
    //
    //    ★ clip 은 frames × ticks == TotalTicks 가 되도록 맞춰 두었다.
    //      안 맞으면 모션이 잘리거나 남는다.
    //      (startup / ticksPerFrame 을 정수로 맞추는 것은 **적 공격**에서 더 중요하다 —
    //       그쪽은 플레이어가 예고를 읽어야 하므로 그림과 판정이 어긋나면 안 된다)
    //
    //    ★ 6-c 에서 이 네 덩어리가 그대로 weapons.json 이 된다.
    // ============================================================================
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
        /*height*/   16.0f,         // 낮고 얇다
        /*heightFromFoot*/ 10.0f,   // ★ 다리 상자(-18..0) 한가운데
        /*damage*/   10,            // 약하다 — 조준의 대가
        /*staminaCost*/ 26,
        /*impact*/   14,
        /*clip*/     { /*row*/ 4, 6, 5, false },   // ★ 전용 행 — 웅크려 낮게
    };

    constexpr AttackData kDaggerRunning{
        /*name*/     "RUN",
        /*startup*/  8,
        /*active*/   4,
        /*recovery*/ 12,            // 합계 24 = 6프레임 × 4틱
        /*reach*/    18.0f,         // 멀리서 닿는다
        /*width*/    34.0f,
        /*height*/   24.0f,
        /*heightFromFoot*/ 34.0f,   // 몸통
        /*damage*/   14,
        /*staminaCost*/ 34,         // 비싸다. 달리다 치면 스태미나가 빨리 마른다
        /*impact*/   16,
        /*clip*/     { /*row*/ 6, 6, 4, false },   // ★ 전용 행 — 크게 앞으로
    };

    constexpr AttackData kDaggerCombo2{
        /*name*/     "THRUST",
        /*startup*/  8,
        /*active*/   3,
        /*recovery*/ 13,            // 합계 24 = 6프레임 × 4틱
        /*reach*/    12.0f,
        /*width*/    26.0f,
        /*height*/   22.0f,
        /*heightFromFoot*/ 48.0f,   // ★ 머리 상자(-55..-37) 한가운데
        /*damage*/   15,
        /*staminaCost*/ 34,
        /*impact*/   18,
        /*clip*/     { /*row*/ 5, 6, 4, false },   // ★ 전용 행 — 머리 높이로 찌른다
    };

    // 콤보를 예약할 수 있는 구간 = active 가 끝난 뒤부터 상태가 끝날 때까지.
    // ★ 「1타가 실제로 나간 뒤에만 다음을 예약할 수 있다」는 격투게임의 관례다.
    //   startup 중에 예약을 받으면 공격키 연타만으로 2타가 확정되어
    //   「1타를 맞추고 이어친다」는 판단이 사라진다.

    // 웅크리면 느려진다. 조준의 대가이자 「멈춰서 노린다」는 감각을 만든다.
    constexpr float kCrouchSpeedScale = 0.45f;

    // ---- 구르기 프레임 데이터 ----
    constexpr RollData kRoll{
        /*windup*/      4,
        /*invincible*/ 12,
        /*recovery*/   10,   // 합계 26 틱 = 0.43 초
    };

    // ---- 피격 데이터 ----
    //   ★ invuln(24) > ticks(18). 같게 두면 경직이 풀리는 그 틱에 다시 맞는다.
    constexpr HurtData kHurt{
        /*ticks*/     18,
        /*invuln*/    24,
        /*knockback*/ 22.0f,
    };

    // ---- 플레이어 HP ----
    constexpr int kPlayerMaxHp = 100;

    // ★ 죽은 뒤 사망 화면이 뜨기까지의 한 박자.
    //   즉시 덮으면 **무엇에 죽었는지가 보이지 않아** 플레이어가 배울 수 없다.
    //   적이 마지막으로 휘두른 그림이 남아 있어야 「아, 저 공격이었구나」가 된다.
    constexpr int kDeathScreenDelay = 45;   // 0.75 초

    // ---- 갑옷 (임시. F2 로 갈아입어 강인도의 효과를 비교한다) ----
    //   적 공격의 impact 는 swing 18 / bite 12 다.
    //     CLOTH(10) → 둘 다에 휘청인다
    //     PLATE(24) → 둘 다 버텨낸다 (대신 HP 로 지불한다)
    //
    //   ★ 이 두 줄이 6단계에서 armors.json 이 된다.
    //     그때 무게(구르기 속도)와 방어력이 같은 자리에 추가된다.
    constexpr ArmorData kArmors[] = {
        { "CLOTH", 10 },
        { "PLATE", 24 },
    };
    constexpr int kArmorCount = static_cast<int>(std::size(kArmors));

    // ---- 플레이어 (임시) ----
    //   ★ 좌표와 속도는 모두 캔버스 해상도(640x360) 기준이다.
    constexpr float kPlayerSpeedPerSec  = 150.0f;
    constexpr float kPlayerSpeedPerTick = kPlayerSpeedPerSec / 60.0f;   // 틱당 2.5 픽셀

    // ---- 원점(피벗) : 스프라이트 안에서 "발밑 가운데" ----
    constexpr float kOriginX = kCellW * 0.5f;                 // 32
    constexpr float kOriginY = static_cast<float>(kCellH);    // 64 = 셀의 아래 끝

    // ---- 히트박스 : 발밑 기준의 상대 좌표 ----
    constexpr float kHitHalfWidth = 10.0f;
    constexpr float kHitHeight    = 44.0f;
    constexpr float kHitFootGap   = 2.0f;

    constexpr float kMoveEpsilon = 0.01f;

    constexpr int kStepIntervalTicks = 15;   // 0.25 초
    constexpr int kFlashTicks        = 9;    // 0.15 초 (피격 표현. 지금은 미사용)

    constexpr float kShakeStrength = 2.0f;
    constexpr int   kShakeTicks    = 8;

    // ---- 스태미나 ----
    //   공격 28 이므로 100 으로 3 번은 되고 4 번째에 고갈된다.
    //   「세 번은 되고 네 번은 안 된다」가 몸으로 익혀지는 배치.
    constexpr float kStaminaMax          = 100.0f;
    constexpr float kStaminaRegenPerTick = 0.9f;   // 초당 54
    constexpr int   kStaminaRegenDelay   = 36;     // 0.6 초. 행동할 때마다 초기화된다
    constexpr float kStaminaExhaustExit  = 25.0f;  // 이 이상 회복되면 경직 해제

    //   ★ 회복 지연이 없으면 공격하는 동안에도 회복되어 스태미나가 의미를 잃는다.
    //     지연(36틱)이 공격 길이(24틱)보다 길어서 연속 공격 중에는 전혀 안 찬다.

    float RandomPitch(float spread)
    {
        static std::mt19937 rng{ 12345 };
        std::uniform_real_distribution<float> dist(-spread, spread);
        return dist(rng);
    }

    const char* StateName(PlayerState s)
    {
        switch (s)
        {
        case PlayerState::Run:       return "RUN";
        case PlayerState::Attack:    return "ATTACK";
        case PlayerState::Roll:      return "ROLL";
        case PlayerState::Exhausted: return "EXHAUSTED";
        case PlayerState::Hurt:      return "HURT";
        case PlayerState::Dead:      return "DEAD";
        default:                     return "IDLE";
        }
    }

    // 지금 공격의 어느 구간인가. 화면에 찍어서 프레임 데이터를 눈으로 확인한다.
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


bool PlayScene::Enter(SceneContext& ctx)
{
    auto playerSheet = ctx.assets.Texture(L"assets/textures/player.png");
    auto enemySheet  = ctx.assets.Texture(L"assets/textures/enemy.png");
    if (!playerSheet || !enemySheet)
        return false;

    // ========================================================================
    //  ★ 조립 — 이 몇 줄이 「합성」의 전부다
    //
    //    상속 계층을 짜지 않는다. 필요한 능력을 붙일 뿐이다.
    //      플레이어 = [Sprite]
    //      적       = [Parts] [EnemyBrain] [Sprite]
    //
    //    ★ 붙인 순서 = 실행 순서다. 그래서 이 코드가 곧 실행 순서표이고
    //      따로 외울 것이 없다.
    //      Brain 이 Sprite 보다 먼저여야 이번 틱에 바꾼 클립이 같은 틱에 반영된다.
    // ========================================================================
    m_playerSprite = &m_playerObj.Add<SpriteComponent>(playerSheet, kCellW, kCellH);

    m_enemyParts = &m_enemyObj.Add<PartsComponent>();
    m_enemyBrain = &m_enemyObj.Add<EnemyBrain>(m_playerObj.transform);
    m_enemyObj.Add<SpriteComponent>(enemySheet, kCellW, kCellH);

    // Start 는 **전부 붙은 뒤**에 부른다 — 컴포넌트들이 서로를 찾는 시점이다.
    m_playerObj.Start(ctx);
    m_enemyObj.Start(ctx);

    // ★ 초기화를 Respawn 에 맡긴다. 초기화와 부활은 같은 일이다.
    //   여기서 따로 초기화하면 나중에 필드를 추가할 때 한쪽만 고치게 되고,
    //   「두 번째 판부터 뭔가 이상하다」는 재현하기 어려운 버그가 된다.
    Respawn(ctx);

    Log::Info("[play] Arrows/WASD/Stick = move   Space = attack   Shift = roll   Esc = pause");
    Log::Info("[play] F1 = hitbox   F2 = swap armor   F3 = stats");
    Log::Info("[play] ,  = freeze    . = step 1 tick    / = slow motion (1/8)");
    Log::Info("[play] TIP: , 로 멈춘 뒤 Space 를 누르고 . 로 한 틱씩 밟으면");
    Log::Info("[play]      공격이 몇 틱짜리인지 눈으로 셀 수 있다");
    Log::Info("[play] TIP: 적 머리 위 `!` 가 예고다. 그동안 Shift 로 구르면 흘린다");
    Log::Info("[play] TIP: F2 로 PLATE(poise 24) 를 입으면 경직 없이 버텨낸다");
    Log::Info("[play]      — 대신 HP 로 지불한다");
    return true;
}


// ----------------------------------------------------------------------------
//  Respawn — 자세한 설명은 헤더의 선언부 주석 참조
// ----------------------------------------------------------------------------
void PlayScene::Respawn(SceneContext& ctx)
{
    // ---- 되돌아간다 : 플레이어 ----
    //   구조체를 통째로 기본값으로 되돌린다. 필드를 하나씩 적으면
    //   나중에 추가한 필드를 빠뜨린다 — 그것이 이 함수에서 가장 흔한 버그다.
    m_player  = Player{};
    m_stamina = Stamina{};
    m_player.hp = kPlayerMaxHp;

    m_invulnTicks           = 0;
    m_hitThisSwing          = false;
    m_currentAttack         = nullptr;
    m_comboStep             = 0;
    m_comboQueued           = false;
    m_crouching             = false;
    m_stepCooldown          = 0;
    m_stateBeforeHurt       = PlayerState::Idle;
    m_deathScreenRequested  = false;

    PlayerTr().x = 120.0f;
    PlayerTr().y = 260.0f;
    PlayerTr().facing = 1;

    // ---- 되돌아간다 : 적 ----
    //   ★ 적이 되살아나는 것은 확정된 설계다(design.md §3.6.1).
    //     ★ 이제 **한 줄**이다 — 무엇을 되돌릴지는 적 자신이 안다.
    //       PlayScene 이 적의 부위 배열도 쿨다운도 알 필요가 없어졌다.
    m_enemyBrain->Reset(ctx);

    // ---- 남는다 ----
    //   ★ m_armorIndex (장착 방어구) 는 **일부러 되돌리지 않는다.**
    //     장비는 죽어도 그대로다 — 이 표의 오른쪽 칸에 실제로 들어간 첫 항목이다.
    //     F2 로 PLATE 를 입고 죽어 보면 부활 뒤에도 PLATE 인 것을 확인할 수 있다.
    //     (이걸 되돌리면 「죽을 때마다 장비가 벗겨지는」 게임이 된다)
    //
    //   7단계에서 열어둔 문·얻은 아이템·기도 포인트가 여기 함께 온다.
    //   위의 두 묶음과 이 자리를 나눠 둔 것 자체가 설계다.

    // ★ ChangeState / ChangeEnemyState 를 쓰지 않는다.
    //   둘 다 「같은 상태로의 전이는 무시」한다(의도된 최적화다 — 그게 없으면
    //   매 틱 Play() 가 불려 애니메이션이 프레임 0 에서 멈춘다).
    //   그런데 리셋은 「이미 Idle 인데 Idle 로 만들어야」 하므로 정확히 그
    //   최적화에 걸려 아무 일도 일어나지 않는다.
    //   그래서 상태를 직접 놓고 애니메이션은 forceRestart 로 다시 건다.
    m_state      = PlayerState::Idle;
    m_stateTicks = 0;
    m_playerSprite->Play(kIdleClip, true);
    m_playerSprite->ClearTint();
    m_playerSprite->SetScale(1.0f, 1.0f);
}


// ----------------------------------------------------------------------------
//  Resume — 위에 있던 Scene 이 닫혔다
// ----------------------------------------------------------------------------
void PlayScene::Resume(SceneContext& ctx)
{
    // ★ PauseScene 이 닫힌 경우와 DeathScene 이 닫힌 경우를 **상태로 구분한다.**
    //   덕분에 DeathScene 은 PlayScene 을 알 필요가 없고, 둘 사이에 포인터가 없다.
    //
    //   ★ 조건이 둘인 이유:
    //     죽고 나서 사망 화면이 뜨기까지 45틱의 사이가 있는데, 그동안 Esc 를 누르면
    //     PauseScene 이 올라간다. 그것이 닫힐 때도 여기가 불린다 —
    //     상태만 보면 **사망 화면을 건너뛰고 곧바로 부활**해 버린다.
    //     「사망 화면까지 올라갔었나」를 같이 봐야 두 경우가 갈린다.
    if (m_state != PlayerState::Dead || !m_deathScreenRequested)
        return;

    Respawn(ctx);
    ctx.audio.Play("ui_confirm", 0.7f, -0.35f);
    Log::Info("[play] 부활 — 적도 되살아났다");
}


// ----------------------------------------------------------------------------
//  SelectAttack — ★ 무브셋의 전부가 이 함수다
//
//    「어느 공격이 나가는가」를 입력 맥락이 정한다. 기획서 3.2.1 의 표 그대로다.
//
//    ★ 순서에 규칙이 있다: **명시적 입력이 암묵적 맥락을 이긴다.**
//      플레이어가 Ctrl 을 누르고 있다면 그건 「다리를 노리겠다」는 의사표시이고,
//      「방금 달리고 있었다」보다 강하다. 사람이 방금 한 행동보다
//      지금 누르고 있는 것이 더 최신 의도이기 때문이다.
//
//      그다음은 암묵적 맥락끼리 **구체적인 것부터** 본다.
//
//    ★ 이 함수 하나가 6-c 에서 JSON 의 조건절이 된다.
//      지금 코드로 적어 두는 이유는, 무엇을 조건으로 삼을지 알아야
//      그릇을 만들 수 있기 때문이다.
// ----------------------------------------------------------------------------
const AttackData& PlayScene::SelectAttack(SceneContext& ctx, PlayerState prev) const
{
    // ① 웅크린 채 공격 — 명시적 조준. 다른 무엇보다 우선한다
    if (ctx.input.CrouchHeld())
        return kDaggerCrouch;

    // ② 공격 중이었다 -> 2타
    //
    //   ★ 여기에 `&& m_comboStep == 0` 을 넣었다가 콤보가 아예 안 나왔다.
    //     ChangeState 가 **이 함수를 부르기 직전에** m_comboStep 을 1 로 올리므로,
    //     2타에 들어오는 바로 그 순간 조건이 거짓이 되어 1타가 다시 나갔다.
    //     빌드도 통과하고 게임도 안 죽는, 「그냥 안 되는」 종류의 버그다.
    //
    //   무한 연타 방지는 여기가 아니라 **예약하는 쪽**에 있다 —
    //   Update 의 콤보 예약이 m_comboStep == 0 일 때만 받는다.
    //   막을 곳이 한 곳이면 충분하고, 두 곳에 두면 이렇게 서로를 방해한다.
    if (prev == PlayerState::Attack)
        return kDaggerCombo2;

    // ③ 달리고 있었다 -> 돌진
    if (prev == PlayerState::Run)
        return kDaggerRunning;

    // ④ 기본
    return kDaggerLight;
}


const AttackData& PlayScene::CurrentAttack() const
{
    // 아직 한 번도 공격하지 않았으면 기본값. 판정은 Attack 상태에서만 도므로
    // 실제로는 쓰이지 않지만, 널 참조를 만들지 않기 위해 둔다.
    return m_currentAttack ? *m_currentAttack : kDaggerLight;
}


// ----------------------------------------------------------------------------
//  ChangeState — 상태 머신의 "Enter"
//
//    들어가는 순간 한 번만 해야 하는 일을 모아 둔다.
//    이게 없으면 매 틱 Play() 를 부르게 되어 애니메이션이 프레임 0 에서 멈춘다.
// ----------------------------------------------------------------------------
void PlayScene::ChangeState(SceneContext& ctx, PlayerState next, bool force)
{
    if (m_state == next && !force)
        return;

    // ★ 들어오기 직전의 상태를 먼저 붙잡는다.
    //   m_state 를 덮은 뒤에는 알 수 없고, 무브셋 선택이 이것을 필요로 한다
    //   (「달리고 있었나」 「공격 중이었나」).
    const PlayerState prev = m_state;

    m_state      = next;
    m_stateTicks = 0;

    switch (next)
    {
    case PlayerState::Idle:
        m_playerSprite->Play(kIdleClip);
        break;

    case PlayerState::Run:
        m_playerSprite->Play(kRunClip);

        // ★ 여기서 m_stepCooldown 을 0 으로 되돌리면 안 된다.
        //
        //   Idle/Run 처리는 UpdateMovement 를 **먼저** 부르고 ChangeState 를
        //   나중에 부른다. 그래서 움직이기 시작한 그 틱에 이미
        //   UpdateMovement 가 발소리를 내고 쿨다운을 15로 채워 놓았다.
        //   여기서 0으로 되돌리면 **다음 틱에 또 울려** 1/60초 간격으로
        //   발소리가 두 번 난다. 「살짝 두꺼운 한 번」으로 들려서 눈치채기 어렵다.
        //
        //   「달리기 시작하자마자 첫 발소리」는 UpdateMovement 의
        //   `else { m_stepCooldown = 0; }` (= 멈춰 있는 동안 0으로 유지)가
        //   이미 보장한다. 같은 일을 두 곳에서 하고 있었던 것이다.
        break;

    case PlayerState::Attack:
    {
        // ★ 콤보 단계를 먼저 정한다. SelectAttack 이 이 값을 본다.
        //   Attack 에서 Attack 으로 들어왔다면 2타다.
        m_comboStep   = (prev == PlayerState::Attack) ? 1 : 0;
        m_comboQueued = false;

        // ★ 어느 공격인지를 **여기서 고정한다.** 매 틱 다시 고르면
        //   휘두르는 도중에 Ctrl 을 떼는 순간 프레임 데이터가 갈려
        //   active 구간을 건너뛰거나 두 번 지나간다.
        //   5-e-3 의 attackIsBite, 5-d 의 구르기 방향과 완전히 같은 이유다.
        m_currentAttack = &SelectAttack(ctx, prev);
        const AttackData& atk = *m_currentAttack;

        // forceRestart = true : 같은 클립이라도 처음부터 다시 재생한다.
        // ★ 클립이 공격마다 다르므로 여기서 함께 갈린다.
        m_playerSprite->Play(atk.clip, true);

        // 이번 휘두르기의 "이미 맞춘 대상" 기록을 비운다.
        m_hitThisSwing = false;

        // ★ 스태미나를 여기서 소모한다.
        //   부족해도 공격은 나간다. 0 미만이 되면 공격이 끝난 뒤 Exhausted 로 간다.
        //   「부족하면 안 나감」이 아니라 「나가고 대가를 치름」이 이 게임의 규칙이다.
        m_stamina.current -= static_cast<float>(atk.staminaCost);
        m_stamina.delay    = kStaminaRegenDelay;

        // 휘두르는 소리. ★ 맞는 소리(hit)는 실제로 겹칠 때만 낸다.
        //   둘을 나눠야 헛치기와 명중이 소리로 구분된다.
        //   공격마다 피치를 살짝 달리해 무엇이 나갔는지 소리로도 구분되게 한다.
        const float pitch = (atk.heightFromFoot > 40.0f) ? 0.22f      // 찌르기 = 높게
                          : (atk.heightFromFoot < 20.0f) ? -0.25f     // 웅크리기 = 낮게
                          : 0.0f;
        ctx.audio.Play("swing", 0.55f, pitch + RandomPitch(0.10f), PanFromCanvasX(PlayerTr().x));

        Log::Info("[play] {} 발동  [{} {} {}]  높이 {:.0f}  stam -{}",
                  atk.name, atk.startup, atk.active, atk.recovery,
                  atk.heightFromFoot, atk.staminaCost);
        break;
    }

    case PlayerState::Roll:
    {
        m_playerSprite->Play(kRollClip, true);

        // ★ 방향을 여기서 고정한다.
        //   입력이 있으면 그 방향, 없으면 바라보는 방향으로 굴러간다.
        //   (입력이 없을 때 뒤로 빠지면 적을 통과할 수 없다)
        const Input::MoveIntent mv = ctx.input.Move();
        if (std::abs(mv.x) > kMoveEpsilon || std::abs(mv.y) > kMoveEpsilon)
        {
            m_player.rollDirX = mv.x;
            m_player.rollDirY = mv.y;
        }
        else
        {
            m_player.rollDirX = static_cast<float>(PlayerTr().facing);
            m_player.rollDirY = 0.0f;
        }

        m_stamina.current -= static_cast<float>(kRoll.staminaCost);
        m_stamina.delay    = kStaminaRegenDelay;

        ctx.audio.Play("swing", 0.4f, -0.35f, PanFromCanvasX(PlayerTr().x));   // 낮은 피치 = 구르는 소리
        break;
    }

    case PlayerState::Exhausted:
        // 지친 전용 애니메이션이 아직 없으므로 idle 을 쓰고 색으로 구분한다.
        m_playerSprite->Play(kIdleClip);
        ctx.audio.Play("ui_cancel", 0.45f);
        Log::Info("[play] 스태미나 고갈 — 경직 (stam {:.1f})", m_stamina.current);
        break;

    case PlayerState::Hurt:
        // 전용 애니메이션이 아직 없으므로 idle + 틴트로 구분한다.
        // ★ 소리는 여기서 내지 않는다 — 버텼는지 휘청였는지에 따라 다르므로
        //   HitPlayer 가 낸다. 「들어올 때 한 번」의 예외가 아니라,
        //   원인을 아는 쪽이 내는 것이 맞다.
        m_playerSprite->Play(kIdleClip, true);
        break;

    case PlayerState::Dead:
        m_playerSprite->Play(kIdleClip, true);
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
void PlayScene::UpdateMovement(SceneContext& ctx, float moveX, float moveY)
{
    // 바라보는 방향은 좌우 입력이 있을 때만 갱신한다.
    if (moveX < -kMoveEpsilon)      PlayerTr().facing = -1;
    else if (moveX > kMoveEpsilon)  PlayerTr().facing = +1;

    // ★ 웅크리면 느려진다. 조준의 대가다 —
    //   「다리를 노리려면 멈춰서 노려야 한다」가 이동 속도 한 줄로 만들어진다.
    const float speed = kPlayerSpeedPerTick * (m_crouching ? kCrouchSpeedScale : 1.0f);

    PlayerTr().x += moveX * speed;
    PlayerTr().y += moveY * speed;

    PlayerTr().x = std::clamp(PlayerTr().x, kOriginX,
                            static_cast<float>(Config::kCanvasWidth) - kOriginX);
    PlayerTr().y = std::clamp(PlayerTr().y, kOriginY,
                            static_cast<float>(Config::kCanvasHeight));

    // 발소리 — 틱을 세어 일정 간격마다
    const bool moving = (std::abs(moveX) > kMoveEpsilon || std::abs(moveY) > kMoveEpsilon);
    if (moving)
    {
        if (--m_stepCooldown <= 0)
        {
            // 웅크려 걸으면 발소리도 그만큼 뜸해야 한다. 안 그러면
            // 「천천히 걷는데 발소리는 뛰는 속도」가 되어 어색하다.
            m_stepCooldown = m_crouching
                ? static_cast<int>(kStepIntervalTicks / kCrouchSpeedScale)
                : kStepIntervalTicks;
            ctx.audio.Play("step", 0.45f, RandomPitch(0.15f), PanFromCanvasX(PlayerTr().x));
        }
    }
    else
    {
        m_stepCooldown = 0;
    }
}


// ----------------------------------------------------------------------------
//  SlideDecaying — 감속하며 미끄러진다
//
//    일정 속도로 움직이면 어색하다. 초반에 빠르고 끝에서 감속해야
//    구르기도 넉백도 그럴듯해 보인다.
//    남은 틱 비율에 비례하는 속도를 주고, 총합이 distance 가 되도록 정규화한다.
//    카메라 흔들림 감쇠와 같은 발상이다.
//
//      속도
//       │▓▓▓▓▓▓
//       │▓▓▓▓
//       │▓▓
//       └────────→ 틱
//
//    ★ 구르기와 넉백이 이 함수를 공유한다.
//      5-d 에서 구르기용으로 썼던 식이 그대로 넉백이 되었다 —
//      방향과 거리와 길이만 다르다.
// ----------------------------------------------------------------------------
void PlayScene::SlideDecaying(float dirX, float dirY, float distance, int totalTicks)
{
    const int remaining = totalTicks - m_stateTicks + 1;   // total .. 1
    if (remaining <= 0)
        return;

    // 1 + 2 + ... + total = total * (total+1) / 2
    const float weightSum = static_cast<float>(totalTicks) * (totalTicks + 1) * 0.5f;
    const float step      = distance * static_cast<float>(remaining) / weightSum;

    PlayerTr().x += dirX * step;
    PlayerTr().y += dirY * step;

    PlayerTr().x = std::clamp(PlayerTr().x, kOriginX,
                            static_cast<float>(Config::kCanvasWidth) - kOriginX);
    PlayerTr().y = std::clamp(PlayerTr().y, kOriginY,
                            static_cast<float>(Config::kCanvasHeight));
}


void PlayScene::UpdateRoll()
{
    SlideDecaying(m_player.rollDirX, m_player.rollDirY,
                  kRoll.distance, kRoll.TotalTicks());
}


void PlayScene::UpdateKnockback()
{
    SlideDecaying(m_player.knockDirX, m_player.knockDirY,
                  kHurt.knockback, kHurt.ticks);
}


// ----------------------------------------------------------------------------
//  UpdateStamina — 상태와 무관하게 매 틱
// ----------------------------------------------------------------------------
void PlayScene::UpdateStamina()
{
    // 회복 지연 중이면 아직 안 찬다. 행동할 때마다 이 값이 초기화된다.
    if (m_stamina.delay > 0)
    {
        --m_stamina.delay;
        return;
    }

    if (m_stamina.current < kStaminaMax)
    {
        m_stamina.current += kStaminaRegenPerTick;
        if (m_stamina.current > kStaminaMax)
            m_stamina.current = kStaminaMax;
    }
}


void PlayScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    // ---- 시간은 상태와 무관하게 매 틱 흐른다 ----
    //   ★ 애니메이션 진행도 여기다. 상태 분기 안에 넣으면 안 된다.
    //     빠뜨리면 프레임이 0 에서 멈추고, loop=false 클립의 Finished() 가
    //     영원히 false 가 되어 Attack 상태에서 빠져나오지 못한다.
    ++m_stateTicks;

    // ★ 애니메이션 진행이 여기서 사라졌다 — SpriteComponent 가 스스로 굴린다.
    //   전에는 m_playerAnim.Tick() 을 여기서 불러야 했고, 실제로 리팩터링에서
    //   그 한 줄을 옮기지 않아 애니메이션이 멈추는 버그를 냈다(handoff §8).
    //   컴포넌트가 자기 시간을 스스로 굴리면 그 종류의 누락이 불가능해진다.
    m_playerObj.Tick(ctx);

    UpdateStamina();

    if (m_player.flash > 0)
        --m_player.flash;

    // ★ 피격 무적도 시간이고, 시간은 상태와 무관하게 흐른다.
    //   경직(18틱)보다 길게(24틱) 남기 때문에 경직이 풀린 뒤 6틱을 더 버텨 준다.
    if (m_invulnTicks > 0)
        --m_invulnTicks;

    const Input::MoveIntent move = ctx.input.Move();
    const bool moving = (std::abs(move.x) > kMoveEpsilon || std::abs(move.y) > kMoveEpsilon);

    // ★ 입력을 여기서 갈무리해 둔다. Render 는 Renderer 만 받으므로
    //   그리는 시점에 입력을 물어볼 수 없다(의도된 설계다).
    //   F1 플래그를 Renderer 로 옮긴 것과 같은 종류의 제약이고,
    //   이쪽은 「틱에서 읽어 멤버에 남긴다」로 해결한다.
    m_crouching = ctx.input.CrouchHeld();
    const bool attackPressed = (consumeEdgeInput && ctx.input.AttackPressed());
    const bool rollPressed   = (consumeEdgeInput && ctx.input.RollPressed());

    // ---- 상태별 처리 ----
    switch (m_state)
    {
    case PlayerState::Idle:
    case PlayerState::Run:
        UpdateMovement(ctx, move.x, move.y);

        // 구르기를 공격보다 먼저 본다. 둘이 동시에 눌리면 회피가 우선이다.
        if (rollPressed)
            ChangeState(ctx, PlayerState::Roll);
        else if (attackPressed)
            ChangeState(ctx, PlayerState::Attack);
        else
            ChangeState(ctx, moving ? PlayerState::Run : PlayerState::Idle);
        break;

    case PlayerState::Attack:
    {
        // ★ 이동 입력을 처리하지 않는다 = 공격 중에는 못 움직인다.
        //   방향 전환도 막힌다. 소울류의 "한 번 휘두르면 끝까지 간다" 감각.
        const AttackData& atk = CurrentAttack();

        // ---- 콤보 예약 ----
        //   ★ active 가 끝난 뒤부터만 받는다.
        //     startup 중에도 받으면 공격키 연타만으로 2타가 확정되어
        //     「1타를 내보고 이어칠지 판단한다」가 사라진다.
        //     격투게임이 이 구간을 두는 이유가 그것이다.
        //
        //   ★ 예약해 두었다가 상태가 끝날 때 꺼내 쓴다.
        //     지금 즉시 전이하면 1타의 후딜을 건너뛰어 버린다.
        if (attackPressed && m_comboStep == 0
            && m_stateTicks >= atk.startup + atk.active)
        {
            m_comboQueued = true;
        }

        // ---- 공격 판정 ----
        //   active 구간에서만, 그리고 이번 휘두르기에 아직 안 맞췄을 때만.
        if (AttackActive() && !m_hitThisSwing && !EnemyDead())
        {
            const int part = m_enemyParts->PickHit(AttackHitbox());
            if (part >= 0)
            {
                m_hitThisSwing = true;   // 3틱 동안 3번 맞는 것을 막는다
                m_enemyParts->Flash(kFlashTicks);

                m_enemyParts->Damage(part, atk.damage);

                // ★ 소리와 흔들림은 "맞는 순간" 에 낸다. 휘두르는 순간이 아니다.
                ctx.camera.Shake(kShakeStrength, kShakeTicks);
                ctx.audio.Play("hit", 0.85f, RandomPitch(0.12f),
                               PanFromCanvasX(m_enemyObj.transform.x));

                Log::Info("[play] {} 로 {} 명중  t{}  dmg {}  남은 HP {}",
                          atk.name, m_enemyParts->Name(part), m_stateTicks,
                          atk.damage, std::max(0, m_enemyParts->Hp(part)));

                if (m_enemyParts->IsBroken(part))
                {
                    Log::Info("[play] ★ {} 파괴!", m_enemyParts->Name(part));

                    // ★ 몸통과 머리는 격파. **다리는 부서져도 죽지 않는다**
                    //   — 기획서의 「다리만 베었는데 격파는 비현실적」이 여기서 해결된다.
                    //
                    //   머리 = 즉사로 정했다(시야 상실 + 청각 시스템 대신).
                    //   머리 HP 20 은 몸통 100 의 1/5 이므로 노릴 수 있으면 보상이 크다.
                    //
                    //   ★ 그리고 상자 좌표만으로 이미 조합이 성립한다:
                    //     다리(40) 를 부수면 적이 엎드리고, kBoxCrawl 의 머리가
                    //     앞으로 튀어나오면서(x +4..+20) 기본 공격 높이에 들어온다.
                    //         다리 4방 → 기어옴 → 머리 2방 = 6방
                    //         몸통 정면                    = 9방
                    //     「부위를 노리면 빨리 죽는다」가 데이터에서 나왔다.
                    if (part == Part_Torso || part == Part_Head)
                    {
                        m_enemyBrain->Kill(ctx);
                        Log::Info("[play] ★★ 적 격파 ({} 파괴)", m_enemyParts->Name(part));
                    }
                }
            }
        }

        // ★ 상태의 길이는 애니메이션이 아니라 프레임 데이터가 정한다.
        //   Finished() 로 판정하면 프레임 데이터 숫자를 바꿔도 타이밍이 안 바뀐다.
        //   데이터가 진실이고, 애니메이션은 거기에 맞춘다.
        if (m_stateTicks >= atk.TotalTicks())
        {
            // ★ 공격이 끝난 시점에 스태미나가 0 미만이면 경직에 들어간다.
            //   공격 자체는 정상적으로 나갔다 — 대가를 뒤에 치르는 것이다.
            //   ★ 고갈이 콤보보다 우선한다. 「2타를 예약해 두면 고갈을 피한다」가
            //     되면 스태미나 시스템에 구멍이 생긴다.
            if (m_stamina.current < 0.0f)
                ChangeState(ctx, PlayerState::Exhausted);
            else if (m_comboQueued)
                // ★ force = true. Attack -> Attack 이라 「같은 상태면 무시」에 걸린다.
                ChangeState(ctx, PlayerState::Attack, true);
            else
                ChangeState(ctx, moving ? PlayerState::Run : PlayerState::Idle);
        }
        break;
    }

    case PlayerState::Roll:
        // ★ 이동 입력을 처리하지 않는다. 시작할 때 고정한 방향으로만 간다.
        UpdateRoll();

        if (m_stateTicks >= kRoll.TotalTicks())
        {
            // 구르기도 스태미나를 쓰므로 같은 고갈 규칙이 적용된다.
            if (m_stamina.current < 0.0f)
                ChangeState(ctx, PlayerState::Exhausted);
            else
                ChangeState(ctx, moving ? PlayerState::Run : PlayerState::Idle);
        }
        break;

    case PlayerState::Exhausted:
        // ★ 아무 입력도 처리하지 않는다. 완전히 무방비.
        //   상태 머신 덕분에 이 한 줄이 「모든 입력 처리에 !exhausted 를 붙이기」를
        //   대신한다.
        if (m_stamina.current >= kStaminaExhaustExit)
            ChangeState(ctx, PlayerState::Idle);
        break;

    case PlayerState::Hurt:
        // ★ 입력을 처리하지 않는다 — Exhausted 와 완전히 같은 구조.
        //   대신 넉백으로 밀려난다.
        UpdateKnockback();

        if (m_stateTicks >= kHurt.ticks)
        {
            // ★ 「고갈 경직 중에 맞으면 경직이 풀린다」를 막는다.
            //   그렇게 되면 **피격이 이득**이 되어 스태미나 시스템이 무너진다.
            //   실제로 겪기 전에는 눈에 안 보이는 종류의 구멍이다.
            const bool stillExhausted =
                   (m_stamina.current < 0.0f)
                || (m_stateBeforeHurt == PlayerState::Exhausted
                    && m_stamina.current < kStaminaExhaustExit);

            if (stillExhausted)
                ChangeState(ctx, PlayerState::Exhausted);
            else
                ChangeState(ctx, moving ? PlayerState::Run : PlayerState::Idle);
        }
        break;

    case PlayerState::Dead:
        // ★ 한 박자 두고 사망 화면을 올린다. 즉시 덮으면 무엇에 죽었는지 안 보인다.
        //   부활은 여기서 하지 않는다 — DeathScene 이 닫힐 때 Resume 이 한다.
        if (!m_deathScreenRequested && m_stateTicks >= kDeathScreenDelay)
        {
            m_deathScreenRequested = true;
            ctx.scenes.Push(std::make_unique<DeathScene>());
        }
        break;
    }

    // ---- 적 ----
    //   ★ 플레이어 갱신 뒤에 부른다. 적이 이번 틱의 플레이어 위치를 보고 움직인다.
    m_enemyObj.Tick(ctx);

    // ---- 적 -> 플레이어 피격 판정 ----
    //
    //   ★ 판정을 EnemyBrain 안에 두지 않았다.
    //     그러면 적이 플레이어의 존재를 알아야 하고, 「플레이어 -> 적」 방향과
    //     모양이 달라진다. 양쪽을 다 보고 있는 것은 이 Scene 하나뿐이므로,
    //     **두 몸 사이의 판정은 Scene 이 한다.** 위쪽 플레이어 공격 판정과
    //     같은 모양인 것을 보라 — 대칭이 유지된다.
    TryEnemyHit(ctx);

    // ---- 상태와 무관한 엣지 입력 ----
    if (consumeEdgeInput)
    {
        // 일시정지는 PausePressed 를 쓴다. 패드 B 가 구르기이므로
        // CancelPressed 를 쓰면 구를 때마다 일시정지가 걸린다.
        if (ctx.input.PausePressed())
        {
            ctx.audio.Play("ui_cancel");
            ctx.scenes.Push(std::make_unique<PauseScene>());
        }

        // ★ F1(히트박스 표시)은 여기 없다. Game 이 프레임당 1회 처리한다 —
        //   표시 설정은 틱과 무관하고, 틱 안에서 읽으면 프레임 정지 중에 사라진다.
        //
        //   F2 는 반대다. 장착 방어구는 **게임 상태**이므로 틱 안에서 바꿔야 하고,
        //   그래서 Input 이 ConsumeEdges 까지 붙잡아 두는 누적 엣지를 쓴다.
        //   「어느 키인가」가 아니라 「무엇을 바꾸는가」가 자리를 정한다.
        if (ctx.input.ArmorSwapPressed())
        {
            m_armorIndex = (m_armorIndex + 1) % kArmorCount;
            ctx.audio.Play("ui_confirm", 0.5f);
            Log::Info("[play] 갑옷 → {}  (poise {})   swing impact 18 / bite impact 12",
                      Armor().name, Armor().poise);
        }
    }
}


AABB PlayScene::SpriteBounds() const
{
    return {
        PlayerTr().x - kOriginX,
        PlayerTr().y - kOriginY,
        PlayerTr().x - kOriginX + kCellW,
        PlayerTr().y
    };
}


AABB PlayScene::PlayerHurtbox() const
{
    return {
        PlayerTr().x - kHitHalfWidth,
        PlayerTr().y - kHitFootGap - kHitHeight,
        PlayerTr().x + kHitHalfWidth,
        PlayerTr().y - kHitFootGap
    };
}


// ① 구르기 무적 — 플레이어가 스태미나와 타이밍으로 **벌어낸** 것
bool PlayScene::RollInvincible() const
{
    if (m_state != PlayerState::Roll)
        return false;

    const RollData& r = kRoll;
    return m_stateTicks >= r.windup
        && m_stateTicks <  r.windup + r.invincible;
}


bool PlayScene::Invincible() const
{
    // ② 피격 무적 — 게임이 **주는** 안전장치.
    //   경직과 한 세트로만 주어지고, 경직보다 길다(kHurt.invuln > kHurt.ticks).
    //   이 6틱의 여유가 「경직이 풀리는 그 틱에 다시 맞는」 스턴락을 막는다.
    if (m_invulnTicks > 0)
        return true;

    return RollInvincible();
}


const ArmorData& PlayScene::Armor() const
{
    return kArmors[m_armorIndex];
}


// ----------------------------------------------------------------------------
//  HitPlayer — ★ 5-e-3 의 심장부
//
//    맞았을 때 무슨 일이 일어나는지를 **데이터가 정한다.**
//
//        poise >= impact  →  데미지만. 자리에서 버틴다 (상태를 바꾸지 않는다)
//        poise <  impact  →  Hurt(경직) + 넉백 + 피격 무적
//
//    ※ 무적 판정은 이 함수에 오기 전에 끝나 있다(TryEnemyAttack).
//      「맞았다」가 확정된 뒤의 처리만 여기 있다.
// ----------------------------------------------------------------------------
void PlayScene::HitPlayer(SceneContext& ctx, const AttackData& atk)
{
    m_player.hp   -= atk.damage;
    m_player.flash = kFlashTicks;

    // ---- 사망이 가장 먼저다. 강인도로 버텨도 HP 는 깎였다 ----
    if (m_player.hp <= 0)
    {
        m_player.hp = 0;
        ctx.camera.Shake(kShakeStrength * 2.5f, kShakeTicks * 3);
        ctx.audio.Play("hit", 1.0f, -0.55f, PanFromCanvasX(PlayerTr().x));
        ChangeState(ctx, PlayerState::Dead);
        return;
    }

    // ---- ★ 경직 여부를 강인도가 정한다 ----
    if (Armor().poise >= atk.impact)
    {
        // 버텨냈다 — **상태를 바꾸지 않는다.**
        // 공격 중이었다면 그대로 휘두름이 이어진다. 흔히 하이퍼아머라 부른다.
        //
        // ★ 피격 무적을 주지 않는다.
        //   못 움직이는 구간이 없으므로 스턴락 위험이 없고,
        //   무적은 그 위험을 막기 위한 장치이기 때문이다.
        //   대신 맞을 때마다 HP 가 확실히 깎인다 — 이것이 버티기의 비용이다.
        ctx.camera.Shake(kShakeStrength * 0.6f, kShakeTicks);
        ctx.audio.Play("hit", 0.5f, -0.75f, PanFromCanvasX(PlayerTr().x));   // 둔탁하게
        Log::Info("[play] 버텨냄  poise {} >= impact {}   dmg {}  HP {}",
                  Armor().poise, atk.impact, atk.damage, m_player.hp);
        return;
    }

    // ---- 휘청였다 ----
    //   넉백 방향 = 적 → 플레이어. 정규화한다.
    float dx = PlayerTr().x - m_enemyObj.transform.x;
    float dy = PlayerTr().y - m_enemyObj.transform.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0001f)
    {
        dx /= len;
        dy /= len;
    }
    else
    {
        // 완전히 겹쳐 있으면 방향이 없다. 적이 보는 쪽으로 밀어낸다.
        dx = static_cast<float>(m_enemyObj.transform.facing);
        dy = 0.0f;
    }
    m_player.knockDirX = dx;
    m_player.knockDirY = dy;

    // ★ ChangeState 전에 기록한다. 뒤에 두면 이미 Hurt 로 바뀌어 있다.
    m_stateBeforeHurt = m_state;
    m_invulnTicks     = kHurt.invuln;

    ctx.camera.Shake(kShakeStrength * 1.8f, kShakeTicks * 2);
    ctx.audio.Play("hit", 0.95f, -0.25f, PanFromCanvasX(PlayerTr().x));
    Log::Info("[play] 피격  poise {} < impact {}   dmg {}  HP {}   경직 {}틱 / 무적 {}틱",
              Armor().poise, atk.impact, atk.damage, m_player.hp,
              kHurt.ticks, kHurt.invuln);

    ChangeState(ctx, PlayerState::Hurt);
}


bool PlayScene::AttackActive() const
{
    if (m_state != PlayerState::Attack)
        return false;

    const AttackData& a = CurrentAttack();
    return m_stateTicks >= a.startup
        && m_stateTicks <  a.startup + a.active;
}


AABB PlayScene::AttackHitbox() const
{
    // ★ 상자를 만드는 계산을 MakeAttackBox 로 뺐다 — 적과 **같은 함수**를 쓴다.
    //   좌우 반전 정규화(AABB 뒤집힘) 함정이 한 곳에만 존재하게 된다.
    return MakeAttackBox(PlayerTr().x, PlayerTr().y, PlayerTr().facing, CurrentAttack());
}


// ★ 적의 상태 머신 · 추격 · 공격 · 부위 판정 · 디버그 표시는 전부
//   Gameplay/EnemyBrain 과 Gameplay/PartsComponent 로 옮겼다.
//   PlayScene 에 남은 것은 **조립과 두 몸 사이의 판정**뿐이다.


// EnemyBrain 이 공개한 것만 물어본다. 내부(상태 enum · 쿨다운)는 모른다.
bool PlayScene::EnemyDead() const
{
    return m_enemyBrain->IsDead();
}


// ----------------------------------------------------------------------------
//  TryEnemyHit — 적의 공격이 플레이어에게 닿았는가
//
//    ★★ 5-d 의 무적 프레임이 의미를 갖는 곳. 컴포넌트로 옮겨도 그대로다.
// ----------------------------------------------------------------------------
void PlayScene::TryEnemyHit(SceneContext& ctx)
{
    if (!m_enemyBrain->AttackActive())     return;
    if (m_enemyBrain->HitThisSwing())      return;   // active 4틱 = 데미지 4번을 막는다
    if (PlayerDead())                      return;

    if (!Intersects(m_enemyBrain->AttackHitbox(), PlayerHurtbox()))
        return;

    // ★ 무적 처리 방식 (b) — 「무적인 틱은 없었던 일」.
    //   휘두르기를 **소진시키지 않는다.** 그래서 무적이 풀린 다음 틱에
    //   active 가 남아 있으면 그때 맞는다. 「무적 프레임」이 문자 그대로 동작한다.
    //   (무적으로 흘린 것을 소진 처리하는 것은 별개 규칙 = 나중의 패링이다)
    if (Invincible())
    {
        Log::Info("[play] ★ 회피 — {} 무적으로 흘렸다 (적 t{})",
                  RollInvincible() ? "구르기" : "피격", m_enemyBrain->StateTicks());
        return;
    }

    m_enemyBrain->MarkHitThisSwing();
    HitPlayer(ctx, m_enemyBrain->CurrentAttack());
}


void PlayScene::Render(Renderer& renderer)
{
    // ★ 적을 그리는 코드가 사라졌다.
    //   스프라이트도, 틴트도, 예고 `!` 도, 부위 상자도 전부 컴포넌트가 자기 것을
    //   자기가 그린다. PlayScene 은 「그려라」 한 줄만 말한다.
    m_enemyObj.Render(renderer);

    // ---- 플레이어 ----
    DirectX::XMVECTOR tint = DirectX::Colors::White;
    if (PlayerDead())
        tint = DirectX::XMVectorSet(0.30f, 0.28f, 0.30f, 1.0f);   // 사망 = 회색
    else if (m_state == PlayerState::Exhausted)
        tint = DirectX::XMVectorSet(0.45f, 0.45f, 0.55f, 1.0f);   // 지쳐서 어둡게
    else if (RollInvincible())
        tint = DirectX::XMVectorSet(0.55f, 0.75f, 1.00f, 1.0f);   // ① 구르기 무적 = 푸르게
    else if (m_player.flash > 0)
        tint = DirectX::XMVectorSet(1.00f, 0.35f, 0.30f, 1.0f);   // 피격 순간 = 붉게
    else if (m_invulnTicks > 0 && (m_invulnTicks / 3) % 2 == 0)
        // ★ ② 피격 무적 = **깜빡임.**
        //   ① 과 다른 표현을 쓰는 이유는 성격이 다르기 때문이다 —
        //   푸른색은 「벌어낸 무적」, 깜빡임은 「봐주는 무적」.
        //   액션게임이 오래 써 온 관례를 그대로 따른다.
        tint = DirectX::XMVectorSet(1.00f, 1.00f, 1.00f, 0.45f);

    // ※ 좌우 반전과 정수 좌표 반올림은 SpriteComponent 가 한다.
    //   전에는 그 규칙이 플레이어용·적용으로 두 벌 복사되어 있었다.

    // ★ 웅크린 자세를 세로로 눌러서 표현한다.
    //
    //   ★★ 원점을 발밑에 둔 결정이 여기서 값을 한다.
    //     세로로 눌러도 **발이 그 자리에 남는다.** 원점이 좌상단이었다면
    //     눌린 만큼 발이 공중에 뜨고, 그것을 보정하는 코드를 따로 써야 했다.
    //     (5-e-3 의 공격 히트박스 계산이 짧았던 것도 같은 이유였다)
    //
    //   ★ 공격 중에는 끈다. CROUCH 공격은 전용 행(row 4)에 **눌린 자세가 이미
    //     구워져 있어서** 여기서 또 누르면 두 번 눌린다.
    //     그리고 구워진 쪽이 픽셀이 깨끗하다 — 어느 줄을 뺄지 생성 스크립트가
    //     골랐으므로 런타임 비정수 축소처럼 지글거리지 않는다.
    //
    //   ※ 웅크린 채 **걷고 서 있는** 자세는 아직 전용 그림이 없어서 이 눌림을 쓴다.
    //     전용 행이 생기면 이 코드는 통째로 사라진다.
    const bool squash = m_crouching && (m_state != PlayerState::Attack);
    const DirectX::XMFLOAT2 scale = squash
        ? DirectX::XMFLOAT2(1.0f, 0.78f)
        : DirectX::XMFLOAT2(1.0f, 1.0f);

    // ★ 표현만 넘기고 그리기는 컴포넌트에 맡긴다.
    m_playerSprite->SetTint(tint);
    m_playerSprite->SetScale(scale.x, scale.y);
    m_playerObj.Render(renderer);

    // ========================================================================
    //  ★ 디버그는 **모든 그림이 끝난 뒤에** 그린다
    //
    //    붙인 순서가 곧 실행 순서라서, Parts 를 먼저 붙이면 판정 상자를
    //    스프라이트가 덮어 「히트박스가 뒤에 있는」 상태가 된다.
    //    실제로 그렇게 만들었다가 화면에서 상자가 안 보였다.
    //
    //    틱 순서(Brain -> Sprite)와 그리기 순서(Sprite -> 디버그)의 요구가
    //    다르므로 **패스를 나눈다.** 그러면 붙인 순서와 무관해진다.
    // ========================================================================
    m_enemyObj.RenderDebug(renderer);
    m_playerObj.RenderDebug(renderer);

    if (renderer.DebugDraw())
    {
        renderer.DrawRectOutline(SpriteBounds(), DirectX::Colors::SlateGray);

        // ★ hurtbox 는 항상 그린다. 색만 바꿔서 무적을 보여 준다.
        //   빈 사각형으로 만들었다면 이 표시가 사라져 「지금 무적인가」를 못 본다.
        //   그리고 무적의 **종류까지** 색으로 나눈다.
        DirectX::XMVECTOR hurtColor = DirectX::Colors::Lime;
        if (RollInvincible())       hurtColor = DirectX::Colors::DeepSkyBlue;  // ① 구르기
        else if (m_invulnTicks > 0) hurtColor = DirectX::Colors::Yellow;       // ② 피격
        renderer.DrawRectOutline(PlayerHurtbox(), hurtColor, 2.0f);

        // ※ 적의 부위 상자와 공격 히트박스는 PartsComponent / EnemyBrain 이
        //   자기 Render 에서 그린다. 여기서 적의 내부를 알 필요가 없다.

        // ★ 공격 히트박스 — active 구간에서만 나타난다.
        //   , 로 멈추고 . 로 밟으면 t8 에 나타나 t10 까지 있는 것을 볼 수 있다.
        if (AttackActive())
        {
            renderer.DrawFilledRect(AttackHitbox(),
                DirectX::XMVectorSet(1.0f, 0.2f, 0.2f, 0.35f));
            renderer.DrawRectOutline(AttackHitbox(), DirectX::Colors::Red, 2.0f);
        }

        // 원점(발밑)을 십자로
        renderer.DrawFilledRect(
            { PlayerTr().x - 5.0f, PlayerTr().y - 1.0f, PlayerTr().x + 5.0f, PlayerTr().y + 1.0f },
            DirectX::Colors::Magenta);
        renderer.DrawFilledRect(
            { PlayerTr().x - 1.0f, PlayerTr().y - 5.0f, PlayerTr().x + 1.0f, PlayerTr().y + 5.0f },
            DirectX::Colors::Magenta);
    }
}


// ----------------------------------------------------------------------------
//  DrawHpBar
//    ★ 플레이어는 부위별 HP 가 아니라 단일 HP 다(기획서 3.2 는 적 전용 시스템).
//      스태미나 바와 같은 구조라 코드가 거울처럼 닮는다.
// ----------------------------------------------------------------------------
void PlayScene::DrawHpBar(Renderer& renderer) const
{
    constexpr float kBarX = 12.0f;
    constexpr float kBarW = 150.0f;
    constexpr float kBarH = 9.0f;
    const float     barY  = Config::kCanvasHeight - 38.0f;

    renderer.DrawFilledRect(
        AABB::FromXYWH(kBarX - 1.0f, barY - 1.0f, kBarW + 2.0f, kBarH + 2.0f),
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.7f));

    const float ratio = std::clamp(
        static_cast<float>(m_player.hp) / static_cast<float>(kPlayerMaxHp),
        0.0f, 1.0f);

    DirectX::XMVECTOR color = DirectX::XMVectorSet(0.78f, 0.22f, 0.22f, 1.0f);
    if (ratio <= 0.0f)      color = DirectX::XMVectorSet(0.30f, 0.10f, 0.10f, 1.0f);
    else if (ratio < 0.3f)  color = DirectX::XMVectorSet(1.00f, 0.35f, 0.30f, 1.0f);   // 위험

    if (ratio > 0.0f)
    {
        renderer.DrawFilledRect(
            AABB::FromXYWH(kBarX, barY, kBarW * ratio, kBarH), color);
    }

    renderer.DrawString(std::format("HP {}", m_player.hp),
                        kBarX + kBarW + 6.0f, barY - 2.0f,
                        DirectX::Colors::DimGray, 1);
}


// ----------------------------------------------------------------------------
//  DrawStaminaBar
//    스태미나는 화면에 보이지 않으면 게임이 성립하지 않는다.
//    플레이어가 남은 양을 모르면 관리할 수가 없다.
// ----------------------------------------------------------------------------
void PlayScene::DrawStaminaBar(Renderer& renderer) const
{
    constexpr float kBarX = 12.0f;
    constexpr float kBarW = 150.0f;
    constexpr float kBarH = 9.0f;
    const float     barY  = Config::kCanvasHeight - 24.0f;

    // 테두리 겸 배경
    renderer.DrawFilledRect(
        AABB::FromXYWH(kBarX - 1.0f, barY - 1.0f, kBarW + 2.0f, kBarH + 2.0f),
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.7f));

    // ★ current 는 음수가 될 수 있으므로 표시 비율은 0 으로 자른다.
    const float ratio = std::clamp(m_stamina.current / kStaminaMax, 0.0f, 1.0f);

    DirectX::XMVECTOR color = DirectX::XMVectorSet(0.35f, 0.80f, 0.45f, 1.0f);   // 녹색
    if (m_state == PlayerState::Exhausted)
        color = DirectX::XMVectorSet(0.85f, 0.20f, 0.20f, 1.0f);                // 경직 = 빨강
    else if (ratio < 0.3f)
        color = DirectX::XMVectorSet(0.90f, 0.75f, 0.25f, 1.0f);                // 부족 = 노랑

    if (ratio > 0.0f)
    {
        renderer.DrawFilledRect(
            AABB::FromXYWH(kBarX, barY, kBarW * ratio, kBarH), color);
    }

    renderer.DrawString("STAM", kBarX + kBarW + 6.0f, barY - 2.0f,
                        DirectX::Colors::DimGray, 1);
}


void PlayScene::RenderUI(Renderer& renderer)
{
    DrawHpBar(renderer);
    DrawStaminaBar(renderer);

    // ★ F2 로 바뀌는 값이므로 **항상** 보여야 한다.
    //   안 보이면 「같은 공격에 왜 이번엔 안 밀렸지?」를 확인할 수가 없다.
    //   적 공격의 impact 는 swing 18 / bite 12 다.
    renderer.DrawString(
        std::format("ARMOR {} (poise {})  F2 to swap", Armor().name, Armor().poise),
        12.0f, Config::kCanvasHeight - 52.0f,
        DirectX::Colors::SlateGray, 1);

    // ★ 상태 머신을 눈으로 보기 위한 표시.
    //   F2 로 멈추고 F4 를 눌러 가며 STATE 와 t 를 세면
    //   「공격이 몇 틱짜리인가」를 직접 확인할 수 있다.
    if (m_state == PlayerState::Attack)
    {
        // ★ 어느 공격이 나갔는지 · 다음이 예약됐는지가 보여야
        //   무브셋이 실제로 갈리는 것을 눈으로 확인할 수 있다.
        const AttackData& a = CurrentAttack();
        renderer.DrawString(
            std::format("{}{}  t{:<3}{}   [{} {} {}]  h{:.0f}{}",
                        a.name, (m_comboStep > 0) ? "-2" : "",
                        m_stateTicks, AttackPhase(m_stateTicks, a),
                        a.startup, a.active, a.recovery, a.heightFromFoot,
                        m_comboQueued ? "  >> NEXT" : ""),
            6.0f, 6.0f,
            AttackActive() ? DirectX::Colors::Red : DirectX::Colors::Orange, 1);
    }
    else if (m_state == PlayerState::Roll)
    {
        const RollData& r = kRoll;
        renderer.DrawString(
            std::format("STATE ROLL  t{:<3}{}   [{} {} {}]",
                        m_stateTicks, RollPhase(m_stateTicks, r),
                        r.windup, r.invincible, r.recovery),
            6.0f, 6.0f,
            Invincible() ? DirectX::Colors::DeepSkyBlue : DirectX::Colors::Orange, 1);
    }
    else if (m_state == PlayerState::Hurt)
    {
        // ★ 경직 18틱 / 무적 24틱 이 **둘 다** 보여야 한다.
        //   무적이 경직보다 길다는 것을 눈으로 확인하는 표시다.
        renderer.DrawString(
            std::format("STATE HURT  t{:<3}stagger {}   invuln {}",
                        m_stateTicks, kHurt.ticks, m_invulnTicks),
            6.0f, 6.0f, DirectX::Colors::Yellow, 1);
    }
    else
    {
        renderer.DrawString(
            std::format("STATE {}{}  t{}{}", StateName(m_state),
                        m_crouching ? " (CROUCH)" : "", m_stateTicks,
                        (m_invulnTicks > 0)
                            ? std::format("   invuln {}", m_invulnTicks)
                            : std::string{}),
            6.0f, 6.0f,
            (m_state == PlayerState::Exhausted || PlayerDead())
                ? DirectX::Colors::Red
                : DirectX::Colors::Orange, 1);
    }

    // ★ 디버그 표시를 한 블록으로 모았다. if 가 두 번 있으면
    //   조건을 바꿀 때 한쪽만 고치는 일이 생긴다.
    if (renderer.DebugDraw())
    {
        renderer.DrawString(
            std::format("stam {:6.1f} / {:.0f}   regen delay {:2}",
                        m_stamina.current, kStaminaMax, m_stamina.delay),
            6.0f, 20.0f, DirectX::Colors::Gainsboro, 1);

        // HP 바가 -38 로 들어왔으므로 범례를 위로 올린다.
        renderer.DrawString("green/blue/yellow=hurtbox  red=my hit  orange=enemy hit",
                            6.0f, Config::kCanvasHeight - 66.0f,
                            DirectX::Colors::Lime, 1);

        // 부위별 HP. 아래에서 위 순서로 쌓아 올린다.
        for (int i = 0; i < Part_Count; ++i)
        {
            const bool broken = m_enemyParts->IsBroken(i);
            renderer.DrawString(
                std::format("{:<6}{:>4}/{:<4}{}", m_enemyParts->Name(i),
                            std::max(0, m_enemyParts->Hp(i)), m_enemyParts->MaxHp(i),
                            broken ? " BROKEN" : ""),
                Config::kCanvasWidth - 150.0f,
                40.0f + i * 14.0f,
                broken ? DirectX::Colors::DimGray : DirectX::Colors::Gold, 1);
        }
    }

    // 적 상태 — 부위 파괴가 행동을 바꾸는 것을 눈으로 확인하는 표시
    if (m_enemyBrain->State() == EnemyState::Attack)
    {
        // ★ 적 공격도 플레이어와 같은 프레임 데이터 표시를 쓴다.
        //   AttackPhase() 를 그대로 재사용한다 — 구조가 같으니 도구도 같다.
        const AttackData& a = m_enemyBrain->CurrentAttack();
        renderer.DrawString(
            std::format("ENEMY {} t{:<3}{}   [{} {} {}]  imp {}",
                        a.name,
                        m_enemyBrain->StateTicks(),
                        AttackPhase(m_enemyBrain->StateTicks(), a),
                        a.startup, a.active, a.recovery, a.impact),
            6.0f, 34.0f,
            m_enemyBrain->AttackActive() ? DirectX::Colors::Red
                                         : DirectX::Colors::Orange, 1);
    }
    else
    {
        renderer.DrawString(
            std::format("ENEMY {}{}{}", EnemyStateName(m_enemyBrain->State()),
                        m_enemyParts->LegsBroken() ? "  (legs broken)" : "",
                        m_enemyBrain->Cooldown() > 0
                            ? std::format("  cd {}", m_enemyBrain->Cooldown())
                            : std::string{}),
            6.0f, 34.0f,
            m_enemyParts->LegsBroken() ? DirectX::Colors::Orange
                                       : DirectX::Colors::Gold, 1);
    }

    if (EnemyDead())
        renderer.DrawStringCentered("ENEMY DOWN", Config::kCanvasWidth * 0.5f, 60.0f,
                                    DirectX::Colors::Gold, 2);

    // ※ "YOU DIED" 는 여기서 그리지 않는다. DeathScene 이 맡는다 —
    //   페이드와 자동 부활이 붙으면서 「사망 화면」이 하나의 Scene 이 되었다.
}
