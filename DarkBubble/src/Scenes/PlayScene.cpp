#include "Scenes/PlayScene.h"
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
    constexpr AnimationClip kAttackClip { /*row*/ 2, /*frames*/ 6, /*ticks*/  4, /*loop*/ false };  // 15fps
    constexpr AnimationClip kRollClip   { /*row*/ 3, /*frames*/ 6, /*ticks*/  4, /*loop*/ false };  // 15fps
    //   구르기는 24틱, 상태는 26틱 — 마지막 프레임(일어남)이 2틱 더 유지된다.
    //   상태 길이는 프레임 데이터가 정하고 애니메이션이 거기에 맞춘다는 원칙 그대로다.

    // ---- 무기 프레임 데이터 ----
    //   ★ 6단계에서 이 값들이 weapons.json 으로 빠진다.
    //     지금은 애니메이션(6프레임 × 4틱 = 24틱)과 총합을 일부러 맞춰 뒀다.
    constexpr AttackData kDaggerLight{
        /*startup*/  8,
        /*active*/   3,
        /*recovery*/ 13,     // 합계 24 틱 = 0.4 초
    };

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

    // ---- 히트박스 생성 : 플레이어와 적이 **공유한다** ----
    //   발밑 원점 + facing + AttackData 만으로 만들어진다.
    //   때리는 쪽이 누구인지 이 함수는 모른다 — 그래서 양쪽에서 쓸 수 있다.
    //
    //   ★ facing 이 -1 이면 outer < inner 가 되어 사각형이 뒤집힌다.
    //     AABB 는 left <= right 를 전제하고 Intersects 가 그 전제에 의존하므로,
    //     정규화하지 않으면 왼쪽을 볼 때 공격이 절대 맞지 않는다.
    //     한 곳에 모아 두면 이 함정을 두 번 밟지 않는다.
    AABB MakeAttackBox(float x, float y, int facing, const AttackData& a)
    {
        const float inner = x     + facing * a.reach;
        const float outer = inner + facing * a.width;

        const float left  = (inner < outer) ? inner : outer;
        const float right = (inner < outer) ? outer : inner;

        const float centerY = y - a.heightFromFoot;
        return { left, centerY - a.height * 0.5f, right, centerY + a.height * 0.5f };
    }

    float RandomPitch(float spread)
    {
        static std::mt19937 rng{ 12345 };
        std::uniform_real_distribution<float> dist(-spread, spread);
        return dist(rng);
    }

    float PanFromX(float x)
    {
        return std::clamp(x / static_cast<float>(Config::kCanvasWidth) * 2.0f - 1.0f,
                          -1.0f, 1.0f);
    }

    // ---- 적 ----
    //   같은 그림(0행)을 속도만 바꿔 idle / chase 로 쓴다.
    //   AnimationPlayer::Play 가 ticksPerFrame 까지 비교하도록 고쳐서
    //   전환이 실제로 반영된다.
    constexpr AnimationClip kEnemyIdleClip  { /*row*/ 0, /*frames*/ 4, /*ticks*/ 14, /*loop*/ true };
    constexpr AnimationClip kEnemyChaseClip { /*row*/ 0, /*frames*/ 4, /*ticks*/  6, /*loop*/ true };
    constexpr AnimationClip kEnemyCrawlClip { /*row*/ 1, /*frames*/ 4, /*ticks*/ 10, /*loop*/ true };

    // ---- 적 공격 클립 ----
    //   ★ 애니메이션을 프레임 데이터에 맞추는 두 조건.
    //
    //     ① frameCount × ticksPerFrame == startup + active + recovery
    //        애니메이션이 상태보다 먼저 끝나거나 도중에 잘리지 않는다
    //
    //     ② startup / ticksPerFrame == 타격 프레임의 인덱스  (정수여야 한다)
    //        팔이 뻗는 그림이 **판정이 켜지는 바로 그 틱에** 시작한다
    //
    //   ②를 정수로 만들려고 프레임 데이터를 2~3틱 조정했다.
    //   데이터가 진실이라는 원칙은 유지되지만, 「보기와 판정이 어긋나는」 것보다
    //   데이터를 반올림하는 편이 싸다. 어긋나면 플레이어가 학습할 수 없다.
    //
    //     swing  5 × 12 = 60 = 24 + 4 + 32     24 / 12 = 2  → 프레임 2 가 타격
    //     bite   6 ×  9 = 54 = 18 + 3 + 33     18 /  9 = 2  → 프레임 2 가 타격
    //
    //   ★ loop = false. 마지막 프레임에서 멈춘다.
    //     다만 상태를 빠져나가는 판정은 Finished() 가 아니라 프레임 데이터가 한다.
    constexpr AnimationClip kEnemySwingClip { /*row*/ 2, /*frames*/ 5, /*ticks*/ 12, /*loop*/ false };
    constexpr AnimationClip kEnemyBiteClip  { /*row*/ 3, /*frames*/ 6, /*ticks*/  9, /*loop*/ false };

    // ---- 부위 이름과 HP : 자세와 무관 ----
    constexpr const char* kPartName[Part_Count]  = { "HEAD", "TORSO", "LEGS" };
    constexpr int         kPartMaxHp[Part_Count] = {     20,     100,     40 };

    // ---- 서 있는 자세의 상자 ----
    //   스프라이트(발끝 y=62) 기준.
    //     머리 y  9..27  →  발밑 기준 -55..-37
    //     몸통 y 27..46  →              -37..-18
    //     다리 y 46..62  →              -18..  0
    //   좌우 대칭이라 뒤집어도 같다.
    constexpr PartBox kBoxStand[Part_Count] = {
        { -10.0f, -55.0f,  10.0f, -37.0f },   // HEAD
        { -11.0f, -37.0f,  11.0f, -18.0f },   // TORSO
        { -10.0f, -18.0f,  10.0f,   0.0f },   // LEGS
    };

    // ---- 엎드린 자세의 상자 ----
    //   crawl 스프라이트 기준. 몸을 낮추고 머리가 앞으로 나온다.
    //     몸통 y 48..61, x 22..44  →  x -10..+12, y -16..-3
    //     머리 y 37..53, x 36..52  →  x  +4..+20, y -27..-11
    //   ★ 좌우 비대칭이다. facing 이 -1 이면 상자도 뒤집어야 한다.
    constexpr PartBox kBoxCrawl[Part_Count] = {
        {   4.0f, -27.0f,  20.0f, -11.0f },   // HEAD  — 앞으로 나온다
        { -10.0f, -16.0f,  12.0f,  -3.0f },   // TORSO — 낮게 엎드린다
        { -10.0f, -14.0f,   0.0f,  -3.0f },   // LEGS  — 이미 부서져 있어 실제로는 안 쓰인다
    };

    // ---- 적 공격 (플레이어의 kDaggerLight 와 같은 구조체다) ----
    //
    //   ★ startup 24틱(0.4초) 은 **일부러 길다.** 예고를 보고 반응할 시간이다.
    //     플레이어 구르기의 무적은 [t+4, t+16) 이고 판정은 [24, 28) 이므로
    //         t+4 <= 24  그리고  t+16 >= 28   →   t12 ~ t20
    //     즉 9틱(0.15초)의 회피 창이 생긴다. 이 숫자 하나가 난이도다.
    //     (창의 폭 = invincible 12 − active 4 + 1 = 9. 이 식은 항상 성립한다)
    //
    //   ★ 24 는 애니메이션 때문에 정해진 숫자다 — 5프레임 × 12틱 시트에서
    //     24 / 12 = 2 라 팔이 뻗는 프레임이 판정과 같은 틱에 시작한다.
    constexpr AttackData kEnemySwing{
        /*startup*/  24,
        /*active*/    4,
        /*recovery*/ 32,            // 합계 60틱 = 1초. recovery 가 반격의 창이다
        /*reach*/     8.0f,
        /*width*/    26.0f,
        /*height*/   26.0f,
        /*heightFromFoot*/ 30.0f,
        /*damage*/   18,
        /*staminaCost*/ 0,          // 적은 스태미나를 쓰지 않는다
        /*impact*/   18,
    };

    // 물어뜯기 — 다리가 부서져 기어다닐 때.
    //   ★ 「다리 파괴 = 무해」로 만들지 않기 위한 데이터다.
    //     더 빠르고(startup 18) 더 낮지만, 사거리가 짧고 후딜이 길다.
    //     기획서 3.2 는 「다리 = 이동 불가」이지 「무해」가 아니다.
    constexpr AttackData kEnemyBite{
        /*startup*/  18,            // 낮은 자세에서 갑자기 — 예고가 짧다 (18 / 9 = 2)
        /*active*/    3,
        /*recovery*/ 33,            // 기어서 재정비하므로 후딜이 길다. 합계 54틱
        /*reach*/     4.0f,
        /*width*/    20.0f,
        /*height*/   16.0f,
        /*heightFromFoot*/ 12.0f,   // 낮게 — 발밑을 노린다
        /*damage*/   10,
        /*staminaCost*/ 0,
        /*impact*/   12,
    };

    // 공격이 끝난 뒤 다음 공격까지. 없으면 사거리 안에서 무한 공격이 된다.
    constexpr int kEnemyAttackCooldown = 40;   // 0.67 초

    // ---- 적 행동 ----
    constexpr float kEnemySightRange  = 220.0f;   // 이 거리 안이면 추격 시작
    constexpr float kEnemyWalkPerSec  = 60.0f;    // 플레이어(150)보다 훨씬 느리다
    constexpr float kEnemyCrawlPerSec = 18.0f;    // 다리가 부서지면 이 속도
    constexpr float kEnemyWalkPerTick  = kEnemyWalkPerSec  / 60.0f;
    constexpr float kEnemyCrawlPerTick = kEnemyCrawlPerSec / 60.0f;

    // ---- 공격 위치 ----
    //   ★ 가로(사거리)와 세로(허용폭)를 따로 둔다. 히트박스가 가로로 뻗으므로
    //     원형 거리로 판정하면 위아래로 떨어진 플레이어를 영원히 헛친다.
    //
    //   ★ 그리고 이 값들이 「멈추는 위치」와 「공격하는 위치」 양쪽에 쓰인다.
    //     따로 두면 「멈췄는데 닿지 않는」 적이 생긴다 —
    //     5-e-2 의 kEnemyStopDist(28) 를 그대로 두고 물어뜯기 사거리(20)를
    //     넣었다면, 기어오는 적이 28 에서 멈춰 영원히 못 물었을 것이다.
    constexpr float kEnemySwingRange = 34.0f;
    constexpr float kEnemyBiteRange  = 20.0f;

    //   세로 허용폭. reach(8) 보다 플레이어 hurtbox 반폭(10) 이 크므로
    //   가로로 완전히 겹쳐도(dx=0) 상자는 닿는다. 세로만 맞춰 주면 된다.
    constexpr float kEnemyAttackYTolerance = 14.0f;

    const char* EnemyStateName(EnemyState s)
    {
        switch (s)
        {
        case EnemyState::Chase:  return "CHASE";
        case EnemyState::Attack: return "ATTACK";
        case EnemyState::Crawl:  return "CRAWL";
        case EnemyState::Dead:   return "DEAD";
        default:                 return "IDLE";
        }
    }

    // 두 사각형이 겹치는 면적. 안 겹치면 0.
    float OverlapArea(const AABB& a, const AABB& b)
    {
        const float w = std::min(a.right,  b.right)  - std::max(a.left, b.left);
        const float h = std::min(a.bottom, b.bottom) - std::max(a.top,  b.top);
        if (w <= 0.0f || h <= 0.0f)
            return 0.0f;
        return w * h;
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
    m_sheet = ctx.assets.Texture(L"assets/textures/player.png");
    if (!m_sheet)
        return false;

    m_enemySheet = ctx.assets.Texture(L"assets/textures/enemy.png");
    if (!m_enemySheet)
        return false;

    m_playerAnim.Play(kIdleClip);
    m_enemyAnim.Play(kEnemyIdleClip);

    // 부위 HP 초기화
    for (int i = 0; i < Part_Count; ++i)
        m_enemy.hp[i] = kPartMaxHp[i];

    m_player.hp = kPlayerMaxHp;

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
//  ChangeState — 상태 머신의 "Enter"
//
//    들어가는 순간 한 번만 해야 하는 일을 모아 둔다.
//    이게 없으면 매 틱 Play() 를 부르게 되어 애니메이션이 프레임 0 에서 멈춘다.
// ----------------------------------------------------------------------------
void PlayScene::ChangeState(SceneContext& ctx, PlayerState next)
{
    if (m_state == next)
        return;

    m_state      = next;
    m_stateTicks = 0;

    switch (next)
    {
    case PlayerState::Idle:
        m_playerAnim.Play(kIdleClip);
        break;

    case PlayerState::Run:
        m_playerAnim.Play(kRunClip);
        m_stepCooldown = 0;   // 달리기 시작하자마자 첫 발소리
        break;

    case PlayerState::Attack:
        // forceRestart = true : 같은 클립이라도 처음부터 다시 재생한다.
        // 연속 공격을 넣을 때 필요해진다.
        m_playerAnim.Play(kAttackClip, true);

        // 이번 휘두르기의 "이미 맞춘 대상" 기록을 비운다.
        m_hitThisSwing = false;

        // ★ 스태미나를 여기서 소모한다.
        //   부족해도 공격은 나간다. 0 미만이 되면 공격이 끝난 뒤 Exhausted 로 간다.
        //   「부족하면 안 나감」이 아니라 「나가고 대가를 치름」이 이 게임의 규칙이다.
        m_stamina.current -= static_cast<float>(kDaggerLight.staminaCost);
        m_stamina.delay    = kStaminaRegenDelay;

        // 휘두르는 소리. ★ 맞는 소리(hit)는 실제로 겹칠 때만 낸다.
        //   둘을 나눠야 헛치기와 명중이 소리로 구분된다.
        ctx.audio.Play("swing", 0.55f, RandomPitch(0.12f), PanFromX(m_player.x));
        break;

    case PlayerState::Roll:
    {
        m_playerAnim.Play(kRollClip, true);

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
            m_player.rollDirX = static_cast<float>(m_player.facing);
            m_player.rollDirY = 0.0f;
        }

        m_stamina.current -= static_cast<float>(kRoll.staminaCost);
        m_stamina.delay    = kStaminaRegenDelay;

        ctx.audio.Play("swing", 0.4f, -0.35f, PanFromX(m_player.x));   // 낮은 피치 = 구르는 소리
        break;
    }

    case PlayerState::Exhausted:
        // 지친 전용 애니메이션이 아직 없으므로 idle 을 쓰고 색으로 구분한다.
        m_playerAnim.Play(kIdleClip);
        ctx.audio.Play("ui_cancel", 0.45f);
        Log::Info("[play] 스태미나 고갈 — 경직 (stam {:.1f})", m_stamina.current);
        break;

    case PlayerState::Hurt:
        // 전용 애니메이션이 아직 없으므로 idle + 틴트로 구분한다.
        // ★ 소리는 여기서 내지 않는다 — 버텼는지 휘청였는지에 따라 다르므로
        //   HitPlayer 가 낸다. 「들어올 때 한 번」의 예외가 아니라,
        //   원인을 아는 쪽이 내는 것이 맞다.
        m_playerAnim.Play(kIdleClip, true);
        break;

    case PlayerState::Dead:
        m_playerAnim.Play(kIdleClip, true);
        ctx.audio.Play("ui_cancel", 0.9f, -0.6f);
        Log::Info("[play] ★★ 플레이어 사망 — 5-e-4 에서 DeathScene 으로 간다");
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
    if (moveX < -kMoveEpsilon)      m_player.facing = -1;
    else if (moveX > kMoveEpsilon)  m_player.facing = +1;

    m_player.x += moveX * kPlayerSpeedPerTick;
    m_player.y += moveY * kPlayerSpeedPerTick;

    m_player.x = std::clamp(m_player.x, kOriginX,
                            static_cast<float>(Config::kCanvasWidth) - kOriginX);
    m_player.y = std::clamp(m_player.y, kOriginY,
                            static_cast<float>(Config::kCanvasHeight));

    // 발소리 — 틱을 세어 일정 간격마다
    const bool moving = (std::abs(moveX) > kMoveEpsilon || std::abs(moveY) > kMoveEpsilon);
    if (moving)
    {
        if (--m_stepCooldown <= 0)
        {
            m_stepCooldown = kStepIntervalTicks;
            ctx.audio.Play("step", 0.45f, RandomPitch(0.15f), PanFromX(m_player.x));
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

    m_player.x += dirX * step;
    m_player.y += dirY * step;

    m_player.x = std::clamp(m_player.x, kOriginX,
                            static_cast<float>(Config::kCanvasWidth) - kOriginX);
    m_player.y = std::clamp(m_player.y, kOriginY,
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
    m_playerAnim.Tick();
    UpdateStamina();

    if (m_player.flash > 0)
        --m_player.flash;

    // ★ 피격 무적도 시간이고, 시간은 상태와 무관하게 흐른다.
    //   경직(18틱)보다 길게(24틱) 남기 때문에 경직이 풀린 뒤 6틱을 더 버텨 준다.
    if (m_invulnTicks > 0)
        --m_invulnTicks;

    const Input::MoveIntent move = ctx.input.Move();
    const bool moving = (std::abs(move.x) > kMoveEpsilon || std::abs(move.y) > kMoveEpsilon);
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
        // ★ 이동 입력을 처리하지 않는다 = 공격 중에는 못 움직인다.
        //   방향 전환도 막힌다. 소울류의 "한 번 휘두르면 끝까지 간다" 감각.

        // ---- 공격 판정 ----
        //   active 구간에서만, 그리고 이번 휘두르기에 아직 안 맞췄을 때만.
        if (AttackActive() && !m_hitThisSwing && !EnemyDead())
        {
            const int part = PickHitPart(AttackHitbox());
            if (part >= 0)
            {
                m_hitThisSwing = true;   // 3틱 동안 3번 맞는 것을 막는다
                m_enemy.flash  = kFlashTicks;

                m_enemy.hp[part] -= kDaggerLight.damage;

                // ★ 소리와 흔들림은 "맞는 순간" 에 낸다. 휘두르는 순간이 아니다.
                ctx.camera.Shake(kShakeStrength, kShakeTicks);
                ctx.audio.Play("hit", 0.85f, RandomPitch(0.12f), PanFromX(m_enemy.x));

                Log::Info("[play] {} 명중  t{}  dmg {}  남은 HP {}",
                          kPartName[part], m_stateTicks,
                          kDaggerLight.damage, std::max(0, m_enemy.hp[part]));

                if (m_enemy.hp[part] <= 0)
                {
                    Log::Info("[play] ★ {} 파괴!", kPartName[part]);

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
                        ChangeEnemyState(ctx, EnemyState::Dead);
                        Log::Info("[play] ★★ 적 격파 ({} 파괴)", kPartName[part]);
                    }
                }
            }
        }

        // ★ 상태의 길이는 애니메이션이 아니라 프레임 데이터가 정한다.
        //   Finished() 로 판정하면 프레임 데이터 숫자를 바꿔도 타이밍이 안 바뀐다.
        //   데이터가 진실이고, 애니메이션은 거기에 맞춘다.
        if (m_stateTicks >= kDaggerLight.TotalTicks())
        {
            // ★ 공격이 끝난 시점에 스태미나가 0 미만이면 경직에 들어간다.
            //   공격 자체는 정상적으로 나갔다 — 대가를 뒤에 치르는 것이다.
            if (m_stamina.current < 0.0f)
                ChangeState(ctx, PlayerState::Exhausted);
            else
                ChangeState(ctx, moving ? PlayerState::Run : PlayerState::Idle);
        }
        break;

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
        // 아무것도 하지 않는다. ★ 5-e-4 에서 DeathScene 전환이 여기 들어온다.
        break;
    }

    // ---- 적 ----
    //   ★ 플레이어 갱신 뒤에 부른다. 적이 이번 틱의 플레이어 위치를 보고 움직인다.
    //     그리고 적 공격의 피격 판정도 이 안에서 일어난다 —
    //     즉 이번 틱에 구른 결과(무적)가 이번 틱의 적 공격에 반영된다.
    UpdateEnemy(ctx);

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

        if (ctx.input.DebugTogglePressed())
        {
            m_showDebug = !m_showDebug;
            Log::Info("[play] 히트박스 표시 {}", m_showDebug ? "ON" : "OFF");
        }

        // ★ 임시 키. 강인도의 두 분기를 눈으로 비교하기 위한 것이다.
        //   같은 공격에 CLOTH 는 튕겨나가고 PLATE 는 그대로 서서 휘두른다.
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
        m_player.x - kOriginX,
        m_player.y - kOriginY,
        m_player.x - kOriginX + kCellW,
        m_player.y
    };
}


AABB PlayScene::PlayerHurtbox() const
{
    return {
        m_player.x - kHitHalfWidth,
        m_player.y - kHitFootGap - kHitHeight,
        m_player.x + kHitHalfWidth,
        m_player.y - kHitFootGap
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
        ctx.audio.Play("hit", 1.0f, -0.55f, PanFromX(m_player.x));
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
        ctx.audio.Play("hit", 0.5f, -0.75f, PanFromX(m_player.x));   // 둔탁하게
        Log::Info("[play] 버텨냄  poise {} >= impact {}   dmg {}  HP {}",
                  Armor().poise, atk.impact, atk.damage, m_player.hp);
        return;
    }

    // ---- 휘청였다 ----
    //   넉백 방향 = 적 → 플레이어. 정규화한다.
    float dx = m_player.x - m_enemy.x;
    float dy = m_player.y - m_enemy.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0001f)
    {
        dx /= len;
        dy /= len;
    }
    else
    {
        // 완전히 겹쳐 있으면 방향이 없다. 적이 보는 쪽으로 밀어낸다.
        dx = static_cast<float>(m_enemy.facing);
        dy = 0.0f;
    }
    m_player.knockDirX = dx;
    m_player.knockDirY = dy;

    // ★ ChangeState 전에 기록한다. 뒤에 두면 이미 Hurt 로 바뀌어 있다.
    m_stateBeforeHurt = m_state;
    m_invulnTicks     = kHurt.invuln;

    ctx.camera.Shake(kShakeStrength * 1.8f, kShakeTicks * 2);
    ctx.audio.Play("hit", 0.95f, -0.25f, PanFromX(m_player.x));
    Log::Info("[play] 피격  poise {} < impact {}   dmg {}  HP {}   경직 {}틱 / 무적 {}틱",
              Armor().poise, atk.impact, atk.damage, m_player.hp,
              kHurt.ticks, kHurt.invuln);

    ChangeState(ctx, PlayerState::Hurt);
}


bool PlayScene::AttackActive() const
{
    if (m_state != PlayerState::Attack)
        return false;

    const AttackData& a = kDaggerLight;
    return m_stateTicks >= a.startup
        && m_stateTicks <  a.startup + a.active;
}


AABB PlayScene::AttackHitbox() const
{
    // ★ 상자를 만드는 계산을 MakeAttackBox 로 뺐다 — 적과 **같은 함수**를 쓴다.
    //   좌우 반전 정규화(AABB 뒤집힘) 함정이 한 곳에만 존재하게 된다.
    return MakeAttackBox(m_player.x, m_player.y, m_player.facing, kDaggerLight);
}


// ----------------------------------------------------------------------------
//  적 상태 머신 — 플레이어와 같은 구조
// ----------------------------------------------------------------------------
void PlayScene::ChangeEnemyState(SceneContext& ctx, EnemyState next)
{
    if (m_enemy.state == next)
        return;

    m_enemy.state      = next;
    m_enemy.stateTicks = 0;

    // Enter : 상태에 들어갈 때 한 번만
    switch (next)
    {
    case EnemyState::Idle:  m_enemyAnim.Play(kEnemyIdleClip);  break;
    case EnemyState::Chase: m_enemyAnim.Play(kEnemyChaseClip); break;
    case EnemyState::Crawl: m_enemyAnim.Play(kEnemyCrawlClip); break;

    case EnemyState::Attack:
        // ★ 어느 공격인지 여기서 고정한다. 도중에 다리가 부서져도 안 바뀐다.
        m_enemy.attackIsBite = EnemyLegsBroken();

        // 이번 휘두르기의 「이미 맞췄나」 표시를 비운다.
        // 플레이어 쪽 ChangeState(Attack) 의 m_hitThisSwing 과 같은 줄이다.
        m_enemy.hitThisSwing = false;

        // ★ 공격 모션. forceRestart = true 다 —
        //   같은 공격을 연달아 낼 때 클립이 처음부터 다시 재생되어야 한다.
        //   플레이어 쪽 Play(kAttackClip, true) 와 같은 이유.
        m_enemyAnim.Play(m_enemy.attackIsBite ? kEnemyBiteClip : kEnemySwingClip, true);

        // ★ 이 소리는 **청각 예고**다. 화면을 안 보고 있어도 반응할 수 있게 해 준다.
        //   시각 예고(팔을 젖히는 모션)와 이중으로 둔다.
        ctx.audio.Play("swing", 0.4f, -0.55f, PanFromX(m_enemy.x));
        break;

    case EnemyState::Dead:  break;   // 마지막 프레임에서 멈춘다
    }

    Log::Info("[enemy] -> {}", EnemyStateName(next));
}


// ----------------------------------------------------------------------------
//  적 공격 — 플레이어의 AttackActive / AttackHitbox 와 완전히 대칭이다
// ----------------------------------------------------------------------------
const AttackData& PlayScene::EnemyAttack() const
{
    // ★ 자세가 어느 공격인지를 정한다.
    //   기획서 3.2.1 의 「입력 맥락이 어느 공격인지 결정한다」와 같은 발상 —
    //   플레이어는 입력 맥락으로, 적은 자세로 무브셋을 고른다.
    return m_enemy.attackIsBite ? kEnemyBite : kEnemySwing;
}


float PlayScene::EnemyAttackRange() const
{
    // 이쪽은 latch 가 아니라 현재 자세를 본다 —
    // 「지금 다가갈까 공격할까」를 판단하는 값이므로 최신이어야 한다.
    return EnemyLegsBroken() ? kEnemyBiteRange : kEnemySwingRange;
}


bool PlayScene::EnemyInAttackPosition() const
{
    const float dx = std::abs(m_player.x - m_enemy.x);
    const float dy = std::abs(m_player.y - m_enemy.y);

    // ★ 세로를 따로 보는 것이 요점이다. 자세한 이유는 헤더의 그림 참조.
    return dx <= EnemyAttackRange()
        && dy <= kEnemyAttackYTolerance;
}


bool PlayScene::EnemyAttackActive() const
{
    if (m_enemy.state != EnemyState::Attack)
        return false;

    const AttackData& a = EnemyAttack();
    return m_enemy.stateTicks >= a.startup
        && m_enemy.stateTicks <  a.startup + a.active;
}


bool PlayScene::EnemyTelegraph() const
{
    return m_enemy.state == EnemyState::Attack
        && m_enemy.stateTicks < EnemyAttack().startup;
}


AABB PlayScene::EnemyAttackHitbox() const
{
    return MakeAttackBox(m_enemy.x, m_enemy.y, m_enemy.facing, EnemyAttack());
}


// ----------------------------------------------------------------------------
//  TryEnemyAttack — ★★ 5-d 의 무적 프레임이 드디어 의미를 갖는 곳
// ----------------------------------------------------------------------------
void PlayScene::TryEnemyAttack(SceneContext& ctx)
{
    if (!EnemyAttackActive())
        return;

    // active 가 4틱이면 이것 없이는 한 번 휘두를 때 데미지가 4번 들어간다.
    if (m_enemy.hitThisSwing || PlayerDead())
        return;

    if (!Intersects(EnemyAttackHitbox(), PlayerHurtbox()))
        return;

    // ★ 무적 처리 방식 (b) — 「무적인 틱은 없었던 일」.
    //
    //   휘두르기를 **소진시키지 않는다**(hitThisSwing 을 세우지 않는다).
    //   그래서 무적이 풀린 다음 틱에 active 가 남아 있으면 그때 맞는다.
    //   「무적 프레임」이 문자 그대로 동작한다.
    //
    //   무적으로 흘린 것을 소진 처리하는 것은 별개 규칙이고,
    //   그건 나중에 **패링(parry)** 을 만들 때 쓸 개념이다. 지금 섞지 않는다.
    //
    //   invincible 12틱 > active 4틱 이므로, 타이밍만 맞으면 완전 회피가 된다.
    if (Invincible())
    {
        Log::Info("[play] ★ 회피 — {} 무적으로 흘렸다 (적 t{})",
                  RollInvincible() ? "구르기" : "피격", m_enemy.stateTicks);
        return;
    }

    m_enemy.hitThisSwing = true;
    HitPlayer(ctx, EnemyAttack());
}


void PlayScene::MoveEnemyTowardPlayer(float speedPerTick)
{
    const float dx = m_player.x - m_enemy.x;
    const float dy = m_player.y - m_enemy.y;
    const float dist = std::sqrt(dx * dx + dy * dy);

    // 바라보는 방향은 거리와 무관하게 갱신한다
    if (dx < -1.0f)     m_enemy.facing = -1;
    else if (dx > 1.0f) m_enemy.facing = +1;

    // ★ 공격 위치에 도달하면 멈춘다.
    //   「멈추는 조건」과 「공격하는 조건」이 **같은 함수**다 —
    //   따로 두면 「멈췄는데 닿지 않는」 적이 생긴다.
    if (EnemyInAttackPosition() || dist <= 0.0001f)
        return;

    m_enemy.x += dx / dist * speedPerTick;
    m_enemy.y += dy / dist * speedPerTick;

    m_enemy.x = std::clamp(m_enemy.x, kOriginX,
                           static_cast<float>(Config::kCanvasWidth) - kOriginX);
    m_enemy.y = std::clamp(m_enemy.y, kOriginY,
                           static_cast<float>(Config::kCanvasHeight));
}


void PlayScene::UpdateEnemy(SceneContext& ctx)
{
    ++m_enemy.stateTicks;
    m_enemyAnim.Tick();
    if (m_enemy.flash > 0)
        --m_enemy.flash;

    // 쿨다운도 시간이다. 상태와 무관하게 흐른다.
    if (m_enemy.attackCooldown > 0)
        --m_enemy.attackCooldown;

    if (m_enemy.state == EnemyState::Dead)
        return;

    const float dx = m_player.x - m_enemy.x;
    const float dy = m_player.y - m_enemy.y;
    const float dist = std::sqrt(dx * dx + dy * dy);

    // 공격 위치 + 쿨다운 끝 + 플레이어 생존. Chase 와 Crawl 이 공유하는 조건.
    const bool canAttack = EnemyInAttackPosition()
                        && (m_enemy.attackCooldown <= 0)
                        && !PlayerDead();

    switch (m_enemy.state)
    {
    case EnemyState::Idle:
        if (dist <= kEnemySightRange)
            ChangeEnemyState(ctx, EnemyLegsBroken() ? EnemyState::Crawl
                                                    : EnemyState::Chase);
        break;

    case EnemyState::Chase:
        // ★ 부위 파괴가 행동을 바꾸는 지점.
        //   다리가 부서지면 죽지 않고 기어 다닌다.
        if (EnemyLegsBroken())
        {
            ChangeEnemyState(ctx, EnemyState::Crawl);
            break;
        }

        if (canAttack)
            ChangeEnemyState(ctx, EnemyState::Attack);
        else
            MoveEnemyTowardPlayer(kEnemyWalkPerTick);
        break;

    case EnemyState::Crawl:
        // 기어가는 중에는 다시 일어나지 않는다. 다리는 회복되지 않는다.
        //
        // ★ 하지만 **무해하지는 않다.** 사거리에 들어오면 물어뜯는다.
        //   「다리를 부수면 완전 무력화」로 만들면 항상 다리부터 노리는 것이
        //   정답이 되어, 기획서 3.2 의 전술적 선택이 사라진다.
        if (canAttack)
            ChangeEnemyState(ctx, EnemyState::Attack);
        else
            MoveEnemyTowardPlayer(kEnemyCrawlPerTick);
        break;

    case EnemyState::Attack:
        // ★ 이동하지 않는다. 플레이어의 Attack 과 같다 —
        //   한 번 휘두르면 끝까지 간다. 그래서 플레이어가 **걸어서 빠져나갈 수도**
        //   있다. 회피 수단이 구르기 하나만인 게임이 되지 않는다.
        TryEnemyAttack(ctx);

        // 상태 길이는 프레임 데이터가 정한다. 애니메이션 Finished() 를 보지 않는다.
        if (m_enemy.stateTicks >= EnemyAttack().TotalTicks())
        {
            m_enemy.attackCooldown = kEnemyAttackCooldown;
            ChangeEnemyState(ctx, EnemyLegsBroken() ? EnemyState::Crawl
                                                    : EnemyState::Chase);
        }
        break;

    default:
        break;
    }
}


AABB PlayScene::EnemyPartBox(int part) const
{
    // ★ 자세를 **상태가 아니라 몸으로 판정한다.**
    //
    //   5-e-2 에서는 `state == Crawl` 로 골랐다. 그때는 엎드린 적이 가질 수 있는
    //   상태가 Crawl 뿐이었으니 맞는 코드였다.
    //   그런데 5-e-3 에서 Attack 이 생기자 **엎드린 적이 Attack 상태**가 될 수
    //   있게 되었고, 그 순간 상자가 서 있는 위치로 튀어 공중에 떴다.
    //   물어뜯는 0.9초 내내 판정과 그림이 어긋나고, 예고 `!` 도 허공에 떴다.
    //
    //   자세는 **행동이 아니라 몸의 상태**다. 다리가 부서졌으면 무엇을 하든
    //   엎드려 있다. 그래서 EnemyLegsBroken() 으로 판정한다.
    //   상태를 하나 더 추가해도 이 코드는 다시 안 고친다.
    //
    //   ※ handoff.md §8 의 「자세별 판정 상자」 함정을, 상태를 늘리면서
    //     같은 자리에서 다시 밟은 것이다. 조건을 「상태 목록」으로 쓰면
    //     상태가 늘어날 때마다 빠뜨린다. 「몸의 성질」로 쓰면 안 그렇다.
    const PartBox& b = EnemyLegsBroken()
        ? kBoxCrawl[part]
        : kBoxStand[part];

    // ★ 좌우 비대칭 자세를 위해 facing 에 따라 x 를 뒤집는다.
    //   [left, right] 를 0 기준으로 뒤집으면 [-right, -left] 가 된다.
    //   서 있는 자세는 대칭이라 이 연산이 아무 영향을 주지 않는다 — 한 갈래로 처리된다.
    float left  = b.left;
    float right = b.right;
    if (m_enemy.facing < 0)
    {
        left  = -b.right;
        right = -b.left;
    }

    return {
        m_enemy.x + left,
        m_enemy.y + b.top,
        m_enemy.x + right,
        m_enemy.y + b.bottom
    };
}


int PlayScene::PickHitPart(const AABB& attack) const
{
    int   best     = -1;
    float bestArea = 0.0f;

    for (int i = 0; i < Part_Count; ++i)
    {
        // ★ 이미 부서진 부위는 건너뛴다.
        //   다리를 부순 뒤에는 같은 높이로 휘둘러도 다른 부위에 닿는다.
        if (m_enemy.hp[i] <= 0)
            continue;

        const float area = OverlapArea(attack, EnemyPartBox(i));
        if (area > bestArea)
        {
            bestArea = area;
            best     = i;
        }
    }
    return best;
}


void PlayScene::DrawEnemyDebug(Renderer& renderer) const
{
    for (int i = 0; i < Part_Count; ++i)
    {
        const bool broken = (m_enemy.hp[i] <= 0);

        // 부서진 부위는 어둡게, 살아 있는 부위는 노랗게
        renderer.DrawRectOutline(EnemyPartBox(i),
            broken ? DirectX::Colors::DimGray : DirectX::Colors::Gold, 1.0f);

        // 부서진 부위는 대각선 대신 반투명 판으로 덮어 표시한다
        if (broken)
        {
            renderer.DrawFilledRect(EnemyPartBox(i),
                DirectX::XMVectorSet(0.1f, 0.1f, 0.1f, 0.45f));
        }
    }
}


void PlayScene::Render(Renderer& renderer)
{
    // ---- 적 ----
    DirectX::XMVECTOR enemyTint = DirectX::Colors::White;
    if (EnemyDead())                  enemyTint = DirectX::XMVectorSet(0.35f, 0.30f, 0.32f, 1.0f);
    else if (m_enemy.flash > 0)       enemyTint = DirectX::XMVectorSet(1.00f, 0.75f, 0.70f, 1.0f);
    else if (EnemyAttackActive())     enemyTint = DirectX::XMVectorSet(1.00f, 0.45f, 0.35f, 1.0f);
    else if (EnemyTelegraph())        enemyTint = DirectX::XMVectorSet(1.00f, 0.78f, 0.60f, 1.0f);

    // 시트는 오른쪽을 보고 그려져 있으므로 facing 이 -1 일 때 뒤집는다
    const DirectX::SpriteEffects enemyFx = (m_enemy.facing < 0)
        ? DirectX::SpriteEffects_FlipHorizontally
        : DirectX::SpriteEffects_None;

    const RECT enemySrc = m_enemyAnim.SourceRect(kCellW, kCellH);
    renderer.Sprites().Draw(
        m_enemySheet.Get(),
        DirectX::XMFLOAT2(std::round(m_enemy.x), std::round(m_enemy.y)),
        &enemySrc, enemyTint,
        0.0f,
        DirectX::XMFLOAT2(kOriginX, kOriginY),
        1.0f,
        enemyFx);

    // ---- ★ 공격 예고 (telegraph) ----
    //   startup 구간에만 머리 위에 뜬다.
    //
    //   적의 startup 이 24틱이든 60틱이든, **플레이어가 그 시작을 볼 수 없으면
    //   회피는 운이다.** 격투게임과 소울류가 예외 없이 예고를 주는 이유다.
    //
    //   ★ 예고가 두 겹이고, 그게 그대로 기획서의 설계다.
    //
    //     ① 애니메이션 (팔을 뒤로 젖힌다)  — 누구에게나 보인다. **읽는 기술이 필요하다**
    //     ② 이 `!` 표시                    — 기획서 1.1 의 「初心者の指輪」
    //
    //     ①만 있으면 초보자는 반응하지 못하고, ②만 있으면 숙련의 여지가 없다.
    //     지금은 ②를 항상 보이게 두고, 나중에 이 if 를 지문 장착 여부로 감싸면
    //     「초보자 반지를 빼면 모션만으로 읽어야 한다」가 성립한다.
    //     기획서 1.1 의 「지문은 능력이 아니라 인식을 바꾼다」가 이 한 줄이다.
    //
    //   월드 레이어에 그리므로 카메라·흔들림을 같이 타고 적에 붙어 따라온다.
    if (EnemyTelegraph())
    {
        const AABB head = EnemyPartBox(Part_Head);
        renderer.DrawString("!",
            std::round((head.left + head.right) * 0.5f - 8.0f),   // 8x14 폰트 x2 = 폭 16
            std::round(head.top - 30.0f),
            DirectX::Colors::Red, 2);
    }

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

    const DirectX::SpriteEffects fx = (m_player.facing < 0)
        ? DirectX::SpriteEffects_FlipHorizontally
        : DirectX::SpriteEffects_None;

    // ★ 그릴 때는 정수 좌표로. 소수 위치에 그리면 시트의 옆 칸을 물어온다.
    const DirectX::XMFLOAT2 drawPos{
        std::round(m_player.x),
        std::round(m_player.y)
    };

    const RECT src = m_playerAnim.SourceRect(kCellW, kCellH);
    renderer.Sprites().Draw(
        m_sheet.Get(), drawPos, &src, tint,
        0.0f,
        DirectX::XMFLOAT2(kOriginX, kOriginY),
        1.0f,
        fx);

    // ---- 디버그 표시 (F1) ----
    if (m_showDebug)
    {
        renderer.DrawRectOutline(SpriteBounds(), DirectX::Colors::SlateGray);

        // ★ hurtbox 는 항상 그린다. 색만 바꿔서 무적을 보여 준다.
        //   빈 사각형으로 만들었다면 이 표시가 사라져 「지금 무적인가」를 못 본다.
        //   그리고 무적의 **종류까지** 색으로 나눈다.
        DirectX::XMVECTOR hurtColor = DirectX::Colors::Lime;
        if (RollInvincible())       hurtColor = DirectX::Colors::DeepSkyBlue;  // ① 구르기
        else if (m_invulnTicks > 0) hurtColor = DirectX::Colors::Yellow;       // ② 피격
        renderer.DrawRectOutline(PlayerHurtbox(), hurtColor, 2.0f);

        DrawEnemyDebug(renderer);

        // ★ 적 공격 히트박스 — active 구간에서만 나타난다.
        //   , 로 멈추고 . 로 밟으면 swing 은 t26 에 나타나 t29 까지 있다.
        //   그동안 플레이어 hurtbox 가 파란색(구르기 무적)이면 흘러간다.
        if (EnemyAttackActive())
        {
            renderer.DrawFilledRect(EnemyAttackHitbox(),
                DirectX::XMVectorSet(1.0f, 0.55f, 0.10f, 0.35f));
            renderer.DrawRectOutline(EnemyAttackHitbox(),
                DirectX::Colors::Orange, 2.0f);
        }

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
            { m_player.x - 5.0f, m_player.y - 1.0f, m_player.x + 5.0f, m_player.y + 1.0f },
            DirectX::Colors::Magenta);
        renderer.DrawFilledRect(
            { m_player.x - 1.0f, m_player.y - 5.0f, m_player.x + 1.0f, m_player.y + 5.0f },
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
        const AttackData& a = kDaggerLight;
        renderer.DrawString(
            std::format("STATE ATTACK  t{:<3}{}   [{} {} {}]",
                        m_stateTicks, AttackPhase(m_stateTicks, a),
                        a.startup, a.active, a.recovery),
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
            std::format("STATE {}  t{}{}", StateName(m_state), m_stateTicks,
                        (m_invulnTicks > 0)
                            ? std::format("   invuln {}", m_invulnTicks)
                            : std::string{}),
            6.0f, 6.0f,
            (m_state == PlayerState::Exhausted || PlayerDead())
                ? DirectX::Colors::Red
                : DirectX::Colors::Orange, 1);
    }

    if (m_showDebug)
    {
        renderer.DrawString(
            std::format("stam {:6.1f} / {:.0f}   regen delay {:2}",
                        m_stamina.current, kStaminaMax, m_stamina.delay),
            6.0f, 20.0f, DirectX::Colors::Gainsboro, 1);
    }

    if (m_showDebug)
    {
        // HP 바가 -38 로 들어왔으므로 범례를 위로 올린다.
        renderer.DrawString("green/blue/yellow=hurtbox  red=my hit  orange=enemy hit",
                            6.0f, Config::kCanvasHeight - 66.0f,
                            DirectX::Colors::Lime, 1);

        // 부위별 HP. 아래에서 위 순서로 쌓아 올린다.
        for (int i = 0; i < Part_Count; ++i)
        {
            const bool broken = (m_enemy.hp[i] <= 0);
            renderer.DrawString(
                std::format("{:<6}{:>4}/{:<4}{}", kPartName[i],
                            std::max(0, m_enemy.hp[i]), kPartMaxHp[i],
                            broken ? " BROKEN" : ""),
                Config::kCanvasWidth - 150.0f,
                40.0f + i * 14.0f,
                broken ? DirectX::Colors::DimGray : DirectX::Colors::Gold, 1);
        }
    }

    // 적 상태 — 부위 파괴가 행동을 바꾸는 것을 눈으로 확인하는 표시
    if (m_enemy.state == EnemyState::Attack)
    {
        // ★ 적 공격도 플레이어와 같은 프레임 데이터 표시를 쓴다.
        //   AttackPhase() 를 그대로 재사용한다 — 구조가 같으니 도구도 같다.
        const AttackData& a = EnemyAttack();
        renderer.DrawString(
            std::format("ENEMY {} t{:<3}{}   [{} {} {}]  imp {}",
                        m_enemy.attackIsBite ? "BITE " : "SWING",
                        m_enemy.stateTicks, AttackPhase(m_enemy.stateTicks, a),
                        a.startup, a.active, a.recovery, a.impact),
            6.0f, 34.0f,
            EnemyAttackActive() ? DirectX::Colors::Red : DirectX::Colors::Orange, 1);
    }
    else
    {
        renderer.DrawString(
            std::format("ENEMY {}{}{}", EnemyStateName(m_enemy.state),
                        EnemyLegsBroken() ? "  (legs broken)" : "",
                        m_enemy.attackCooldown > 0
                            ? std::format("  cd {}", m_enemy.attackCooldown)
                            : std::string{}),
            6.0f, 34.0f,
            EnemyLegsBroken() ? DirectX::Colors::Orange : DirectX::Colors::Gold, 1);
    }

    if (EnemyDead())
        renderer.DrawStringCentered("ENEMY DOWN", Config::kCanvasWidth * 0.5f, 60.0f,
                                    DirectX::Colors::Gold, 2);

    // ★ 임시 표시. 기획서 3.6 은 「`You died` 대신 부활하면서 멋진 대사」이므로
    //   5-e-4 에서 DeathScene + 대사 테이블로 교체된다.
    if (PlayerDead())
    {
        renderer.DrawStringCentered("YOU DIED",
                                    Config::kCanvasWidth  * 0.5f,
                                    Config::kCanvasHeight * 0.5f - 20.0f,
                                    DirectX::Colors::Crimson, 3);
    }
}
