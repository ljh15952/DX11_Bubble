#include "Scenes/PlayScene.h"

#include "Scenes/DeathScene.h"
#include "Scenes/PauseScene.h"

#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Audio/Audio.h"
#include "Gameplay/AttackData.h"
#include "Gameplay/EnemyBrain.h"
#include "Gameplay/PartsComponent.h"
#include "Gameplay/PlayerController.h"
#include "Gameplay/PoiseComponent.h"
#include "Gameplay/StaminaComponent.h"
#include "Gameplay/WeaponPickup.h"
#include "Graphics/Assets.h"
#include "Graphics/Camera.h"
#include "Graphics/Renderer.h"
#include "Graphics/SpriteComponent.h"
#include "Input/Input.h"

#include <DirectXColors.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <random>
#include <string>

namespace
{
    constexpr int kCellW = 64;
    constexpr int kCellH = 64;

    constexpr float kShakeStrength = 2.0f;
    constexpr int   kShakeTicks    = 8;
    constexpr int   kFlashTicks    = 9;

    // ============================================================================
    //  ★ 강인도 — 적 종류마다 다른 값을 갖는다
    //
    //    플레이어 공격의 impact 는 light 14 / crouch 14 / running 16 / thrust 18.
    //    잡몹을 15 로 두면 이렇게 갈린다:
    //
    //        LIGHT  14  stam 28   ✗ 못 끊는다
    //        CROUCH 14  stam 26   ✗
    //        RUN    16  stam 34   ✓ 끊는다
    //        THRUST 18  stam 34   ✓ (1타를 맞춰야 나온다)
    //
    //    ★ **끊을 수 있는 둘이 정확히 비싼 둘이다.**
    //      「적을 끊으려면 스태미나를 더 낸다」가 데이터만으로 성립한다.
    //      13 이면 전부 끊겨 선택이 사라지고, 20 이면 아무것도 못 끊어 죽는다.
    //
    //    6-g 에서 이 값이 enemies.json 으로 간다. 보스는 훨씬 높게 둔다.
    // ============================================================================
    constexpr int kGruntPoise = 15;
    constexpr int kClothPoise = 10;   // 플레이어 초기값. 방어구가 곧 덮어쓴다

    // ============================================================================
    //  ★ 부위 설정 — 같은 컴포넌트, 다른 값 (design.md §3.2.2)
    //
    //    ★ 다리가 부서지면 **둘 다 쓰러져 기어간다.** 플레이어라고 예외가 아니다.
    //
    //    플레이어 : 팔이 몸통의 완충재다.
    //      적 swing(18) 기준 -> 팔 2대 + 몸통 4대 = 6대에 죽는다.
    //      성한 쪽 팔로 몸을 돌려 막으면 2대를 더 번다 —
    //      대신 등을 보이는 대가를 치른다.
    //
    //    잡몹 : 팔이 없다(maxHp 0). 스프라이트에 팔이 그려져 있지 않고,
    //      팔을 주면 플레이어 공격 11대가 필요해져 너무 질겨진다.
    //      보스에게는 숫자만 넣으면 팔이 생긴다.
    //
    //    6-g 에서 이 두 덩어리가 그대로 JSON 이 된다.
    // ============================================================================
    constexpr PartsProfile kPlayerParts{
        /*maxHp*/ { /*head*/ 20, /*L.arm*/ 24, /*R.arm*/ 24, /*torso*/ 60, /*legs*/ 40 },
        /*proneWhenLegsBroken*/ true,    // ★ 쓰러져 기어간다. 적과 같다
    };

    constexpr PartsProfile kGruntParts{
        /*maxHp*/ { /*head*/ 20, /*L.arm*/  0, /*R.arm*/  0, /*torso*/ 100, /*legs*/ 40 },
        /*proneWhenLegsBroken*/ true,    // 기어다닌다
    };

    float RandomPitch(float spread)
    {
        static std::mt19937 rng{ 4321 };
        std::uniform_real_distribution<float> dist(-spread, spread);
        return dist(rng);
    }
}


// ============================================================================
//  ① 조립
// ============================================================================
bool PlayScene::Enter(SceneContext& ctx)
{
    auto playerSheet = ctx.assets.Texture(L"assets/textures/player.png");
    auto enemySheet  = ctx.assets.Texture(L"assets/textures/enemy.png");
    if (!playerSheet || !enemySheet)
        return false;

    // ★ 상속 계층을 짜지 않는다. 필요한 능력을 붙일 뿐이다.
    //
    //   붙인 순서 = 실행 순서다:
    //     Stamina(회복)   -> Controller(판단·이동) -> Sprite(애니메이션)
    //     Parts(번쩍임)   -> Brain(판단·이동)      -> Sprite
    //   Controller / Brain 이 Sprite 보다 먼저여야 이번 틱에 바꾼 클립이
    //   같은 틱에 반영된다.
    m_playerObj.Add<StaminaComponent>();
    m_playerObj.Add<PoiseComponent>(kClothPoise);   // 값은 방어구가 덮어쓴다
    m_playerParts = &m_playerObj.Add<PartsComponent>(kPlayerParts);
    m_player = &m_playerObj.Add<PlayerController>();
    m_playerObj.Add<SpriteComponent>(playerSheet, kCellW, kCellH);

    m_enemyParts = &m_enemyObj.Add<PartsComponent>(kGruntParts);
    m_enemyPoise = &m_enemyObj.Add<PoiseComponent>(kGruntPoise);
    m_enemyBrain = &m_enemyObj.Add<EnemyBrain>(m_playerObj.transform);
    m_enemyObj.Add<SpriteComponent>(enemySheet, kCellW, kCellH);

    // ★ 떨어진 무기도 GameObject 다. 위치가 있고 그려지므로 Transform 이 필요하고,
    //   플레이어·적과 같은 그릇에 담기면 「월드에 있는 것」이 한 종류가 된다 —
    //   나중에 상자·함정·투사체가 생겨도 같은 방식으로 붙는다.
    m_pickup = &m_weaponObj.Add<WeaponPickup>();

    // Start 는 **전부 붙은 뒤**에 부른다 — 컴포넌트들이 서로를 찾는 시점이다.
    m_playerObj.Start(ctx);
    m_enemyObj.Start(ctx);
    m_weaponObj.Start(ctx);

    Log::Info("[play] Arrows/WASD/Stick = move   Space = attack   Shift = roll");
    Log::Info("[play] LMB / Space = attack   RMB = bite (팔이 없어도 된다)");
    Log::Info("[play] Ctrl = crouch (다리를 노린다)   Esc = pause");
    Log::Info("[play] F1 = hitbox   F2 = swap armor   F3 = stats");
    Log::Info("[play] ,  = freeze    . = step 1 tick    / = slow motion (1/8)");
    Log::Info("[play] TIP: 공격 -> 후딜 중에 다시 공격 = 2타(THRUST). 머리 높이다");
    Log::Info("[play] TIP: 적 머리 위 `!` 가 예고다. 그동안 Shift 로 구르면 흘린다");
    return true;
}


void PlayScene::Respawn(SceneContext& ctx)
{
    // ★ 무엇을 되돌릴지는 **각자가 안다.** Scene 은 「되돌려라」만 말한다.
    //   design.md §3.6.1 의 「되돌아간다 / 남는다」 표가 각 컴포넌트 안에 있다.
    m_player->Respawn(ctx);
    m_enemyBrain->Reset(ctx);

    // ★ m_weaponObj 를 **건드리지 않는다.**
    //   떨어진 무기는 그 자리에 그대로 남고, 부활한 뒤 다시 주우러 간다.
    //   design.md §3.6.1 의 「남는다」 칸이 여기서 실체를 갖는다 —
    //   **아무것도 안 하는 것이 기능**인 드문 경우다.
}


void PlayScene::Resume(SceneContext& ctx)
{
    // ★ PauseScene 이 닫힌 경우와 DeathScene 이 닫힌 경우를 **상태로 구분한다.**
    //   덕분에 DeathScene 은 PlayScene 을 알 필요가 없고 둘 사이에 포인터가 없다.
    if (!m_player->IsDead())
        return;

    Respawn(ctx);
    ctx.audio.Play("ui_confirm", 0.7f, -0.35f);
    Log::Info("[play] 부활 — 적도 되살아났다");
}


// ============================================================================
//  ② 판정 — 두 몸 사이의 일
//
//    ★ 두 함수가 **같은 모양**인 것을 보라. 방향만 반대다.
//      컴포넌트 안에 넣었다면 한쪽이 다른 쪽을 알아야 하고 대칭이 깨진다.
// ============================================================================

// ---- 플레이어 → 적 ----
void PlayScene::TryPlayerHit(SceneContext& ctx)
{
    if (!m_player->AttackActive())   return;
    if (m_player->HitThisSwing())    return;   // active 3틱 = 데미지 3번을 막는다
    if (m_enemyBrain->IsDead())      return;

    const int part = m_enemyParts->PickHit(m_player->AttackHitbox(),
                                           m_playerObj.transform.x);
    if (part < 0)
        return;

    const AttackData& atk = m_player->CurrentAttack();

    m_player->MarkHitThisSwing();
    m_enemyParts->Flash(kFlashTicks);
    m_enemyParts->Damage(part, atk.damage);

    // ★ 소리와 흔들림은 "맞는 순간" 에 낸다. 휘두르는 순간이 아니다.
    ctx.camera.Shake(kShakeStrength, kShakeTicks);
    ctx.audio.Play("hit", 0.85f, RandomPitch(0.12f),
                   PanFromCanvasX(m_enemyObj.transform.x));

    Log::Info("[play] {} 로 {} 명중  dmg {}  남은 HP {}",
              atk.name, m_enemyParts->Name(part), atk.damage,
              std::max(0, m_enemyParts->Hp(part)));

    // ---- ★ 강인도 판정 : 적도 휘청인다 ----
    //   「휘청일지」는 두 몸 사이의 계산이므로 여기서 한다 —
    //   플레이어가 맞을 때(TryEnemyHit -> TakeHit)와 대칭이다.
    //
    //   ★ 이것이 예고(`!`)에 두 번째 용도를 준다.
    //     구르면 흘리고, 강하게 치면 **끊는다.**
    if (m_enemyPoise->WouldStagger(atk.impact))
    {
        const bool wasWindingUp = m_enemyBrain->Telegraph();
        m_enemyBrain->Stagger(ctx, m_playerObj.transform.x, m_playerObj.transform.y);
        ctx.camera.Shake(kShakeStrength * 1.6f, kShakeTicks);
        Log::Info("[play] ★ 적 휘청임  impact {} > poise {}{}",
                  atk.impact, m_enemyPoise->Value(),
                  wasWindingUp ? "   — 공격을 끊었다!" : "");
    }

    if (!m_enemyParts->IsBroken(part))
        return;

    Log::Info("[play] ★ {} 파괴!", m_enemyParts->Name(part));

    // ★ 몸통과 머리는 격파. **다리는 부서져도 죽지 않는다** —
    //   기획서의 「다리만 베었는데 격파는 비현실적」이 여기서 해결된다.
    if (m_enemyParts->Fatal())
    {
        m_enemyBrain->Kill(ctx);
        Log::Info("[play] ★★ 적 격파 ({} 파괴)", m_enemyParts->Name(part));
    }
}


// ---- 적 → 플레이어 ----
void PlayScene::TryEnemyHit(SceneContext& ctx)
{
    if (!m_enemyBrain->AttackActive())  return;
    if (m_enemyBrain->HitThisSwing())   return;
    if (m_player->IsDead())             return;

    // ★ TryPlayerHit 와 **같은 모양**이다. 이제 양쪽 다 부위 판정을 한다.
    const int part = m_playerParts->PickHit(m_enemyBrain->AttackHitbox(),
                                            m_enemyObj.transform.x);
    if (part < 0)
        return;

    // ★★ 5-d 의 무적 프레임이 의미를 갖는 곳.
    //
    //   무적 처리 방식 (b) — 「무적인 틱은 없었던 일」.
    //   휘두르기를 **소진시키지 않는다.** 그래서 무적이 풀린 다음 틱에
    //   active 가 남아 있으면 그때 맞는다. 「무적 프레임」이 문자 그대로 동작한다.
    //   (무적으로 흘린 것을 소진 처리하는 것은 별개 규칙 = 나중의 패링이다)
    if (m_player->Invincible())
    {
        Log::Info("[play] ★ 회피 — {} 무적으로 흘렸다 (적 t{})",
                  m_player->RollInvincible() ? "구르기" : "피격",
                  m_enemyBrain->StateTicks());
        return;
    }

    m_enemyBrain->MarkHitThisSwing();
    m_playerParts->Flash(kFlashTicks);
    m_player->TakeHit(ctx, m_enemyBrain->CurrentAttack(), part,
                      m_enemyObj.transform.x, m_enemyObj.transform.y);
}


// ============================================================================
//  ③ 매 틱
// ============================================================================
void PlayScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    // ★ 틱 **전에** 알려 줘야 한다. 그래야 컨트롤러가 같은 Space 입력을
    //   공격이 아니라 줍기로 쓸지 판단할 수 있다.
    //   뒤에 알려 주면 「공격도 하고 줍기도 하는」 한 틱이 생긴다.
    m_player->SetPickupAvailable(
        m_pickup->Active()
        && m_player->Hand() == WeaponHand::None
        && Intersects(m_pickup->PickupArea(), m_playerParts->Box(Part_Torso)));

    // ★ 순서에 의미가 있다.
    //   플레이어를 먼저 굴리고, 그 결과(이번 틱의 위치·무적)를 보고 적이 움직인다.
    //   그리고 판정은 각자 움직인 **직후**에 한 번씩.
    m_playerObj.Tick(ctx, consumeEdgeInput);
    UpdateWeaponPickup(ctx);
    TryPlayerHit(ctx);

    m_enemyObj.Tick(ctx, consumeEdgeInput);
    TryEnemyHit(ctx);

    // ---- Scene 전환 ----
    //   ★ 사망 화면을 PlayerController 가 직접 띄우지 않는 이유:
    //     Scene 전환은 Scene 의 일이고, Gameplay 가 Scenes 를 알기 시작하면
    //     층이 뒤엉킨다. 컨트롤러는 「띄울 때가 됐다」까지만 말한다.
    if (m_player->ConsumeDeathScreenRequest())
        ctx.scenes.Push(std::make_unique<DeathScene>());

    if (consumeEdgeInput && ctx.input.PausePressed())
    {
        // 일시정지는 PausePressed 를 쓴다. 패드 B 가 구르기이므로
        // CancelPressed 를 쓰면 구를 때마다 일시정지가 걸린다.
        ctx.audio.Play("ui_cancel");
        ctx.scenes.Push(std::make_unique<PauseScene>());
    }
}


// ----------------------------------------------------------------------------
//  UpdateWeaponPickup — 떨구기와 줍기
// ----------------------------------------------------------------------------
void PlayScene::UpdateWeaponPickup(SceneContext& ctx)
{
    // ---- 떨군다 ----
    if (m_player->ConsumeWeaponDropRequest())
    {
        const Transform& tr = m_playerObj.transform;
        m_pickup->DropAt(tr.x, tr.y);
        ctx.audio.Play("ui_cancel", 0.7f, -0.5f, PanFromCanvasX(tr.x));
        Log::Info("[play] 무기가 땅에 떨어졌다 ({:.0f}, {:.0f})", tr.x, tr.y);
    }

    // ---- 줍는다 ----
    if (!m_player->ConsumePickupRequest())
        return;

    // ★ 어느 손에 드는가는 **남아 있는 팔**이 정한다.
    //   오른팔이 살아 있으면 오른손(무기 손), 아니면 왼손.
    //   왼손에 들면 보조 슬롯이 차서 나중에 횃불을 못 든다 — §1.2 의 기회비용.
    const WeaponHand hand =
          !m_playerParts->IsBroken(Part_RightArm) ? WeaponHand::Right
        : !m_playerParts->IsBroken(Part_LeftArm)  ? WeaponHand::Left
        :                                           WeaponHand::None;

    if (hand == WeaponHand::None)
    {
        Log::Info("[play] 팔이 없어 주울 수 없다");
        return;
    }

    m_pickup->PickedUp();
    m_player->EquipWeapon(hand);
    ctx.audio.Play("ui_confirm", 0.7f);
}


// ============================================================================
//  그리기
// ============================================================================
void PlayScene::Render(Renderer& renderer)
{
    // 바닥의 물건 -> 적 -> 플레이어 순. 뒤에 그린 것이 위에 보인다.
    m_weaponObj.Render(renderer);
    m_enemyObj.Render(renderer);
    m_playerObj.Render(renderer);

    // ★ 디버그는 **모든 그림이 끝난 뒤에** 그린다.
    //   붙인 순서가 곧 실행 순서라서, Parts 를 먼저 붙이면 판정 상자를
    //   스프라이트가 덮어 「히트박스가 뒤에 있는」 상태가 된다.
    //   틱 순서와 그리기 순서의 요구가 다르므로 패스를 나눈다.
    m_weaponObj.RenderDebug(renderer);
    m_enemyObj.RenderDebug(renderer);
    m_playerObj.RenderDebug(renderer);
}


void PlayScene::RenderUI(Renderer& renderer)
{
    // 각 컴포넌트가 자기 표시를 그린다. Scene 은 순서만 정한다.
    m_playerObj.RenderUI(renderer);
    m_enemyObj.RenderUI(renderer);

    if (renderer.DebugDraw())
    {
        renderer.DrawString("green/blue/yellow=hurtbox  red=my hit  orange=enemy hit",
                            6.0f, Config::kCanvasHeight - 66.0f,
                            DirectX::Colors::Lime, 1);
    }

    if (m_enemyBrain->IsDead())
    {
        renderer.DrawStringCentered("ENEMY DOWN", Config::kCanvasWidth * 0.5f, 60.0f,
                                    DirectX::Colors::Gold, 2);
    }
}
