#include "Scenes/PlayScene.h"

#include "Scenes/DeathScene.h"
#include "Scenes/PauseScene.h"

#include "Core/BodyComponent.h"
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

    // ---- ★ 지형 충돌 상자. 피격 상자와 **다른 것**이다 ----
    //   피격 상자는 **부위 다섯**으로 나뉘지만 지형 상자는 **하나**다.
    //   지형은 「머리가 맞았나」를 묻지 않기 때문이다.
    //
    //   ★ 높이는 자세를 따라간다. 웅크리면 낮은 틈을 지나갈 수 있어야 한다.
    //     원점이 발밑이라 상자는 **위에서** 줄어든다 — 발은 그대로다.
    //     28 은 웅크린 그림(위끝 27)보다 1픽셀 넉넉한 값이다.
    //
    //   플레이어와 적이 같은 값을 쓴다 — 몸집이 비슷하기 때문이다.
    //   ※ 적은 웅크리지 않으므로 crouch 값을 쓸 일이 없다.
    // ---- ★ 카메라 ----
    //   데드존이 넓을수록 화면이 덜 움직인다. 가로를 세로보다 넓게 두는 것이
    //   보통이다 — 좌우 이동은 잦지만 점프는 금방 돌아오기 때문이다.
    constexpr float kDeadHalfW = 56.0f;
    constexpr float kDeadHalfH = 36.0f;

    //   따라갈 지점을 발밑에서 이만큼 올린다(가슴 높이).
    constexpr float kCameraEyeHeight = 24.0f;

    constexpr float kBodyHalfW        =  9.0f;
    constexpr float kBodyStandHeight  = 44.0f;
    constexpr float kBodyCrouchHeight = 28.0f;
    constexpr float kBodyProneHeight  = 26.0f;   // 엎드린 그림(위끝 25)보다 1픽셀 넉넉히

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
//  ⓪ 지형
//
//    ★ 발판은 **통과할 수 없다**(2026-09-14 결정). 위에서도 옆에서도 막힌다.
//      「위에서 내려올 때만 밟히는」 통과형은 옆면 충돌이 필요 없어 싸지만,
//      ↓ 로 내려가기와 묶이는 구조다. ↓ 는 나중에 **사다리**에 쓰기로 했으므로
//      발판은 그냥 벽으로 둔다.
//
//    ★ 화면 좌우 끝도 **지형**이다. 전에는 이동 코드마다 clamp 를 걸었는데,
//      벽으로 두면 걷기·구르기·넉백이 전부 같은 규칙으로 막힌다 —
//      「예외를 없애려면 그것도 규칙 안에 넣는다」.
// ============================================================================
void PlayScene::BuildLevel()
{
    const float w = Config::kWorldWidth;
    const float h = Config::kWorldHeight;
    const float g = Config::kGroundY;

    m_level.Clear();

    m_level.AddSolid({ 0.0f, g, w, h });                 // 바닥
    m_level.AddSolid({ -32.0f, -h, 0.0f, h });           // 왼쪽 벽 (월드 밖)
    m_level.AddSolid({ w, -h, w + 32.0f, h });           // 오른쪽 벽

    // ---- 발판 ----
    //   ★ 배치가 그냥 장식이 아니다. 두 가지 숫자에 묶여 있다:
    //
    //     ① 높이 차 40 < **점프 정점 55**.
    //        55 를 넘으면 영영 못 올라간다.
    //
    //     ② 가로 간격 <= 40.
    //        공중 제어력이 0.6 이라 공중에서는 틱당 1.5픽셀밖에 못 간다.
    //        높이 40 이상을 유지하는 구간이 약 20틱이므로 **30픽셀쯤**이
    //        건널 수 있는 거리다. 그보다 벌리면 보이는데 못 가는 발판이 된다.
    //
    //        280 바닥 -> 240 -> 200 -> 160
    //
    //   ★ 스폰 자리(플레이어 120 · 적 470)를 비워 둔다.
    //     몸 상자가 발판에 끼인 채 시작하면 밀려나면서 튄다.
    //
    //   가장 높은 곳에서 뛰어내리면 발끝이 적의 머리(발끝에서 37~55)를 지난다 —
    //   §3.8.2 의 「높은 데서 뛰어내려 머리를 노린다」가 여기서 처음 성립한다.
    //   ★ 세로로도 올라간다. 맵이 화면 두 장 높이(720)이므로 위로 240 을 오르면
    //     화면이 따라 올라간다 — 카메라가 두 축으로 움직이는 것을 확인할 수 있다.
    struct Plat { float x0, x1, up; };
    constexpr Plat kPlats[] = {
        // 왼쪽 — 계단으로 올라간다
        {  180.0f,  280.0f,  40.0f },
        {  320.0f,  440.0f,  80.0f },
        {  240.0f,  360.0f, 120.0f },
        {  400.0f,  520.0f, 160.0f },
        {  560.0f,  700.0f, 200.0f },
        // 가운데 — 높은 길
        {  740.0f,  880.0f, 200.0f },
        {  900.0f, 1020.0f, 160.0f },
        {  820.0f,  940.0f,  80.0f },
        // 오른쪽 — 다시 내려온다
        { 1060.0f, 1180.0f, 120.0f },
        { 1220.0f, 1340.0f,  80.0f },
        { 1140.0f, 1260.0f, 200.0f },
        { 1400.0f, 1540.0f,  40.0f },
        { 1580.0f, 1720.0f,  80.0f },
        { 1480.0f, 1620.0f, 120.0f },
        { 1660.0f, 1840.0f, 160.0f },
    };
    for (const Plat& p : kPlats)
        m_level.AddSolid({ p.x0, g - p.up, p.x1, g - p.up + 8.0f });
}


// ----------------------------------------------------------------------------
//  BuildBackdrop — 시차 배경
//
//    ★ 두 겹이면 충분하다. 깊이가 다른 것이 **두 개만 있어도** 눈은 거리를
//      읽는다. 세 겹째부터는 비용만 늘고 차이는 거의 없다.
//
//    ★ 배경은 지형이 아니다. Level 에 넣지 않는다 — 넣으면 밟히고 막힌다.
// ----------------------------------------------------------------------------
void PlayScene::BuildBackdrop()
{
    m_backdrop.clear();

    const float g = Config::kGroundY;

    // 먼 층 — 거의 안 움직인다. 높고 굵다.
    for (float x = -100.0f; x < Config::kWorldWidth + 200.0f; x += 230.0f)
        m_backdrop.push_back({ x, g - 300.0f, 46.0f, 0.22f });

    // 가까운 층 — 절반쯤 따라 움직인다. 낮고 가늘다.
    //   ★ 간격을 먼 층과 **서로소에 가깝게** 둔다(230 vs 167).
    //     배수로 두면 두 층이 주기적으로 겹쳐 「벽 하나」로 보인다.
    for (float x = 40.0f; x < Config::kWorldWidth + 200.0f; x += 167.0f)
        m_backdrop.push_back({ x, g - 170.0f, 26.0f, 0.55f });
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

    // ★ 팔은 몸과 **다른 시트**다 (design.md §8.1).
    //   몸 시트에는 팔이 아예 그려져 있지 않아서, 잘린 팔을 「지울」 수가 없다.
    //   그래서 팔을 겹쳐 그리고 잘리면 그 장을 안 그린다.
    //   네 장 모두 player.png 를 읽어 칸마다 맞춰 생성한 것이다
    //   (tools/gen_player_arms.ps1).
    auto armFrontSheet   = ctx.assets.Texture(L"assets/textures/player_arm_front.png");
    auto armBackSheet    = ctx.assets.Texture(L"assets/textures/player_arm_back.png");
    auto stumpFrontSheet = ctx.assets.Texture(L"assets/textures/player_stump_front.png");
    auto stumpBackSheet  = ctx.assets.Texture(L"assets/textures/player_stump_back.png");
    if (!armFrontSheet || !armBackSheet || !stumpFrontSheet || !stumpBackSheet)
        return false;

    // ★ 상속 계층을 짜지 않는다. 필요한 능력을 붙일 뿐이다.
    //
    //   붙인 순서 = 실행 순서다:
    //     Stamina(회복)   -> Controller(판단·이동) -> Sprite(애니메이션)
    //     Parts(번쩍임)   -> Brain(판단·이동)      -> Sprite
    //   Controller / Brain 이 Sprite 보다 먼저여야 이번 틱에 바꾼 클립이
    //   같은 틱에 반영된다.
    BuildLevel();
    BuildBackdrop();

    // ★ Body 를 **맨 앞에** 붙인다 = 「물리 먼저, 판단 나중」.
    //   컨트롤러가 Grounded() 를 읽을 때 이미 이번 틱의 결과가 들어 있다.
    //   반대로 붙이면 착지를 한 틱 늦게 알아채 그림이 한 틱 어긋난다.
    m_playerObj.Add<BodyComponent>(m_level, kBodyHalfW, kBodyStandHeight,
                                   kBodyCrouchHeight, kBodyProneHeight);
    m_playerObj.Add<StaminaComponent>();
    m_playerObj.Add<PoiseComponent>(kClothPoise);   // 값은 방어구가 덮어쓴다
    m_playerParts = &m_playerObj.Add<PartsComponent>(kPlayerParts);
    m_player = &m_playerObj.Add<PlayerController>();

    // 레이어를 더한 순서가 곧 위로 올라가는 순서다.
    //   ★ 뒷팔을 몸보다 「아래」에 넣을 필요가 없다 — 뒷팔은 몸통 **바깥**에만
    //     그려져 몸 픽셀을 덮지 않으므로 위에 그려도 결과가 같다.
    //     쓰지 않을 기능(아래 레이어)을 엔진에 만들지 않은 이유다.
    SpriteComponent& playerSprite = m_playerObj.Add<SpriteComponent>(playerSheet, kCellW, kCellH);
    m_player->SetArmLayers(
        playerSprite.AddLayer(armFrontSheet),
        playerSprite.AddLayer(armBackSheet),
        playerSprite.AddLayer(stumpFrontSheet, false),   // 상처는 잘린 뒤에만
        playerSprite.AddLayer(stumpBackSheet,  false));

    // ★ 적도 떨어진다. 같은 컴포넌트, 같은 숫자다.
    m_enemyObj.Add<BodyComponent>(m_level, kBodyHalfW, kBodyStandHeight,
                                  kBodyCrouchHeight, kBodyProneHeight);
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

    // ★ 첫 프레임부터 제자리를 비춘다. Update 가 돌기 전에 한 번 그려진다.
    UpdateCamera(ctx);

    Log::Info("[play] Arrows/WASD/Stick = move   Space = JUMP   Shift = roll");
    Log::Info("[play] LMB = attack   RMB = bite (팔이 없어도 된다)   LMB = 줍기(발밑)");
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
                   PanFromWorldX(m_enemyObj.transform.x, ctx.camera.X()));

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
    // ★ 적이 플레이어의 부위 상태를 **직접 보지 않는다.** Scene 이 이어 준다.
    //   엎드린 상대에게 휘두르면 몸 위로 지나가므로, 적은 물기로 바꿔야 한다.
    m_enemyBrain->SetTargetProne(m_playerParts->Prone());

    m_playerObj.Tick(ctx, consumeEdgeInput);
    UpdateWeaponPickup(ctx);
    TryPlayerHit(ctx);

    m_enemyObj.Tick(ctx, consumeEdgeInput);
    TryEnemyHit(ctx);

    // ★ **전부 움직인 뒤**에 따라간다. 먼저 움직이면 한 틱 뒤처진 곳을 비춘다.
    UpdateCamera(ctx);

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
//  UpdateCamera — 데드존 추적
//
//    ★ 발밑이 아니라 **가슴 높이**를 본다. 발밑을 따라가면 점프할 때마다
//      화면이 아래로 쏠린다 — 눈이 쫓는 것은 몸이지 발이 아니다.
//
//    ★★ Enter 에서도 부른다. 안 부르면 **첫 프레임만** 카메라가 (0,0) 에 있어
//      월드 왼쪽 위가 한 번 번쩍인다. 「처음 한 번」을 잊는 고전적인 자리다.
// ----------------------------------------------------------------------------
void PlayScene::UpdateCamera(SceneContext& ctx)
{
    const Transform& tr = m_playerObj.transform;

    ctx.camera.Follow(tr.x, tr.y - kCameraEyeHeight,
                      kDeadHalfW, kDeadHalfH,
                      Config::kCanvasWidth, Config::kCanvasHeight);

    ctx.camera.ClampTo({ 0.0f, 0.0f, Config::kWorldWidth, Config::kWorldHeight },
                       Config::kCanvasWidth, Config::kCanvasHeight);

    // Render 는 SceneContext 를 못 받으므로 여기에 적어 둔다.
    //   ※ 일시정지·사망 화면이 위에 떠 있는 동안에는 Update 가 안 돌므로
    //     이 값이 그대로 남는다 — 카메라가 멈춰 있는 것이 맞다.
    m_viewX = ctx.camera.X();
    m_viewY = ctx.camera.Y();
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
        ctx.audio.Play("ui_cancel", 0.7f, -0.5f, PanFromWorldX(tr.x, ctx.camera.X()));
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
    // ---- ★ 바닥 ----
    //   벨트스크롤일 때는 바닥이 「화면 아래쪽」이라는 암묵적인 것이었다.
    //   플랫포머에서는 **밟는 면**이 되었으므로 눈에 보여야 한다 —
    //   안 그리면 캐릭터가 허공에 떠 있는 것처럼 보인다.
    //
    //   ※ 발판 여러 장은 6-d 다. 지금은 한 면이라 사각형 하나로 충분하다.
    const float canvasW = static_cast<float>(Config::kCanvasWidth);
    const float canvasH = static_cast<float>(Config::kCanvasHeight);

    // ---- ★ 시차 배경 ----
    //   카메라가 x 만큼 움직였을 때 배경을 x * depth 만큼만 움직이려면,
    //   월드 좌표를 **반대로 x * (1 - depth) 만큼 되밀어** 그리면 된다.
    //   (그림은 카메라 변환을 이미 타고 있으므로)
    for (const BackPillar& p : m_backdrop)
    {
        const float dx = std::round(m_viewX * (1.0f - p.depth));
        const float dy = std::round(m_viewY * (1.0f - p.depth));

        // 멀수록 어둡다 — 공기 원근(aerial perspective). 색 하나로 거리가 읽힌다.
        const float tone = 0.045f + p.depth * 0.045f;

        renderer.DrawFilledRect(
            { p.x + dx, p.top + dy, p.x + p.width + dx, Config::kWorldHeight + dy },
            DirectX::XMVectorSet(tone, tone, tone * 1.35f, 1.0f));
    }

    // ★ 바닥도 발판도 **같은 목록**에서 나온다. 그려지는 것과 막는 것이
    //   같은 데이터라 「보이는데 안 막히는 발판」이 생길 수 없다.
    for (const AABB& s : m_level.Solids())
    {
        // ★ 화면 밖은 건너뛴다. 판정이 아니라 **보이는 범위**로 자른다 —
        //   전에는 캔버스(0~640)로 잘랐는데, 월드가 넓어지면서 그건
        //   「월드 왼쪽 세 화면」을 뜻하게 되었다.
        if (s.right <= m_viewX || s.left >= m_viewX + canvasW)  continue;
        if (s.bottom <= m_viewY || s.top >= m_viewY + canvasH)  continue;

        renderer.DrawFilledRect(s, DirectX::XMVectorSet(0.09f, 0.09f, 0.12f, 1.0f));

        // 윗면에 밝은 선 한 줄. 「여기가 발이 닿는 높이」를 픽셀 하나로 말한다.
        renderer.DrawFilledRect({ s.left, s.top, s.right, s.top + 1.0f },
                                DirectX::XMVectorSet(0.30f, 0.31f, 0.38f, 1.0f));
    }

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
        renderer.DrawString("gold=torso/head  purple=arm  red=my hit  orange=enemy hit",
                            6.0f, Config::kCanvasHeight - 66.0f,
                            DirectX::Colors::Lime, 1);
    }

    if (m_enemyBrain->IsDead())
    {
        renderer.DrawStringCentered("ENEMY DOWN", Config::kCanvasWidth * 0.5f, 60.0f,
                                    DirectX::Colors::Gold, 2);
    }
}
