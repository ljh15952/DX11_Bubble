#include "Scenes/PlayScene.h"

#include "Scenes/DeathScene.h"
#include "Scenes/InventoryScene.h"
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
    // ※ 단검·대검의 칸 번호는 여기 없다 — **무기 데이터가 들고 있다**
    //   (items.json 의 `icon`). 여기 남는 것은 무기가 아닌 것들뿐이다.
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

    // 떨어진 무기의 몸. ★ **그림(16)보다 작다** — 물건은 발판 끝에
    //   아슬아슬하게 걸쳐야 「떨어질 뻔했다」가 보인다. 그림만큼 넓게 잡으면
    //   가장자리에서 공중에 뜬 것처럼 보인다.
    constexpr float kPickupHalfW  = 5.0f;
    constexpr float kPickupHeight = 6.0f;

    constexpr float kShakeStrength = 2.0f;
    constexpr int   kShakeTicks    = 8;
    constexpr int   kFlashTicks    = 9;

    // ※ **적의** 부위·강인도·공격 숫자는 EnemyType 으로 옮겨 갔다(7-b).
    //   왜 그 값인지도 같이 갔다 — Gameplay/EnemyType.cpp 참조.

    // ============================================================================
    //  ★ 플레이어의 부위 (design.md §3.2.2)
    //
    //    팔이 몸통의 **완충재**다. 적 swing(18) 기준으로
    //    팔 2대 + 몸통 4대 = 6대에 죽는다.
    //    성한 쪽 팔로 몸을 돌려 막으면 2대를 더 번다 —
    //    대신 등을 보이는 대가를 치른다.
    //
    //    ★ 다리가 부서지면 **쓰러져 기어간다.** 플레이어라고 예외가 아니다.
    //
    //    ※ 아직 파일로 안 나갔다. 적처럼 player.json 이 생기면 같이 나간다 —
    //      지금 빼면 쓰지도 않을 스키마를 하나 더 만드는 셈이다.
    // ============================================================================
    constexpr PartsProfile kPlayerParts{
        /*maxHp*/ { /*head*/ 20, /*L.arm*/ 24, /*R.arm*/ 24, /*torso*/ 60, /*legs*/ 40 },
        /*proneWhenLegsBroken*/ true,
    };

    //   플레이어의 초기 강인도. 방어구(F2)가 곧 덮어쓴다.
    constexpr int kClothPoise = 10;

    float RandomPitch(float spread)
    {
        static std::mt19937 rng{ 4321 };
        std::uniform_real_distribution<float> dist(-spread, spread);
        return dist(rng);
    }
}


// ============================================================================
//  ⓪ 맵 (7-c)
//
//    ★ 지형이 **파일에서 온다.** 여기 있던 배열은 assets/data/maps/*.json 으로
//      갔다 — 이미 「사각형 목록」이었기 때문에 형태가 하나도 안 바뀌었다.
//      6-d 에서 `Level` 의 **질의 모양만** 고정해 둔 것이 여기서 값을 한다.
//
//    ★★ 발판은 **통과할 수 없다**. 화면 좌우 끝도 지형이다 —
//      벽으로 두면 걷기·구르기·넉백이 전부 같은 규칙으로 막힌다.
//
//    ★ 배치의 세 조건(design.md §4.0.3)은 그대로다. 파일로 나갔다고
//      사라지는 규칙이 아니다:
//        ① 세로 간격 56 < 점프 정점 66
//        ② 세로 **틈**(간격-두께) 48 > 몸 높이 44   ← 놓치면 못 올라간다
//        ③ 가로 간격 32 < 공중 이동 거리
// ============================================================================
bool PlayScene::LoadMap(SceneContext& ctx, const std::string& name,
                        const std::string& entry)
{
    const std::wstring path =
        L"assets/data/maps/" + std::wstring(name.begin(), name.end()) + L".json";

    std::string err;
    MapData loaded;
    if (!MapIO::Load(path.c_str(), loaded, &err))
    {
        // ★ 읽기에 실패하면 **지금 맵을 그대로 둔다.** 다른 로더들과 같은 약속 —
        //   포탈 하나에 오타가 났다고 게임이 멈추면 맵 만들기가 무서워진다.
        Log::Info("[map] '{}' 를 못 읽었다 ({}) — 지금 맵 유지", name, err);
        return false;
    }

    m_map     = std::move(loaded);
    m_mapName = name;

    // 지형을 다시 채운다.
    m_level.Clear();
    m_level.SetGroundY(m_map.groundY);
    for (const AABB& s : m_map.solids)
        m_level.AddSolid(s);

    BuildBackdrop();
    SpawnEnemies(ctx);

    // ---- 플레이어를 입구에 놓는다 ----
    //   ★ **부활 지점은 여기서 안 정한다.** 부활은 맵의 시작이 아니라
    //     「마지막으로 쉰 자리」다(세이브 포인트). 맵을 지나가는 것만으로
    //     부활 지점이 바뀌면 되돌아갈 이유가 사라진다.
    m_focus = {};   // 맵이 바뀌었으니 들고 있던 포인터는 무효다
    // ※ m_drops 는 지우지 않는다. 각자 **어느 맵인지**를 들고 있어서
    //   그 맵으로 돌아오면 그 자리에 그대로 있다(§3.6.1 의 「남는다」).
    m_player->PlaceAt(m_map.EntryX(entry));

    UpdateCamera(ctx);

    Log::Info("[map] '{}' 진입 ({} 입구)  지형 {}  적 {}  상호작용 {}",
              name, entry, m_map.solids.size(), m_map.enemies.size(),
              m_map.interacts.size());
    return true;
}


// ----------------------------------------------------------------------------
//  UpdateFocus — 지금 발밑에 무엇이 있는가
//
//    ★ **누르기 전에** 찾아 둔다. 그래야 「E 를 누를 수 있다」를 화면에
//      보여 줄 수 있다 — 안 보여 주면 플레이어가 서 있어도 모른다.
// ----------------------------------------------------------------------------
void PlayScene::UpdateFocus()
{
    m_focus = {};

    // ★ 죽었거나 **공중이면** 아무것도 가리키지 않는다.
    //   공중을 막는 이유는 6-c-8 때와 같다 — 뛰어넘으며 주워지면 안 된다.
    //   무기·포탈·화톳불에 **같은 조건**을 건다: 규칙이 하나면 예외도 없다.
    if (m_player->IsDead() || !m_player->Grounded())
        return;

    // 몸통 상자로 본다 — 발끝 점으로 보면 뛰어넘을 때 그냥 지나친다.
    const AABB body = m_playerParts->Box(Part_Torso);

    // ---- ★ 우선순위가 셋이다 ----
    //     ① 주울 수 있는 물건
    //     ② 맵의 것 (포탈 · 화톳불)
    //     ③ **못 줍는** 물건 — 안내만
    //
    //   ★ ①이 ②보다 먼저인 이유: 화톳불은 도망 안 가지만 **쉬면 적이
    //     되살아난다.** 주우려다 쉬면 되돌릴 수가 없다 —
    //     **되돌릴 수 없는 쪽을 뒤로** 민다.
    //
    //   ★★ ③이 맨 뒤인 이유: 「손이 차서 못 줍는다」를 알려 주긴 해야 하지만,
    //     그것 때문에 **화톳불을 못 쓰게 되면** 안 된다. 안내가 기능을
    //     가로막으면 안내가 아니라 방해다.
    Drop* blocked = nullptr;

    for (Drop& d : m_drops)
    {
        if (d.map != m_mapName || !Intersects(body, d.pickup->PickupArea()))
            continue;

        // ★ 손에 못 들어도 **가방에 들어가면** 주울 수 있다(8-e).
        if (m_player->PickupHand(d.pickup->WeaponId()) == WeaponHand::None
            && !m_player->BagHasRoom())
        {
            if (!blocked) blocked = &d;   // 뒤로 미뤄 둔다
            continue;
        }

        m_focus.kind   = FocusKind::Weapon;
        m_focus.box    = d.pickup->PickupArea();
        m_focus.prompt = "E : PICK UP";
        m_focus.drop   = &d;
        return;
    }

    for (const MapInteract& it : m_map.interacts)
    {
        if (!Intersects(body, it.box))
            continue;

        m_focus.kind   = FocusKind::Map;
        m_focus.box    = it.box;
        m_focus.prompt = it.Prompt();
        m_focus.map    = &it;
        return;
    }

    if (blocked)
    {
        // ★ 초점은 잡되 **E 는 아무것도 안 한다**(Interact 가 다시 묻는다).
        //   말 없이 안 주워지면 「이건 못 줍는 물건인가?」로 읽힌다 —
        //   못 하는 이유를 안 알려 주면 플레이어는 규칙을 못 배운다.
        m_focus.kind   = FocusKind::Weapon;
        m_focus.box    = blocked->pickup->PickupArea();
        m_focus.prompt = "BAG FULL";   // ★ 손도 가방도 찼다 — 이제 무엇을 버릴지 정할 때다
        m_focus.drop   = blocked;
    }
}


// ----------------------------------------------------------------------------
//  Interact — E 를 눌렀다
//
//    ★ 여기가 **늘어나는 자리**다. 상자·사람·사다리가 오면 case 가 하나씩
//      붙고, 찾는 코드(UpdateFocus)도 버튼도 그대로다.
// ----------------------------------------------------------------------------
void PlayScene::Interact(SceneContext& ctx)
{
    // ---- 무기를 줍는다 ----
    if (m_focus.kind == FocusKind::Weapon)
    {
        Drop* d = m_focus.drop;
        const std::string& id = d->pickup->WeaponId();

        // ★★ **빈 손이 먼저, 그다음 가방**(8-e). 손에 들 수 있으면 바로 든다 —
        //   팔이 잘려 떨군 무기를 주웠는데 메뉴를 열어야 다시 들 수 있으면
        //   싸움 중엔 번거롭다. 손이 차 있으면 가방에 넣는다.
        const WeaponHand hand = m_player->PickupHand(id);
        if (hand != WeaponHand::None)
            m_player->EquipWeapon(hand, id);
        else if (m_player->StoreInBag(id))
            Log::Info("[play] {} 를 가방에 넣었다", id);
        else
            return;   // 손도 가방도 안 된다 — 초점이 이미 BAG FULL 을 말했다

        ctx.audio.Play("ui_confirm", 0.7f);

        // ★★ 「주웠다」 = **목록에서 사라진다.** 숨김 플래그가 필요 없다.
        //   ★ 지우면 m_focus.drop 이 끊기므로 **초점도 같이** 비운다 —
        //     다음 틱의 UpdateFocus 가 다시 채운다.
        m_drops.erase(m_drops.begin() + (d - m_drops.data()));
        m_focus = {};
        return;
    }

    if (m_focus.kind != FocusKind::Map)
        return;

    const MapInteract& it = *m_focus.map;
    switch (it.kind)
    {
    case InteractKind::Portal:
        // ★ 요청만 적어 둔다. 넘어가는 것은 틱의 맨 끝이다.
        m_portalPending = true;
        m_portalTo      = it.to;
        m_portalEntry   = it.entry;
        break;

    case InteractKind::SavePoint:
        // ★ 소울류의 화톳불이다 — **회복하고, 적이 되살아나고, 여기서 부활한다.**
        //   셋이 한 묶음이라 「쉬어 갈까」가 판단이 된다. 회복만 되면 공짜다.
        m_saveMap = m_mapName;
        m_saveX   = (it.box.left + it.box.right) * 0.5f;

        // ★★ 세로는 **상자의 아랫변**이다. 화톳불이 놓인 바닥이 곧 그 값이라
        //   따로 적을 것이 없다 — 발판 위에 두면 상자도 같이 올라가 있다.
        m_saveY   = it.box.bottom;

        m_player->Rest();
        SpawnEnemies(ctx);

        ctx.audio.Play("ui_confirm", 0.8f, 0.2f);
        Log::Info("[play] 쉬었다 — 부활 지점 '{}' ({} {:.0f},{:.0f}) · 회복 · 적 부활",
                  it.name, m_saveMap, m_saveX, m_saveY);
        break;
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
    for (float x = -300.0f; x < m_map.worldWidth + 400.0f; x += 214.0f, ++i)
        m_backdrop.push_back({ x, -80.0f + static_cast<float>((i * 5) % 4) * 74.0f,
                               54.0f, 0.22f });

    // 가까운 층 — 절반쯤 따라 움직인다. 가늘고 낮다.
    //   ★ 간격을 먼 층과 **서로소에 가깝게** 둔다(214 vs 151).
    //     배수로 두면 두 층이 주기적으로 겹쳐 한 덩어리로 보인다.
    i = 0;
    for (float x = -200.0f; x < m_map.worldWidth + 400.0f; x += 151.0f, ++i)
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
    m_enemies.clear();
    m_enemies.reserve(m_map.enemies.size());

    for (const MapEnemySpawn& s : m_map.enemies)
    {
        // ★ 모르는 종류면 잡몹으로 만든다. 맵 데이터에 오타가 났다고
        //   게임이 죽으면 안 된다 — JSON 로더와 같은 태도다.
        auto it = m_enemyTypes.find(s.type);
        if (it == m_enemyTypes.end())
        {
            Log::Info("[play] 모르는 적 종류 '{}' — grunt 로 대신한다", s.type);
            it = m_enemyTypes.find("grunt");
        }
        const EnemyType& type = it->second;

        Enemy e;
        e.type = it->first;   // ★ 대신 쓴 종류(grunt)면 그 이름이다 — 실제로 싸우는 것
        e.obj = std::make_unique<GameObject>("enemy");
        e.obj->transform.x      = s.x;
        e.obj->transform.facing = s.facing;

        // 붙인 순서 = 실행 순서. 플레이어와 **같은 구성**이다.
        e.obj->Add<BodyComponent>(m_level, kBodyHalfW, kBodyStandHeight,
                                  kBodyCrouchHeight, kBodyProneHeight);
        e.parts = &e.obj->Add<PartsComponent>(type.parts);
        e.poise = &e.obj->Add<PoiseComponent>(type.poise);

        // ★ 종류를 **참조로** 넘긴다. 카탈로그는 Scene 이 소유하고 리로드가
        //   값만 덮어쓰므로, 이 참조는 계속 유효하다.
        e.brain = &e.obj->Add<EnemyBrain>(m_playerObj.transform, type);

        e.obj->Add<SpriteComponent>(m_enemySheet, kCellW, kCellH);

        m_enemies.push_back(std::move(e));
    }

    // ★ 세우자마자 Start. 「만들고 나중에 Start」를 두 곳에 나눠 두면
    //   리로드 경로에서만 빠뜨린다.
    for (Enemy& e : m_enemies)
        e.obj->Start(ctx);
}


// ----------------------------------------------------------------------------
//  ReloadData — F6
//
//    ★ 적을 **다시 세운다.** 부위 HP 는 PartsComponent 를 만들 때 정해지므로,
//      카탈로그만 갱신하면 「HP 만 안 바뀌는」 반쪽 리로드가 된다.
//      밸런스를 만지는 중에는 리셋이 오히려 예측 가능하다.
// ----------------------------------------------------------------------------
void PlayScene::ReloadData(SceneContext& ctx)
{
    std::string err;
    if (EnemyTypeIO::LoadInto(L"assets/data/enemies.json", m_enemyTypes, &err))
        Log::Info("[enemy] enemies.json 적용");
    else
        Log::Info("[enemy] enemies.json 을 못 읽었다 ({}) — 이전 값 유지", err);

    SpawnEnemies(ctx);
    UpdateCamera(ctx);
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

    m_enemySheet = enemySheet;

    // ★ 기본값을 먼저 넣고 파일로 덮어쓴다. 무브셋과 같은 순서다 —
    //   반대로 하면 파일에 없는 항목이 비어 버린다.
    m_enemyTypes["grunt"] = DefaultGrunt();
    ReloadData(ctx);

    // ★ 떨어진 무기도 GameObject 다. 위치가 있고 그려지므로 Transform 이 필요하고,
    //   플레이어·적과 같은 그릇에 담기면 「월드에 있는 것」이 한 종류가 된다 —
    //   나중에 상자·함정·투사체가 생겨도 같은 방식으로 붙는다.

    // ★ 플레이어를 먼저 Start 한다 — LoadMap 이 SetHome/PlaceAt 을 부르는데
    //   그때 컨트롤러의 컴포넌트 참조가 이미 채워져 있어야 한다.
    m_playerObj.Start(ctx);

    if (!LoadMap(ctx, "field", "start"))
        return false;   // 첫 맵도 못 읽으면 진행할 수가 없다

    // ★ 처음 부활 지점은 **방금 읽은 맵의 start** 다. 아직 화톳불을 만나기
    //   전에 죽어도 갈 곳이 있어야 한다 — 값의 출처는 맵 파일 하나뿐이다.
    m_saveMap = m_mapName;
    m_saveX   = m_map.EntryX("start");
    m_saveY   = m_map.groundY;   // 입구는 언제나 지면 위에 있다

    // Start 는 **전부 붙은 뒤**에 부른다 — 컴포넌트들이 서로를 찾는 시점이다.

    Log::Info("[play] Arrows/WASD/Stick = move   Space = JUMP   Shift = roll");
    Log::Info("[play] LMB = 왼손   RMB = 오른손   — 무기가 있으면 휘두르고 없으면 문다");
    Log::Info("[play] E = 상호작용 — 발밑에 있는 것이 무엇인지가 하는 일을 정한다");
    Log::Info("[play]        무기 = 줍기(주손)   포탈 = 이동   화톳불 = 쉬기");
    Log::Info("[play] Ctrl = crouch (다리를 노린다)   Esc = pause");
    Log::Info("[play] F1 = hitbox   F2 = swap armor   F3 = stats");
    Log::Info("[play] F4 = 다리 파괴/복구   F7 = 오른팔 파괴/복구(= 무기를 떨군다)");
    Log::Info("[play] Tab = 장비 화면 (가방 6칸). ★ 게임은 **안 멈춘다** — 적이 온다");
    Log::Info("[play]      대검은 **양손**이라 두 칸을 차지한다 (좌/우클릭이 같은 것)");
    Log::Info("[play] 방패: 그 손의 버튼을 **누르고 있으면** 막는다. 앉으면 띠가 내려간다");
    Log::Info("[play] ,  = freeze    . = step 1 tick    / = slow motion (1/8)");
    Log::Info("[play] TIP: 공격 -> 후딜 중에 다시 공격 = 2타(THRUST). 머리 높이다");
    Log::Info("[play] TIP: 적 머리 위 `!` 가 예고다. 그동안 Shift 로 구르면 흘린다");
    return true;
}


void PlayScene::Respawn(SceneContext& ctx)
{
    // ★ 부활은 **마지막으로 쉰 자리**다. 맵이 다르면 그 맵을 다시 읽는다 —
    //   동굴에서 죽었어도 들판에서 쉬었으면 들판에서 일어난다.
    //   ★ 맵 읽기가 실패해도 부활은 해야 한다 — 그래서 **반환값을 본다.**
    //     실패하면 지금 맵이 그대로 남으므로 여기서 적을 되살린다.
    if (m_saveMap == m_mapName || !LoadMap(ctx, m_saveMap, "start"))
        SpawnEnemies(ctx);   // 같은 맵(또는 못 읽음) -> 적만 되살린다

    m_player->SetHome(m_saveX, m_saveY);

    // ★ 무엇을 되돌릴지는 **각자가 안다.** Scene 은 「되돌려라」만 말한다.
    //   design.md §3.6.1 의 「되돌아간다 / 남는다」 표가 각 컴포넌트 안에 있다.
    m_player->Respawn(ctx);
    for (Enemy& e : m_enemies)
        e.brain->Reset(ctx);

    // ★ m_drops 를 **건드리지 않는다.**
    //   떨어진 무기는 그 자리에 그대로 남고, 부활한 뒤 다시 주우러 간다.
    //   design.md §3.6.1 의 「남는다」 칸이 여기서 실체를 갖는다 —
    //   **아무것도 안 하는 것이 기능**인 드문 경우다.
}


void PlayScene::Resume(SceneContext& ctx)
{
    // ★ 위에 있던 화면 중 **사망 화면이 닫혔을 때만** 부활한다.
    //   DeathScene 은 PlayScene 을 알 필요가 없고 둘 사이에 포인터가 없다.
    //
    //   ★★ 전에는 `IsDead()` 로 구분했다 — 「위가 닫혔는데 죽어 있으면
    //     사망 화면이 닫힌 것」. 위에 올라가는 것이 Pause · Death 둘뿐일 때는
    //     맞았다. 장비 화면이 생기자 **틀렸다**: 게임이 안 멈추는 메뉴라
    //     연 채로 죽을 수 있고, 그때 메뉴가 스스로 닫히면 여기로 와서
    //     **사망 화면을 건너뛰고 즉시 부활했다.**
    //     「죽어 있다」는 「사망 화면을 띄웠다」의 **대리값**이었을 뿐이다 —
    //     셋째가 생기자 대리값이 무너졌다(handoff §9.1 의 사촌).
    //   그래서 **진짜로 묻고 싶은 것**을 직접 기억한다.
    if (!m_deathScreenShown)
        return;

    m_deathScreenShown = false;
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

    // ★ 특수 효과(§3.10.5). **곱한 결과만** 넘긴다 — 부위 HP 쪽은 효과를 모른다.
    //   효과가 늘어도 PartsComponent 는 「데미지 몇」만 받으면 된다.
    const int bonus  = m_player->BonusDamageVs(target->type);
    const int damage = atk.damage * (100 + bonus) / 100;

    m_player->MarkHitThisSwing();
    target->parts->Flash(kFlashTicks);
    target->parts->Damage(part, damage);

    // ★ 소리와 흔들림은 "맞는 순간" 에 낸다. 휘두르는 순간이 아니다.
    ctx.camera.Shake(kShakeStrength, kShakeTicks);
    ctx.audio.Play("hit", 0.85f, RandomPitch(0.12f),
                   PanFromWorldX(target->Tr().x, ctx.camera.X()));

    Log::Info("[play] {} 로 {} 명중  dmg {}{}  남은 HP {}",
              atk.name, target->parts->Name(part), damage,
              bonus ? std::format(" (+{}% vs {})", bonus, target->type) : std::string(),
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
// ----------------------------------------------------------------------------
//  HitsWall — 이 상자가 벽과 겹치는가
//
//    ★★ 「발밑보다 위로 솟은 것」만 벽이다.
//      내려찍기의 상자는 발끝 **아래로** 9픽셀 나간다(heightFromFoot 6,
//      height 30). 딛고 선 바닥까지 세면 착지할 때마다 튕긴다.
//      바닥은 윗면이 발끝과 **같고**, 벽은 윗면이 발끝보다 **높다** —
//      규칙이 아니라 **정의**다. 도랑 안에서는 도랑의 옆벽(윗면 600)이
//      발끝(640)보다 높으므로 벽이 된다.
// ----------------------------------------------------------------------------
bool PlayScene::HitsWall(const AABB& box, float feetY) const
{
    bool hit = false;
    m_level.ForEachOverlapping(box, [&](const AABB& s)
    {
        if (s.top < feetY)
            hit = true;
    });
    return hit;
}


// ----------------------------------------------------------------------------
//  TryDeflect — 벽에 걸린 휘두르기를 튕긴다 (§3.12)
//
//    ★ **active 동안 매 틱** 본다. 첫 틱에만 보면 달리며 치기(dash)처럼
//      휘두르는 도중에 앞으로 나가는 공격이 벽을 뚫는다.
//    ★ 플레이어와 적이 **같은 모양**이다. 공격 데이터를 같이 쓰니 튕기는
//      규칙도 같아야 한다 — 한쪽만 튕기면 「적은 벽 너머로 벤다」가 된다.
// ----------------------------------------------------------------------------
void PlayScene::TryDeflect(SceneContext& ctx)
{
    if (m_player->AttackActive() && !m_player->HitThisSwing()
        && HitsWall(m_player->AttackHitbox(), m_playerObj.transform.y))
    {
        m_player->Deflect(ctx);
    }

    for (Enemy& e : m_enemies)
    {
        if (!e.brain->AttackActive() || e.brain->HitThisSwing())
            continue;
        if (!HitsWall(e.brain->AttackHitbox(), e.Tr().y))
            continue;

        e.brain->Deflect(ctx);
        ctx.audio.Play("hit", 0.6f, 0.7f, PanFromWorldX(e.Tr().x, ctx.camera.X()));
        Log::Info("[play] ★ 적의 휘두르기가 벽에 튕겼다");
    }
}


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
        const AABB atkBox = e.brain->AttackHitbox();
        const int  part   = m_playerParts->PickHit(atkBox, e.Tr().x);
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
        // ★ 상자를 같이 넘긴다 — 「막았는가」는 **맞는 쪽**이 판단한다(§3.11).
        const HitResult r = m_player->TakeHit(ctx, e.brain->CurrentAttack(), atkBox,
                                              part, e.Tr().x, e.Tr().y);

        // ★★ 단단한 방패에 튕겼으면 **친 쪽이** 굳는다(§3.12).
        //   컨트롤러는 적을 모르므로 결과만 돌려주고, 적에게 전하는 것은
        //   둘을 다 아는 Scene 이다 — 벽에 튕긴 것과 **같은 함수**를 부른다.
        if (r == HitResult::Deflected)
        {
            e.brain->Deflect(ctx);
            Log::Info("[play] ★ 방패가 튕겨 냈다 — 적이 굳었다. 반격할 틈이다");
        }
    }
}


// ============================================================================
//  ③ 매 틱
// ============================================================================
void PlayScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    // ★ 「줍기가 가능한가」를 컨트롤러에 미리 알려 주던 줄이 여기 있었다.
    //   손 버튼이 줍기를 겸하던 동안에는 **틱 전에** 알려 줘야 했지만,
    //   줍기가 E 로 옮겨 가면서 그 왕복이 통째로 사라졌다.
    //   ★ 기능을 옮기면 그 기능을 **떠받치던 것도 같이** 사라지는지 볼 것.

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
    UpdateWeaponDrop(ctx);

    // ★ 떨어진 무기도 **굴린다**(중력·발판 착지).
    //   ★★ 떨구기 **뒤**다. 그래야 이번 틱에 떨어진 것이 같은 틱부터 낙하한다.
    //   ★ **이 맵의 것만** 굴린다. 다른 맵의 물건을 여기 지형으로 떨어뜨리면
    //     엉뚱한 높이에 착지한다 — 지형은 지금 맵의 것뿐이다.
    for (Drop& d : m_drops)
        if (d.map == m_mapName)
            d.obj->Tick(ctx, consumeEdgeInput);
    TryPlayerHit(ctx);

    for (Enemy& e : m_enemies)
        e.obj->Tick(ctx, consumeEdgeInput);
    TryEnemyHit(ctx);

    // ★ 튕김은 **적중 판정 뒤**다. 칼이 적에게 먼저 닿았으면 그 휘두르기는
    //   이미 끝났으므로(HitThisSwing) 벽을 안 본다 — **살에 박힌 칼은 벽까지
    //   안 간다.** 순서를 거꾸로 하면 적 바로 뒤에 벽이 있을 때 적을 못 친다.
    TryDeflect(ctx);

    // ★ **전부 움직인 뒤**에 따라간다. 먼저 움직이면 한 틱 뒤처진 곳을 비춘다.
    UpdateCamera(ctx);

    // ---- ★ 상호작용 ----
    //   판정이 다 끝난 뒤에 본다. 그리고 맵을 바꾸는 것은 **틱의 맨 끝**이다 —
    //   순회 중에 적 목록과 지형을 갈아 끼우면 안 된다.
    UpdateFocus();
    if (consumeEdgeInput && ctx.input.InteractPressed())
        Interact(ctx);

    if (m_portalPending)
    {
        m_portalPending = false;
        LoadMap(ctx, m_portalTo, m_portalEntry);
        return;   // 이번 틱은 여기서 끝. 새 맵은 다음 틱부터 돈다
    }

    // ---- Scene 전환 ----
    //   ★ 사망 화면을 PlayerController 가 직접 띄우지 않는 이유:
    //     Scene 전환은 Scene 의 일이고, Gameplay 가 Scenes 를 알기 시작하면
    //     층이 뒤엉킨다. 컨트롤러는 「띄울 때가 됐다」까지만 말한다.
    if (m_player->ConsumeDeathScreenRequest())
    {
        m_deathScreenShown = true;   // ★ Resume 이 이것을 본다
        ctx.scenes.Push(std::make_unique<DeathScene>());
    }

    // ★ F6 — 데이터를 다시 읽는다. 무기는 컨트롤러가 스스로 읽고(같은 키),
    //   적은 Scene 이 읽는다 — 카탈로그를 Scene 이 소유하기 때문이다.
    if (consumeEdgeInput && ctx.input.DataReloadPressed())
        ReloadData(ctx);

    // ★ 임시 키. 밝기는 **비교해 봐야** 정할 수 있다.
    if (consumeEdgeInput && ctx.input.DarkTogglePressed())
    {
        m_dark = !m_dark;
        Log::Info("[play] (F5) 어둠 {}", m_dark ? "ON" : "OFF");
    }

    // ★ 장비 화면. **게임을 멈추지 않는다**(InventoryScene.h 주석).
    //   죽은 뒤에는 안 연다 — 열자마자 스스로 닫힐 뿐이다.
    if (consumeEdgeInput && ctx.input.InventoryPressed() && !m_player->IsDead())
    {
        ctx.audio.Play("ui_confirm", 0.5f);
        ctx.scenes.Push(std::make_unique<InventoryScene>(*m_player));
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

        // ★ **무기가 자기 아이콘을 들고 있다.** 여기에 `if (단검) … else if
        //   (대검) …` 을 쓰면 무기를 추가할 때마다 이 줄을 찾아 고쳐야 한다.
        const int  icon    = armed ? m_player->Weapon(cells[i].hand).icon : kIconTeeth;

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

    ctx.camera.ClampTo({ 0.0f, 0.0f, m_map.worldWidth, m_map.worldHeight },
                       Config::kCanvasWidth, Config::kCanvasHeight);

    // Render 는 SceneContext 를 못 받으므로 여기에 적어 둔다.
    //   ※ 일시정지·사망 화면이 위에 떠 있는 동안에는 Update 가 안 돌므로
    //     이 값이 그대로 남는다 — 카메라가 멈춰 있는 것이 맞다.
    m_viewX = ctx.camera.X();
    m_viewY = ctx.camera.Y();
}


// ----------------------------------------------------------------------------
//  UpdateWeaponDrop — 떨구기
//
//    ★ 줍기는 여기 없다. E 가 받아 `Interact()` 로 간다 — 「발밑에 있는
//      것에 E」라는 규칙 하나에 무기도 들어갔기 때문이다.
// ----------------------------------------------------------------------------
void PlayScene::UpdateWeaponDrop(SceneContext& ctx)
{
    const Transform& tr = m_playerObj.transform;
    for (const std::string& id : m_player->ConsumeDrops())
        DropItem(ctx, id, tr.x, tr.y);
}


// ----------------------------------------------------------------------------
//  DropItem — 물건 하나를 월드에 놓는다
//
//    ★ 적 스폰(SpawnEnemies)과 **같은 모양**이다: GameObject 를 만들고,
//      필요한 능력을 붙이고, 조립할 때 포인터를 받아 둔다.
//      「월드에 있는 것」이 한 종류이므로 만드는 법도 한 종류다.
// ----------------------------------------------------------------------------
void PlayScene::DropItem(SceneContext& ctx, const std::string& weaponId, float x, float y)
{
    const ItemCatalog& types = m_player->Items();
    auto it = types.find(weaponId);
    const int icon = (it != types.end()) ? it->second.icon : 1;

    Drop d;
    d.obj = std::make_unique<GameObject>("drop");
    d.map = m_mapName;

    // ★ Body 를 **먼저** 붙인다 = 「물리 먼저, 판단 나중」. 플레이어와 같다.
    //   중력도 발판 착지도 여기 이미 있다 — 물건용 낙하 코드를 새로 쓰면
    //   플레이어와 두 벌이 된다.
    d.obj->Add<BodyComponent>(m_level, kPickupHalfW,
                              kPickupHeight, kPickupHeight, kPickupHeight);
    d.pickup = &d.obj->Add<WeaponPickup>();
    d.pickup->SetSheet(m_icons);   // 손 슬롯과 **같은 시트**를 쓴다

    d.obj->Start(ctx);
    d.pickup->DropAt(x, y, weaponId, icon);

    ctx.audio.Play("ui_cancel", 0.7f, -0.5f, PanFromWorldX(x, ctx.camera.X()));
    Log::Info("[play] {} 가 땅에 떨어졌다 ({:.0f}, {:.0f})  바닥의 물건 {}개",
              weaponId, x, y, m_drops.size() + 1);

    m_drops.push_back(std::move(d));
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
              p.x + p.width + dx, m_map.worldHeight + 500.0f + dy },
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
    for (Drop& d : m_drops)
        if (d.map == m_mapName)
            d.obj->Render(renderer);
    for (Enemy& e : m_enemies)
        e.obj->Render(renderer);
    m_playerObj.Render(renderer);

    // ---- ★ 어둠은 그림 위, 디버그 **아래** ----
    //   순서가 규칙이다. 디버그보다 위에 덮으면 어두워서 판정 상자를 못 본다.
    //   「보여야 하는 것」과 「가려야 하는 것」이 층으로 갈린다.
    DrawDarkness(renderer);

    // ★ 상호작용할 것은 **항상 보인다.** 안 보이면 「여기가 무엇인지」를
    //   알 수가 없다 — 나중에 문·화톳불 그림이 오면 이 자리를 대신한다.
    //   포탈은 푸르게, 세이브 포인트는 따뜻하게 — **색이 종류를 말한다.**
    for (const MapInteract& it : m_map.interacts)
    {
        const bool save = (it.kind == InteractKind::SavePoint);
        const DirectX::XMVECTOR fill = save
            ? DirectX::XMVectorSet(1.00f, 0.72f, 0.35f, 0.22f)
            : DirectX::XMVectorSet(0.55f, 0.75f, 1.00f, 0.22f);
        const DirectX::XMVECTOR edge = save
            ? DirectX::XMVectorSet(1.00f, 0.80f, 0.45f, 0.65f)
            : DirectX::XMVectorSet(0.65f, 0.85f, 1.00f, 0.55f);

        renderer.DrawFilledRect(it.box, fill);
        renderer.DrawRectOutline(it.box, edge, 1.0f);
    }

    // ★ 지금 누를 수 있는 것 위에만 안내를 띄운다. 항상 띄우면 아무도 안 읽는다 —
    //   「SPACE : PICK UP」과 같은 규칙이다.
    if (m_focus.kind != FocusKind::None)
    {
        renderer.DrawString(m_focus.prompt,
            std::round((m_focus.box.left + m_focus.box.right) * 0.5f - 32.0f),
            std::round(m_focus.box.top - 16.0f),
            DirectX::Colors::Gold, 1);
    }

    // ★ 디버그는 **모든 그림이 끝난 뒤에** 그린다.
    //   붙인 순서가 곧 실행 순서라서, Parts 를 먼저 붙이면 판정 상자를
    //   스프라이트가 덮어 「히트박스가 뒤에 있는」 상태가 된다.
    //   틱 순서와 그리기 순서의 요구가 다르므로 패스를 나눈다.
    for (Drop& d : m_drops)
        if (d.map == m_mapName)
            d.obj->RenderDebug(renderer);
    for (Enemy& e : m_enemies)
        e.obj->RenderDebug(renderer);
    m_playerObj.RenderDebug(renderer);

    // ★ 방패 상자. 부위(금색)·몸(청록)과 **또 다른 색**이다 —
    //   「막히는 높이」는 눈으로 봐야 이해된다(§3.11 의 띠 표).
    if (renderer.DebugDraw() && m_player->Guarding())
        renderer.DrawRectOutline(m_player->GuardBox(),
                                 DirectX::Colors::DeepSkyBlue, 1.0f);
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
