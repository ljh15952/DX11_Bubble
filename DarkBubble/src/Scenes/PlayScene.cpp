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
    // ---- ★ 어둠 ----
    //   마스크 크기는 그림과 같아야 한다(tools/gen_light_mask.ps1).
    constexpr float kLightMaskW = 640.0f;
    constexpr float kLightMaskH = 320.0f;
    constexpr float kLightHeight = 28.0f;   // 빛의 중심 = 가슴 높이

    //   ★ 완전한 검정(1.0)이 아니다. 완전히 가리면 지형을 못 읽어 답답하고,
    //     무엇보다 **적의 `!` 예고가 안 보여** 불공평해진다.
    //     실루엣이 희미하게 남는 정도가 「어둠」이다.
    constexpr float kDarkAlpha = 0.88f;

    // ---- 손 슬롯 아이콘 ----
    constexpr float kHandSlotX = 48.0f;
    constexpr int   kIconSize  = 16;
    constexpr int   kIconEmpty = 0;    // ※ 지금은 안 쓴다. 빈손도 「문다」이므로
    constexpr int   kIconDagger = 1;
    constexpr int   kIconSevered = 2;
    constexpr int   kIconTeeth = 3;

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
    // ---- ★ 배치는 장식이 아니라 **세 개의 숫자에 묶여 있다** ----
    //
    //   ① 세로 간격 56  <  점프 정점 66
    //      넘으면 영영 못 올라간다.
    //
    //   ② ★★ 세로 **틈** = 56 - 두께 8 = 48  >  **몸 높이 44**
    //      이걸 놓쳐서 처음엔 못 올라갔다. 간격 40 / 두께 8 이면 틈이 32 인데
    //      몸이 44 라 **그 사이에 설 수가 없다.** 그 자리는 강제 웅크리기가 되고,
    //      웅크리면 점프가 막히므로 위로 갈 방법이 사라진다.
    //      「올라갈 수 있는가」는 점프 높이만의 문제가 아니다 —
    //      **올라가서 설 자리가 있는가**까지가 조건이다.
    //
    //   ③ 가로 간격 32.
    //      공중 제어력 0.6 이라 틱당 1.5픽셀. 높이 56 을 넘는 것이 11틱째이고
    //      그때 이미 16픽셀을 갔으므로, 건너야 할 거리(32 - 몸폭 18 = 14)를
    //      **넘어서** 도착한다. 벌리면 보이는데 못 가는 발판이 된다.
    //
    //   ★ 스폰 자리(플레이어 120 · 적 470)를 비워 둔다.
    //     몸 상자가 발판에 끼인 채 시작하면 밀려나면서 튄다.
    //
    //   ★ 세로로 224 를 오른다. 맵이 화면 두 장 높이라 카메라가 따라 올라간다.
    constexpr float kStep  = 56.0f;
    constexpr float kThick =  8.0f;

    struct Plat { float x0, x1; int level; };   // level 1~4 = kStep 의 배수
    constexpr Plat kPlats[] = {
        // 왼쪽 — 계단으로 올라간다
        {  180.0f,  300.0f, 1 },
        {  332.0f,  452.0f, 2 },
        {  260.0f,  380.0f, 3 },   // 위 발판과 x 가 겹친다 = 제자리 점프로 오른다
        {  412.0f,  532.0f, 4 },
        // 가운데 — 높은 길에서 내려온다
        {  564.0f,  700.0f, 4 },
        {  732.0f,  860.0f, 3 },
        {  892.0f, 1020.0f, 2 },
        { 1052.0f, 1180.0f, 1 },
        // 오른쪽 — 다시 올라갔다 내려온다
        { 1212.0f, 1340.0f, 2 },
        { 1372.0f, 1500.0f, 3 },
        { 1290.0f, 1410.0f, 4 },
        { 1540.0f, 1700.0f, 1 },
        { 1732.0f, 1860.0f, 2 },
    };
    for (const Plat& p : kPlats)
    {
        const float top = g - kStep * static_cast<float>(p.level);
        m_level.AddSolid({ p.x0, top, p.x1, top + kThick });
    }
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

    // ★★ 기둥을 **월드보다 훨씬 위에서** 시작한다.
    //   처음엔 지면 근처에서 시작했는데, 세로 시차가 배경을 아래로 밀어
    //   **지면에 가려 하나도 안 보였다.** 시차는 배경을 움직이는 만큼
    //   **화면 밖으로도 밀어낸다** — 그만큼 크게 그려 두어야 한다.
    //
    //   ★ 높이를 조금씩 다르게 한다. 같은 높이로 세우면 「벽 하나」가 된다.

    // 먼 층 — 거의 안 움직인다. 굵고 높다.
    int i = 0;
    for (float x = -300.0f; x < Config::kWorldWidth + 400.0f; x += 214.0f, ++i)
        m_backdrop.push_back({ x, -80.0f + static_cast<float>((i * 5) % 4) * 74.0f,
                               54.0f, 0.22f });

    // 가까운 층 — 절반쯤 따라 움직인다. 가늘고 낮다.
    //   ★ 간격을 먼 층과 **서로소에 가깝게** 둔다(214 vs 151).
    //     배수로 두면 두 층이 주기적으로 겹쳐 한 덩어리로 보인다.
    i = 0;
    for (float x = -200.0f; x < Config::kWorldWidth + 400.0f; x += 151.0f, ++i)
        m_backdrop.push_back({ x, 80.0f + static_cast<float>((i * 3) % 5) * 58.0f,
                               30.0f, 0.55f });
}


// ----------------------------------------------------------------------------
//  SpawnEnemies — **이 목록이 곧 앞으로의 맵 파일이다**
//
//    지금은 코드에 적혀 있지만 7단계에서 `Level` 의 사각형 목록과 함께
//    map.json 으로 나간다. 그래서 지금부터 **데이터 모양**으로 둔다 —
//    「좌표 목록을 훑어 만든다」가 파일에서 읽어 와도 그대로 성립한다.
//
//    ★ 스폰 자리를 서로 떨어뜨려 둔다. 같은 자리에서 시작하면 몸이 겹친 채로
//      출발하고, 밀어내기가 없으므로 그대로 붙어 다닌다.
// ----------------------------------------------------------------------------
void PlayScene::SpawnEnemies(SceneContext& ctx)
{
    (void)ctx;

    //   ★ 방향을 섞어 둔다. 셋 다 왼쪽(플레이어 쪽)을 보고 있으면
    //     6-f 에서 만든 「등 뒤로 다가간다」를 쓸 자리가 없다.
    constexpr EnemySpawn kSpawns[] = {
        {  470.0f, -1 },   // 마주 본다 — 정면으로 붙어야 한다
        {  980.0f, +1 },   // 등을 보인다 — 몰래 붙을 수 있다
        { 1480.0f, -1 },
    };

    m_enemies.clear();
    m_enemies.reserve(std::size(kSpawns));

    for (const EnemySpawn& s : kSpawns)
    {
        Enemy e;
        e.obj = std::make_unique<GameObject>("enemy");
        e.obj->transform.x      = s.x;
        e.obj->transform.facing = s.facing;

        // 붙인 순서 = 실행 순서. 플레이어와 **같은 구성**이다.
        e.obj->Add<BodyComponent>(m_level, kBodyHalfW, kBodyStandHeight,
                                  kBodyCrouchHeight, kBodyProneHeight);
        e.parts = &e.obj->Add<PartsComponent>(kGruntParts);
        e.poise = &e.obj->Add<PoiseComponent>(kGruntPoise);
        e.brain = &e.obj->Add<EnemyBrain>(m_playerObj.transform);

        m_enemies.push_back(std::move(e));
    }
}


const PlayScene::Enemy* PlayScene::NearestEnemy() const
{
    const Enemy* best = nullptr;
    float bestDist = 0.0f;

    for (const Enemy& e : m_enemies)
    {
        if (e.brain->IsDead()) continue;

        const float d = std::abs(e.Tr().x - m_playerObj.transform.x);
        if (!best || d < bestDist) { best = &e; bestDist = d; }
    }
    return best;
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

    m_lightMask = ctx.assets.Texture(L"assets/textures/light_mask.png");
    m_icons     = ctx.assets.Texture(L"assets/textures/icons.png");
    if (!m_lightMask || !m_icons)
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

    SpawnEnemies(ctx);
    for (Enemy& e : m_enemies)
        e.obj->Add<SpriteComponent>(enemySheet, kCellW, kCellH);

    // ★ 떨어진 무기도 GameObject 다. 위치가 있고 그려지므로 Transform 이 필요하고,
    //   플레이어·적과 같은 그릇에 담기면 「월드에 있는 것」이 한 종류가 된다 —
    //   나중에 상자·함정·투사체가 생겨도 같은 방식으로 붙는다.
    m_pickup = &m_weaponObj.Add<WeaponPickup>();

    // Start 는 **전부 붙은 뒤**에 부른다 — 컴포넌트들이 서로를 찾는 시점이다.
    m_playerObj.Start(ctx);
    for (Enemy& e : m_enemies)
        e.obj->Start(ctx);
    m_weaponObj.Start(ctx);

    // ★ 첫 프레임부터 제자리를 비춘다. Update 가 돌기 전에 한 번 그려진다.
    UpdateCamera(ctx);

    Log::Info("[play] Arrows/WASD/Stick = move   Space = JUMP   Shift = roll");
    Log::Info("[play] LMB = 왼손   RMB = 오른손   — 무기가 있으면 휘두르고 없으면 문다");
    Log::Info("[play] 발밑에 무기가 있으면 그 버튼이 **줍기**가 된다 (누른 손에 든다)");
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
    for (Enemy& e : m_enemies)
        e.brain->Reset(ctx);

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

    // ---- ★ 한 번 휘두르면 **한 명**만 ----
    //   전원을 때리게 두면 광역기가 공짜가 되고, 무리를 한 번에 정리할 수 있어
    //   시야(6-f)도 지형(6-d)도 다시 무의미해진다.
    //   ★ 그래서 맞은 순간 **빠져나간다** — `MarkHitThisSwing` 이 그 스윙을
    //     끝내므로, 뒤쪽 적은 다음 공격을 기다려야 한다.
    Enemy* target = nullptr;
    int    part   = -1;

    for (Enemy& e : m_enemies)
    {
        if (e.brain->IsDead()) continue;

        const int p = e.parts->PickHit(m_player->AttackHitbox(),
                                       m_playerObj.transform.x);
        if (p >= 0) { target = &e; part = p; break; }
    }
    if (!target)
        return;

    const AttackData& atk = m_player->CurrentAttack();

    m_player->MarkHitThisSwing();
    target->parts->Flash(kFlashTicks);
    target->parts->Damage(part, atk.damage);

    // ★ 소리와 흔들림은 "맞는 순간" 에 낸다. 휘두르는 순간이 아니다.
    ctx.camera.Shake(kShakeStrength, kShakeTicks);
    ctx.audio.Play("hit", 0.85f, RandomPitch(0.12f),
                   PanFromWorldX(target->Tr().x, ctx.camera.X()));

    Log::Info("[play] {} 로 {} 명중  dmg {}  남은 HP {}",
              atk.name, target->parts->Name(part), atk.damage,
              std::max(0, target->parts->Hp(part)));

    // ---- ★ 강인도 판정 : 적도 휘청인다 ----
    //   「휘청일지」는 두 몸 사이의 계산이므로 여기서 한다 —
    //   플레이어가 맞을 때(TryEnemyHit -> TakeHit)와 대칭이다.
    //
    //   ★ 이것이 예고(`!`)에 두 번째 용도를 준다.
    //     구르면 흘리고, 강하게 치면 **끊는다.**
    if (target->poise->WouldStagger(atk.impact))
    {
        const bool wasWindingUp = target->brain->Telegraph();
        target->brain->Stagger(ctx, m_playerObj.transform.x, m_playerObj.transform.y);
        ctx.camera.Shake(kShakeStrength * 1.6f, kShakeTicks);
        Log::Info("[play] ★ 적 휘청임  impact {} > poise {}{}",
                  atk.impact, target->poise->Value(),
                  wasWindingUp ? "   — 공격을 끊었다!" : "");
    }

    if (!target->parts->IsBroken(part))
        return;

    Log::Info("[play] ★ {} 파괴!", target->parts->Name(part));

    // ★ 몸통과 머리는 격파. **다리는 부서져도 죽지 않는다** —
    //   기획서의 「다리만 베었는데 격파는 비현실적」이 여기서 해결된다.
    if (target->parts->Fatal())
    {
        target->brain->Kill(ctx);
        Log::Info("[play] ★★ 적 격파 ({} 파괴)", target->parts->Name(part));
    }
}


// ---- 적 → 플레이어 ----
void PlayScene::TryEnemyHit(SceneContext& ctx)
{
    if (m_player->IsDead()) return;

    // ★ 적마다 따로 본다. 「한 번 휘두르면 한 명」은 **플레이어 쪽 규칙**이고,
    //   적 둘이 같은 틱에 때리는 것은 막지 않는다 — 그건 몰려 있는 대가다.
    for (Enemy& e : m_enemies)
    {
        if (!e.brain->AttackActive()) continue;
        if (e.brain->HitThisSwing())  continue;

        // ★ TryPlayerHit 와 **같은 모양**이다. 이제 양쪽 다 부위 판정을 한다.
        const int part = m_playerParts->PickHit(e.brain->AttackHitbox(), e.Tr().x);
        if (part < 0)
            continue;

        // ★★ 5-d 의 무적 프레임이 의미를 갖는 곳.
        //
        //   무적 처리 방식 (b) — 「무적인 틱은 없었던 일」.
        //   휘두르기를 **소진시키지 않는다.** 그래서 무적이 풀린 다음 틱에
        //   active 가 남아 있으면 그때 맞는다. 「무적 프레임」이 문자 그대로 동작한다.
        //   (무적으로 흘린 것을 소진 처리하는 것은 별개 규칙 = 나중의 패링이다)
        //
        //   ★ 적이 여럿이면 이 한 줄의 값어치가 커진다 — 구르기 한 번으로
        //     **동시에 들어온 둘을 다** 흘린다. 무적은 공격마다가 아니라 틱마다다.
        if (m_player->Invincible())
        {
            Log::Info("[play] ★ 회피 — {} 무적으로 흘렸다 (적 t{})",
                      m_player->RollInvincible() ? "구르기" : "피격",
                      e.brain->StateTicks());
            continue;
        }

        e.brain->MarkHitThisSwing();
        m_playerParts->Flash(kFlashTicks);
        m_player->TakeHit(ctx, e.brain->CurrentAttack(), part,
                          e.Tr().x, e.Tr().y);
    }
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
    //
    // ★★ 「나보다 가까운 동료가 있는가」도 Scene 이 알려 준다.
    //   적끼리 서로를 알 필요가 없다 — **둘 사이의 일은 둘을 다 아는 쪽이** 한다.
    //   두 몸 사이의 판정을 Scene 이 하는 것과 같은 이유다.
    {
        const Enemy* front = NearestEnemy();
        for (Enemy& e : m_enemies)
        {
            e.brain->SetTargetProne(m_playerParts->Prone());
            e.brain->SetYieldRoom(front && front != &e);
        }
    }

    m_playerObj.Tick(ctx, consumeEdgeInput);
    UpdateWeaponPickup(ctx);
    TryPlayerHit(ctx);

    for (Enemy& e : m_enemies)
        e.obj->Tick(ctx, consumeEdgeInput);
    TryEnemyHit(ctx);

    // ★ **전부 움직인 뒤**에 따라간다. 먼저 움직이면 한 틱 뒤처진 곳을 비춘다.
    UpdateCamera(ctx);

    // ---- Scene 전환 ----
    //   ★ 사망 화면을 PlayerController 가 직접 띄우지 않는 이유:
    //     Scene 전환은 Scene 의 일이고, Gameplay 가 Scenes 를 알기 시작하면
    //     층이 뒤엉킨다. 컨트롤러는 「띄울 때가 됐다」까지만 말한다.
    if (m_player->ConsumeDeathScreenRequest())
        ctx.scenes.Push(std::make_unique<DeathScene>());

    // ★ 임시 키. 밝기는 **비교해 봐야** 정할 수 있다.
    if (consumeEdgeInput && ctx.input.DarkTogglePressed())
    {
        m_dark = !m_dark;
        Log::Info("[play] (F5) 어둠 {}", m_dark ? "ON" : "OFF");
    }

    if (consumeEdgeInput && ctx.input.PausePressed())
    {
        // 일시정지는 PausePressed 를 쓴다. 패드 B 가 구르기이므로
        // CancelPressed 를 쓰면 구를 때마다 일시정지가 걸린다.
        ctx.audio.Play("ui_cancel");
        ctx.scenes.Push(std::make_unique<PauseScene>());
    }
}


// ----------------------------------------------------------------------------
//  DrawDarkness — 어둠 (design.md §3.9 A)
//
//    ★ 마스크 한 장을 플레이어 위에 덮고, **그 밖은 사각형 넷으로** 채운다.
//      마스크가 화면보다 작으므로 남는 자리를 안 채우면 거기만 훤해진다.
// ----------------------------------------------------------------------------
void PlayScene::DrawDarkness(Renderer& renderer)
{
    if (!m_dark || !m_lightMask)
        return;

    const Transform& tr = m_playerObj.transform;

    // 빛의 중심은 발밑이 아니라 **가슴**이다. 발밑에 두면 머리 위가 어둡다.
    const float lx = std::round(tr.x);
    const float ly = std::round(tr.y - kLightHeight);

    const float halfW = kLightMaskW * 0.5f;
    const float halfH = kLightMaskH * 0.5f;

    // ★ 진하기는 **여기서** 정한다. 그림에는 굽지 않았다 —
    //   그래야 장소마다 다르게 두어도 그림을 다시 안 만든다.
    const DirectX::XMVECTOR tint =
        DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, kDarkAlpha);

    const DirectX::XMFLOAT2 pos{ lx, ly };
    renderer.Sprites().Draw(
        m_lightMask.Get(), pos, nullptr, tint,
        0.0f, DirectX::XMFLOAT2(halfW, halfH), DirectX::XMFLOAT2(1.0f, 1.0f),
        (tr.facing < 0) ? DirectX::SpriteEffects_FlipHorizontally
                        : DirectX::SpriteEffects_None);

    // ---- 마스크 밖 ----
    //   보이는 범위(카메라)를 기준으로 위·아래·좌·우 넷.
    const float vl = m_viewX;
    const float vt = m_viewY;
    const float vr = m_viewX + static_cast<float>(Config::kCanvasWidth);
    const float vb = m_viewY + static_cast<float>(Config::kCanvasHeight);

    const float ml = lx - halfW, mr = lx + halfW;
    const float mt = ly - halfH, mb = ly + halfH;

    const DirectX::XMVECTOR black =
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, kDarkAlpha);

    if (mt > vt) renderer.DrawFilledRect({ vl, vt, vr, mt }, black);   // 위
    if (mb < vb) renderer.DrawFilledRect({ vl, mb, vr, vb }, black);   // 아래
    if (ml > vl) renderer.DrawFilledRect({ vl, mt, ml, mb }, black);   // 왼쪽
    if (mr < vr) renderer.DrawFilledRect({ mr, mt, vr, mb }, black);   // 오른쪽
}


// ----------------------------------------------------------------------------
//  DrawHandSlots — 손에 든 것
//
//    ★ 글자가 아니라 그림인 이유: 몸 상태를 이미 그림으로 보여 주고 있다(§3.2.3).
//      같은 층의 정보를 한쪽만 글자로 두면 눈이 두 번 읽어야 한다.
//      「오른손이 잘려 무기를 떨궜다」가 **한 칸이 X 로 바뀌는 것**으로 읽힌다.
// ----------------------------------------------------------------------------
void PlayScene::DrawHandSlots(Renderer& renderer)
{
    if (!m_icons)
        return;

    // ★★ 칸은 「무엇을 들었나」가 아니라 **「이 버튼이 무엇을 하나」**를 말한다.
    //   조작이 손 단위가 되면서 그것이 플레이어가 알아야 할 전부가 되었다.
    //
    //       단검    휘두른다
    //       이빨    문다 (빈손이든 잘렸든 — 무기가 없으면 남는 것은 이것뿐)
    //
    //   ★ 팔이 잘린 쪽은 **붉게** 칠한다. 「문다」는 같지만 몸 상태가 다르므로,
    //     그림을 하나 더 만드는 대신 **색으로** 한 겹을 얹는다.
    //     (X 그림은 시트에 남겨 둔다 — 장비 화면이 오면 그때 쓴다)
    struct Cell { const char* label; int arm; WeaponHand hand; };
    const Cell cells[2] = {
        { "L", Part_LeftArm,  WeaponHand::Left  },
        { "R", Part_RightArm, WeaponHand::Right },
    };

    const float y = Config::kCanvasHeight - 78.0f;

    for (int i = 0; i < 2; ++i)
    {
        const float x = kHandSlotX + static_cast<float>(i) * (kIconSize + 6.0f);

        const bool armed   = m_player->HandArmed(cells[i].hand);
        const bool severed = m_playerParts->IsBroken(cells[i].arm);
        const int  icon    = armed ? kIconDagger : kIconTeeth;

        const RECT src{ icon * kIconSize, 0, (icon + 1) * kIconSize, kIconSize };

        renderer.Sprites().Draw(
            m_icons.Get(), DirectX::XMFLOAT2(x, y), &src,
            severed ? DirectX::XMVectorSet(1.0f, 0.42f, 0.42f, 1.0f)
                    : DirectX::Colors::White);

        // 어느 손인지. 아이콘만으로는 좌우를 알 수 없다.
        renderer.DrawString(cells[i].label, x + 5.0f, y + kIconSize + 1.0f,
                            severed ? DirectX::Colors::Crimson
                                    : DirectX::Colors::SlateGray, 1);
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
    //   ★ **어느 손에 드는가를 컨트롤러가 들고 온다.** 전에는 여기서
    //     「남아 있는 팔」로 골랐는데, 버튼이 손을 가리키게 된 지금은
    //     **누른 쪽 손**이 답이다 — 고르는 주체가 Scene 에서 플레이어로 옮겨 갔다.
    //
    //   ※ 「팔이 없어 못 줍는다」는 경우가 여기서 사라졌다.
    //     그 판단은 컨트롤러의 CanHold 가 **요청을 내기 전에** 한다 —
    //     못 하는 일을 요청했다가 되돌리는 것보다 애초에 요청을 안 하는 편이 낫다.
    const WeaponHand hand = m_player->ConsumePickupRequest();
    if (hand == WeaponHand::None)
        return;

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

        // ★ 배경 지우기 색이 0.10 이다. 그보다 **양쪽으로** 벌려야 층이 읽힌다 —
        //   전에는 둘 다 0.10 보다 어두워서 서로 구분이 안 됐다.
        //     먼 층   0.135  옅다 (공기에 묻힌다)
        //     가까운 층 0.065 짙다 (실루엣)
        //   이것이 공기 원근이다. 색 하나로 거리가 읽힌다.
        const float tone = (p.depth < 0.4f) ? 0.135f : 0.065f;

        // 아래로는 **월드 밖까지** 뻗는다. 시차로 밀려 올라가도 바닥이 안 뚫린다.
        renderer.DrawFilledRect(
            { p.x + dx, p.top + dy,
              p.x + p.width + dx, Config::kWorldHeight + 500.0f + dy },
            DirectX::XMVectorSet(tone, tone, tone * 1.25f, 1.0f));
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
    for (Enemy& e : m_enemies)
        e.obj->Render(renderer);
    m_playerObj.Render(renderer);

    // ---- ★ 어둠은 그림 위, 디버그 **아래** ----
    //   순서가 규칙이다. 디버그보다 위에 덮으면 어두워서 판정 상자를 못 본다.
    //   「보여야 하는 것」과 「가려야 하는 것」이 층으로 갈린다.
    DrawDarkness(renderer);

    // ★ 디버그는 **모든 그림이 끝난 뒤에** 그린다.
    //   붙인 순서가 곧 실행 순서라서, Parts 를 먼저 붙이면 판정 상자를
    //   스프라이트가 덮어 「히트박스가 뒤에 있는」 상태가 된다.
    //   틱 순서와 그리기 순서의 요구가 다르므로 패스를 나눈다.
    m_weaponObj.RenderDebug(renderer);
    for (Enemy& e : m_enemies)
        e.obj->RenderDebug(renderer);
    m_playerObj.RenderDebug(renderer);
}


void PlayScene::RenderUI(Renderer& renderer)
{
    // 각 컴포넌트가 자기 표시를 그린다. Scene 은 순서만 정한다.
    m_playerObj.RenderUI(renderer);
    DrawHandSlots(renderer);
    // ★ 상태 줄은 **가장 가까운 적** 하나만 그린다.
    //   전부 그리면 화면이 덮이고, 안 그리면 확인할 수가 없다.
    //   ※ 부위 HP 목록도 그 안에서 같이 그려진다.
    //   ★ 변수 이름이 `near` 면 컴파일이 안 된다 — Windows 헤더에 남아 있는
    //     16비트 시절의 매크로(`#define near`)와 부딪힌다. 이름 하나로
    //     엉뚱한 구문 오류가 나는 자리라 적어 둔다.
    if (const Enemy* closest = NearestEnemy())
        closest->obj->RenderUI(renderer);

    if (renderer.DebugDraw())
    {
        renderer.DrawString("gold=torso/head  purple=arm  red=my hit  orange=enemy hit",
                            6.0f, Config::kCanvasHeight - 66.0f,
                            DirectX::Colors::Lime, 1);
    }

    // 살아 있는 적이 하나도 없으면 알린다.
    if (!NearestEnemy())
    {
        renderer.DrawStringCentered("ALL ENEMIES DOWN",
                                    Config::kCanvasWidth * 0.5f, 60.0f,
                                    DirectX::Colors::Gold, 2);
    }
}
