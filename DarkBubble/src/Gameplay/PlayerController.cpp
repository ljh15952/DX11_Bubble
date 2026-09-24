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

#include <climits>
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

    // ---- ★ 죽은 자세 ----
    //   **프레임 하나 · 반복 없음** = 그 자리에서 멎는다.
    //
    //   ★★ 전에는 죽을 때 클립을 아예 안 걸고 `break` 만 두고서
    //     「마지막 프레임에서 멈춘다」고 적어 두었다. 그건 규칙이 아니라
    //     **직전 클립에 대한 가정**이었다 — 비반복 클립(공격) 중에 죽으면
    //     맞았지만, 추격·대기처럼 **반복 클립** 중에 죽으면 시체가 영원히
    //     걸어 다녔다. 그리고 죽는 순간의 상태는 대부분 그쪽이다.
    //
    //   ★★ **전용 「쓰러짐」 행을 쓴다.** 전에는 기어가기 행을 빌려 썼는데,
    //     기어가기는 **다리가 잘렸을 때의 자세**라 어떻게 죽든 다리가 잘린 채
    //     죽은 것처럼 보였다.
    //     빌려 쓴 그림은 **원래 뜻을 같이 가져온다** — 「형태가 비슷하다」가
    //     아니라 **「그 그림이 무엇을 뜻하는가」**로 골라야 한다.
    constexpr AnimationClip kDeadClip { /*row*/ 14, 1,  1, /*loop*/ false };

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
    //      light    26~36  몸통           기본. **이동 중에도 이것이 나온다**
    //      crouch    2~18  다리           Ctrl. 느리고 약한 대신 **조준할 수 있다**
    //      dash     26~36  몸통           ★ 구르기 직후에만. 길게 뻗지만 비싸다
    //      combo2   37~59  머리           1타를 맞춘 뒤에만. 머리는 즉사다
    //      jump     -9~21  머리(위에서)   공중에서만
    //
    //    ★★ 높이를 **범위**로 적는다. 전에는 중심값(34 등)만 적혀 있었는데,
    //      그러면 「몸통을 노린다」고 써 놓고 실제로는 머리까지 무는 것을
    //      못 알아챈다. 닿는 부위는 중심이 아니라 **띠 전체**가 정한다.
    //
    //    ★ 「언제 나오는가」가 전부 **직전에 무엇을 했는가**로 정해진다.
    //      키가 하나(좌클릭)인데 무브셋이 다섯인 이유다.
    //
    //    ★ 「머리를 노리려면 콤보를 성공시켜야 한다」가 데이터만으로 성립한다.
    //    ★ light(28) + combo2(34) = 62. 100 중 62를 쓰면 38 이 남고 구르기는 30 —
    //      콤보 뒤에는 한 번밖에 못 구른다. 기획서 §3.1 이 요구한 긴장이다.
    //    ★ clip 은 frames × ticks == TotalTicks 가 되도록 맞춰 두었다.
    //
    //    6-g 에서 이 네 덩어리가 그대로 items.json 이 된다.
    // ========================================================================
    // ---- ★★ 중단은 **띠**다. 두껍게 만들면 안 된다 ----
    //
    //   전에는 높이 24(발끝 22~46)였다. 그러면 선 적의 몸통(18~37)만이 아니라
    //   **머리(37~55)까지 물고**, 무엇보다 **몸을 낮춘 적의 머리(11~25)를
    //   3픽셀 스쳐서 즉사시켰다.** 평타로 목이 날아가는 셈이다.
    //
    //   적의 휘두르기를 26~36 의 띠로 좁혔던 것과 **같은 이유, 같은 숫자**다:
    //
    //       37 ~ 55   선 적의 머리      ← 닿으면 평타가 즉사기가 된다
    //     ★ 36
    //       중단 띠 (26~36)             몸통 18~37 에만 닿는다
    //     ★ 26
    //       ~ 25      몸을 낮춘 적      ← 닿으면 「낮춘다」가 의미를 잃는다
    //
    //   ★ 그래서 **덤벼들며 무는 적에게는 평타가 빗나간다.** 그때는 웅크려
    //     베거나(2~18) 뛰어올라 찍어야(-9~21) 한다 —
    //     적이 자세로 방어하듯 플레이어도 자세로 답한다.
    //
    //   ※ 숫자는 items.json 에도 있다. **F6 으로 바로 조정할 수 있는
    //     첫 밸런스 변경**이다 — 좁아서 답답하면 height 를 늘려 보면 된다.
    constexpr AttackData kDaggerLight{
        /*name*/     "LIGHT",
        /*startup*/  8,
        /*active*/   3,
        /*recovery*/ 13,            // 합계 24 = 6프레임 × 4틱
        /*reach*/    12.0f,
        /*width*/    28.0f,
        /*height*/   10.0f,         // ★ 24 -> 10. 띠로 좁혔다
        /*heightFromFoot*/ 31.0f,   // ★ 34 -> 31. 26~36 이 된다
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
        /*height*/   10.0f,         // ★ 평타와 **같은 띠**. 길이만 다르다
        /*heightFromFoot*/ 31.0f,
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
        a.heightFromFoot = 18.0f;   // 엎드린 머리(-25..-11)의 한가운데
        a.clip = { /*row*/ 13, 6, 4, false };   // 엎드려 물기 (이빨은 프레임 2·3)

        // ★★ **사거리는 몸 중심이 아니라 입에서 잰다.**
        //   엎드리면 머리가 **앞으로 나간다**(상자 x 4~20). 그래서 서 있을 때의
        //   reach 4 를 그대로 쓰면 판정이 **제 머리 위**에 얹혀 2픽셀밖에 안
        //   튀어나왔다 — 「머리만 있고 짧다」의 정체다.
        //   자세가 몸의 **모양**을 바꾸면 사거리도 같이 봐야 한다.
        a.reach = 18.0f;   // 엎드린 머리 앞끝(20)에서 시작한다
        a.width = 16.0f;
        return a;
    }();

    // 웅크려서 무는 것도 높이가 다르다.
    //   ★ **자기 머리 높이**로 문다. 웅크린 머리는 발끝 12~27 이므로 한가운데가 20.
    //     전에는 자세를 안 봐서 웅크린 채 물면 판정이 **제 머리 위(48)** 로 나갔다.
    //
    //   ★★ 그래서 웅크린 물기는 **즉사를 노릴 수 없다.** 선 적의 머리는 37~55 인데
    //     여기서는 11~29 를 훑으므로 몸통·다리에 닿는다.
    //     「목을 물려면 일어서야 한다」가 좌표만으로 성립한다 —
    //     웅크리기가 중단을 흘리는 대가를 여기서 치른다.
    constexpr AttackData kBiteCrouch = []{
        AttackData a = kBite;
        a.heightFromFoot = 20.0f;
        a.clip = { /*row*/ 12, 6, 4, false };   // 웅크려 물기 (이빨은 프레임 2·3)
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

    // ---- 엎드려 휘두르기 ----
    //   ★ 전에는 엎드린 공격이 kDaggerCrouch 를 빌려 쓰고, 그림은 「전용 행이
    //     없으니 기어가기 자세 유지」로 예외 처리했다. 그 행은 **반복**이라
    //     공격해도 아무 일도 안 일어나 보였다.
    //
    //   ★★ 전용 그림(11행)을 그리자 **예외가 사라졌다.**
    //     공격 데이터가 자기 그림을 들고 다니면 「이 자세일 때는 저 그림」이라는
    //     분기가 필요 없다. 웅크리기 때와 완전히 같은 결말이다.
    //
    //   높이 8 — 엎드린 몸이 훑는 높이. 선 적의 다리(0~18)에 닿는다.
    constexpr AttackData kDaggerProne = []{
        AttackData a = kDaggerCrouch;
        a.name           = "PRONE";
        a.heightFromFoot = 8.0f;

        // ★ 물기와 같은 이유로 사거리를 앞으로 민다. 11행의 칼이 실제로
        //   몸 앞끝(+18)에서 뻗어 나가므로 **그림과 판정이 같은 자리**가 된다.
        a.reach          = 18.0f;
        a.width          = 22.0f;
        a.clip           = { /*row*/ 11, 6, 5, false };   // 30틱 = 6 x 5
        return a;
    }();

    // ========================================================================
    //  DefaultDagger / DefaultUnarmed — **위의 상수들이 이제 「기본값」이다**
    //
    //    6-g 에서 숫자가 assets/data/items.json 으로 나갔다.
    //    그렇다고 여기 값들이 사라진 것은 아니다 — 파일이 없거나 깨졌을 때
    //    쓰는 **바닥값**이고, 무엇보다 **왜 그 숫자인지가 여기 적혀 있다.**
    //    JSON 에는 주석을 못 단다. 이유는 코드에 남고 값만 파일로 나간다.
    // ========================================================================
    //  ★ 8-a 에서 **둘로 갈라졌다.** 무기의 공격과 맨손의 공격은
    //    같은 곳에 있으면 안 된다 — 무기가 둘이 되면 이빨이 두 벌이 된다.
    ItemType DefaultDagger()
    {
        ItemType w;
        w.name      = "DAGGER";
        w.icon      = 1;          // icons.png 1칸 = 단검
        w.twoHanded = false;
        w.light     = kDaggerLight;
        w.crouch    = kDaggerCrouch;
        w.dash      = kDaggerDash;
        w.thrust    = kDaggerCombo2;
        w.jump      = kDaggerJump;
        w.prone     = kDaggerProne;
        return w;
    }

    UnarmedSet DefaultUnarmed()
    {
        UnarmedSet u;
        u.bite       = kBite;
        u.biteCrouch = kBiteCrouch;
        u.biteProne  = kBiteProne;
        return u;
    }

    constexpr RollData kRoll{ /*windup*/ 4, /*invincible*/ 12, /*recovery*/ 10 };
    constexpr HurtData kHurt{ /*ticks*/ 18, /*invuln*/ 24, /*knockback*/ 22.0f };

    // ★ 죽은 뒤 사망 화면이 뜨기까지의 한 박자.
    //   즉시 덮으면 무엇에 죽었는지 안 보여 플레이어가 배울 수 없다.
    constexpr int kDeathScreenDelay = 45;   // 0.75 초

    // ---- ★★ 무게 등급 표 (design.md §3.10.4) ----
    //   ※ 두 벌(CLOTH/PLATE)을 F2 로 오가던 표가 여기 있었다(8-f 에서 제거).
    //
    //   ★ 문턱이 **시작 장비에서 셋 다 닿도록** 정했다:
    //       천 한 벌 + 단검            =  9  → LIGHT
    //       천 한 벌 + 대검            = 19  → MEDIUM
    //       판금 몸통·투구 + 단검 + 연 방패 = 27  → HEAVY
    //   닿지 않는 등급은 **확인할 수 없는 등급**이다(8-a 에서 배운 것).
    //
    //   ★ 구르기 **전체 길이는 안 바뀐다**(26틱). 줄어드는 것은 무적뿐이고
    //     그만큼이 후딜이 된다 — 소울류의 「뚱뚱한 구르기」다. 그림 길이도
    //     안 바뀌므로 애니메이션을 따로 만들 필요가 없다.
    struct WeightTier { int below; float speed; int rollInvuln; };
    constexpr WeightTier kWeightTiers[] = {
        { 12,       1.00f, 12 },   // LIGHT  — 옛날 그대로
        { 24,       0.85f, 10 },   // MEDIUM
        { INT_MAX,  0.70f,  7 },   // HEAVY  — 적의 예고를 구르기로 흘리기 어렵다
    };

    constexpr float kSpeedPerTick     = 150.0f / 60.0f;   // 틱당 2.5 픽셀
    constexpr float kCrouchSpeedScale     = 0.45f;        // 조준의 대가
    constexpr float kProneSpeedScale      = 0.30f;        // 다리 파괴 = 기어간다

    // ★ 막으면서 걷는 속도. 웅크리기(0.45)보다는 빠르고 평소보다는 느리다 —
    //   「막으면서 붙는다」가 되긴 하되 공짜는 아니어야 한다.
    constexpr float kGuardSpeedScale      = 0.55f;

    // ★ 막는 동안의 스태미나 회복 배율. 소울류의 표준이다 —
    //   방패를 들고 있으면 **숨이 안 돌아온다.**
    //   1 이면 방패를 든 채 서 있는 것이 공짜 휴식이 된다(실제로 그랬다).
    constexpr float kGuardRegenScale      = 0.3f;

    // 튕겼을 때 금빛으로 번쩍이는 길이. 피격 번쩍임과 같은 길이로 둔다 —
    //   **색만 다르고 나머지는 같아야** 둘이 비교된다.
    constexpr int   kSparkTicks           = 9;

    constexpr float kOriginX = kCellW * 0.5f;
    constexpr float kOriginY = static_cast<float>(kCellH);

    constexpr float kMoveEpsilon      = 0.01f;
    constexpr int   kStepIntervalTicks = 15;
    constexpr int   kFlashTicks        = 9;

    constexpr float kShakeStrength = 2.0f;
    constexpr int   kShakeTicks    = 8;

    // ---- 점프 (design.md §3.8 의 「미결정」 세 칸이 여기서 숫자가 되었다) ----
    //
    //   ★★ 6.2 에서 **7.0** 으로 올렸다. 발판을 못 올라가서다 —
    //     원인은 점프가 아니라 **몸 높이와 지형의 관계**였다:
    //
    //       점프 높이 52  /  발판 세로 간격 40, 두께 8  ->  틈 32
    //       몸 높이   44  ->  32 < 44  =  그 사이에 **설 수 없다**
    //
    //     세로로 겹친 발판 사이가 몸보다 좁으면 그 자리는 강제 웅크리기가 되고,
    //     웅크리면 점프가 막히므로 **영영 못 올라간다.**
    //     그래서 간격을 56(틈 48 > 44)으로 넓혔고, 점프는 그 56 을 넘어야 한다.
    //
    //   실제 정점은 적분값이라 공식값보다 조금 낮다:
    //       Σ(7.0 - 0.35n), n=1..19  ≈  **66픽셀**  (공식 v0²/2g = 70)
    //     틱 단위로 더하는 이상 공식은 참고값일 뿐이다 — **세어 볼 것.**
    constexpr float kJumpSpeed  = 7.0f;
    //   ★ 공중 제어력. 0 이면 「뛰면 끝」이라 답답하고 1 이면 무게가 사라진다.
    constexpr float kAirControl = 0.6f;
    //   ★ 넉백에 섞는 상승. 착지까지 약 11틱이라 경직(18틱)이 끝나기 **전에**
    //     발이 땅에 닿는다 — 「경직은 풀렸는데 아직 공중」이 생기지 않는다.
    constexpr float kHurtLift   = 2.0f;

    // ※ 시작 좌표는 **맵이 정한다**(entries.start). 여기 있던 kStartX 는
    //   맵이 생기면서 사라졌다 — 좌표를 코드에 박아 두면 적이 셋이 되었을 때와
    //   같은 일이 벌어진다(전부 한 자리에 모인다).

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
    // ★★ 방어 자세가 **자세보다 먼저**다. 막고 있으면 걷든 서 있든
    //   방패를 들고 있는 그림이어야 한다 — 그림을 고르는 곳이 여기 하나뿐이라
    //   한 줄만 더하면 걷기·서기·앉기가 전부 따라온다.
    //   ★ 그림 행은 **방패가 들고 있다**(guardClip). 방패를 추가할 때
    //     여기를 고칠 일이 없다 — 무기가 자기 공격 그림을 드는 것과 같다.
    if (Guarding())
    {
        const ItemType& s = Weapon(GuardHand());
        return Crouched() ? s.guardCrouchClip : s.guardClip;
    }

    switch (m_parts->CurrentPosture())
    {
    case Posture::Prone:  return kCrawlClip;
    case Posture::Crouch: return kCrouchClip;
    case Posture::Stand:  break;
    }
    return moving ? kRunClip : kIdleClip;
}


void PlayerController::Start(SceneContext& ctx)
{
    // ★ 순서가 규칙이다: **기본값을 먼저 채우고 파일로 덮어쓴다.**
    //   반대로 하면 파일에 없는 항목이 비어 버린다.
    // ★ 기본값을 먼저 넣고 파일로 덮어쓴다. 적 카탈로그와 같은 순서다 —
    //   반대로 하면 파일에 없는 항목이 비어 버린다.
    m_items["dagger"] = DefaultDagger();
    m_unarmed           = DefaultUnarmed();
    ReloadItems();

    // ★ 손 슬롯은 **빈 채로 시작한다.** 전에는 기본값이 「오른손에 단검」
    //   이었는데, 슬롯이 이름 문자열이 되면서 그 기본값이 사라졌다 —
    //   여기서 명시적으로 들려 주지 않으면 빈손으로 시작한다.
    //   ★★ 기본값에 숨어 있던 것을 **보이는 한 줄로** 끌어낸 셈이다.
    EquipWeapon(WeaponHand::Right, "dagger");

    // ★ 천 한 벌을 입고 시작한다. 옛 CLOTH(poise 10)가 네 조각으로 갈라진 것이라
    //   합이 그대로 10 이다 — 8-f 전과 **같은 몸**으로 시작한다.
    //   ★ 카탈로그에 **있는 것만** 입는다. 파일에서 빠졌는데 이름만 넣으면
    //     「없는 물건」을 입게 된다.
    for (const char* id : { "cloth_hood", "cloth_coat", "cloth_pants", "cloth_shoes" })
    {
        auto it = m_items.find(id);
        if (it != m_items.end() && it->second.IsArmor())
            m_armor[static_cast<int>(it->second.armorSlot)] = id;
    }
    ApplyArmor();

    // ★ 시작 가방(6칸 — 가득 찬다). 무기를 얻는 길(상자·적 드롭)이 아직 없으므로
    //   장비 화면을 확인하려면 **바꿀 것이 있어야** 한다(8-a).
    //   ★ 고른 기준: 무게 등급 **셋이 다 닿는** 조합 + 특수 효과 하나.
    //     판금은 몸통·투구만 넣었다 — 다리·발까지 넣으면 가방이 넘친다.
    //     (items.json 에는 네 조각이 다 있다)
    for (const char* id : { "greatsword", "buckler", "kite",
                            "plate_mail", "plate_helm", "hunter_boots" })
        if (m_items.count(id))
            StoreInBag(id);

    m_body    = &Owner().Require<BodyComponent>();
    m_sprite  = &Owner().Require<SpriteComponent>();
    m_poise   = &Owner().Require<PoiseComponent>();
    m_parts   = &Owner().Require<PartsComponent>();
    m_stamina = &Owner().Require<StaminaComponent>();
    Respawn(ctx);
}


// ----------------------------------------------------------------------------
//  방어구 · 무게 (8-f, design.md §3.10.4)
// ----------------------------------------------------------------------------
namespace
{
    // 입은 것 + 든 것을 한 번씩 훑는다. ★ 양손 무기는 두 칸에 있어도 **한 번**.
    //   이 규칙을 합계 함수마다 따로 쓰면 한 곳에서 대검을 두 번 센다.
    template <class Fn>
    void ForEachEquipped(const ItemCatalog& items,
                         const std::array<std::string, kArmorSlots>& armor,
                         const std::string (&hands)[2], Fn&& fn)
    {
        auto visit = [&](const std::string& id)
        {
            auto it = items.find(id);
            if (it != items.end()) fn(it->second);
        };

        for (const std::string& id : armor)
            if (!id.empty()) visit(id);

        if (!hands[0].empty()) visit(hands[0]);
        if (!hands[1].empty() && hands[1] != hands[0]) visit(hands[1]);
        // ※ 「두 손이 같은 이름」은 양손 무기일 수도, 한손 무기 두 자루일 수도
        //   있다(ReleaseHand 주석). 무게에서는 두 자루면 **두 번** 세야 하므로
        //   아래에서 따로 바로잡는다.
    }
}


int PlayerController::TotalPoise() const
{
    // ★ 강인도는 **방어구만** 센다. 방패의 버티기는 막았을 때의 규칙이지
    //   맞았을 때의 규칙이 아니다(§3.11.1).
    int sum = 0;
    for (const std::string& id : m_armor)
    {
        auto it = m_items.find(id);
        if (it != m_items.end()) sum += it->second.poise;
    }
    return sum;
}


int PlayerController::EquipWeight() const
{
    int sum = 0;
    ForEachEquipped(m_items, m_armor, m_hand,
                    [&sum](const ItemType& t) { sum += t.weight; });

    // 한손 무기 두 자루(같은 종류)는 위에서 한 번만 셌다 — 한 번 더한다.
    const std::string& r = m_hand[HandSlot(WeaponHand::Right)];
    if (!r.empty() && r == m_hand[HandSlot(WeaponHand::Left)]
        && !Weapon(WeaponHand::Right).twoHanded)
    {
        sum += Weapon(WeaponHand::Right).weight;
    }
    return sum;
}


WeightClass PlayerController::Weight() const
{
    const int w = EquipWeight();
    for (int i = 0; i < static_cast<int>(std::size(kWeightTiers)); ++i)
        if (w < kWeightTiers[i].below)
            return static_cast<WeightClass>(i);
    return WeightClass::Heavy;
}


int PlayerController::RollInvulnTicks() const
{
    return kWeightTiers[static_cast<int>(Weight())].rollInvuln;
}


float PlayerController::WeightSpeedScale() const
{
    return kWeightTiers[static_cast<int>(Weight())].speed;
}


int PlayerController::BonusDamageVs(const std::string& enemyType) const
{
    int percent = 0;
    auto add = [&](const ItemType& t)
    {
        for (const ItemEffect& e : t.effects)
        {
            if (e.kind != EffectKind::BonusDamage) continue;
            if (!e.vs.empty() && e.vs != enemyType) continue;   // 빈 vs = 모두
            percent += e.value;
        }
    };

    // 방어구 — 모든 공격에
    for (const std::string& id : m_armor)
    {
        auto it = m_items.find(id);
        if (it != m_items.end()) add(it->second);
    }

    // 무기 — **그 무기로 칠 때만.** 맨손(물기)이면 무기 효과가 없다.
    if (HandArmed(m_pendingHand))
        add(Weapon(m_pendingHand));

    return percent;
}


void PlayerController::ApplyArmor()
{
    m_poise->SetValue(TotalPoise());
}


bool PlayerController::WearFromBag(int bagSlot)
{
    if (bagSlot < 0 || bagSlot >= kBagSize || m_bag[bagSlot].empty())
        return false;

    const std::string id = m_bag[bagSlot];
    auto it = m_items.find(id);
    if (it == m_items.end() || !it->second.IsArmor())
        return false;

    // ★ 맞바꾸기 — 꺼낸 칸에 입고 있던 것이 들어간다. 그래서 가방이 가득
    //   차 있어도 된다(EquipFromBag 과 같은 이유).
    std::string& worn = m_armor[static_cast<int>(it->second.armorSlot)];
    m_bag[bagSlot] = worn;   // 안 입고 있었으면 빈 칸이 된다
    worn = id;

    ApplyArmor();
    Log::Info("[play] {} 를 입었다 ({})  poise {}  무게 {} {}",
              it->second.name, ArmorSlotName(it->second.armorSlot),
              TotalPoise(), EquipWeight(), WeightClassName(Weight()));
    return true;
}


bool PlayerController::TakeOffArmor(ArmorSlot s)
{
    std::string& worn = m_armor[static_cast<int>(s)];
    if (worn.empty() || !StoreInBag(worn))
        return false;   // 안 입었거나 가방이 찼다 — 입은 채로 둔다

    worn.clear();
    ApplyArmor();
    return true;
}


void PlayerController::DiscardArmor(ArmorSlot s)
{
    std::string& worn = m_armor[static_cast<int>(s)];
    if (worn.empty())
        return;

    m_dropRequests.push_back(worn);   // 발밑에 — Scene 이 놓는다
    worn.clear();
    ApplyArmor();
}


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
namespace
{
    // 손 -> 그 팔의 부위 번호. 두 곳에서 같은 대응을 적지 않으려고 모아 둔다.
    int ArmOf(WeaponHand hand)
    {
        return (hand == WeaponHand::Right) ? Part_RightArm : Part_LeftArm;
    }
}


bool PlayerController::HandArmed(WeaponHand hand) const
{
    if (hand == WeaponHand::None)
        return false;

    // ★★ `twoHanded` 분기가 **사라졌다.** 양손 무기는 두 칸을 다 차지하므로
    //   「그 칸이 비었는가」만 물으면 된다 — 좌/우클릭이 둘 다 대검을
    //   휘두르는 것도, 왼손이 막히는 것도 **같은 한 줄**에서 나온다.
    //   ★ 규칙을 조건문으로 쓰는 대신 **자료 구조가 말하게** 만든 자리다.
    if (m_hand[HandSlot(hand)].empty())
        return false;

    // ※ 팔이 잘리면 TakeHit 이 이미 무기를 떨구므로 여기 걸릴 일은 없다.
    //   그래도 본다 — 「떨구는 쪽」과 「휘두르는 쪽」이 서로를 믿고 조건을
    //   생략하면, 한쪽이 바뀔 때 다른 쪽이 조용히 틀린다.
    return !m_parts->IsBroken(ArmOf(hand));
}


void PlayerController::OnPartBroken(int part)
{
    Log::Info("[play] ★ {} 절단!", m_parts->Name(part));

    if (part == Part_Legs)
    {
        Log::Info("[play]   -> 다리 상실 : 이동 대폭 감소 + 구르기 불가");
        return;
    }
    if (part != Part_RightArm && part != Part_LeftArm)
        return;

    // ★ 팔이 잘리면 **그 손에 있던 것**을 떨군다(design.md §3.2.2).
    //   다른 손이면 아무 일도 없다 — 어느 손인지가 결과를 바꾼다.
    const WeaponHand lost =
        (part == Part_RightArm) ? WeaponHand::Right : WeaponHand::Left;

    if (m_hand[HandSlot(lost)].empty())
        return;

    //   ★★ 양손 무기는 **어느 팔이 잘려도** 떨어진다. 두 칸을 차지하고 있으니
    //     한 칸이 무너지면 나머지 칸도 비어야 한다 — 새 규칙이 아니라
    //     「두 칸을 차지한다」의 결과다.
    //   ★ 「밀려난 것을 땅에 떨어뜨린다」와 **같은 일**이므로 같은 함수를 쓴다.
    const std::string dropped = m_hand[HandSlot(lost)];
    ReleaseHand(lost, Release::Ground);   // ★ 잘리면 **땅**이다 — 가방이 아니다
    Log::Info("[play]   -> {} 를 떨궜다! 주우러 가야 한다", dropped);
}


bool PlayerController::IsShieldHand(WeaponHand hand) const
{
    return HandArmed(hand) && Weapon(hand).IsShield();
}


WeaponHand PlayerController::GuardHand() const
{
    // ★ 왼손 우선 — 보조 슬롯이 방패 자리다(§3.10). 오른손에 방패를 들어도
    //   되지만, 그러면 휘두를 것이 없어지는 것이 대가다.
    for (WeaponHand h : { WeaponHand::Left, WeaponHand::Right })
        if (IsShieldHand(h))
            return h;

    return WeaponHand::None;
}


bool PlayerController::Guarding() const
{
    const WeaponHand h = GuardHand();
    if (h == WeaponHand::None)
        return false;

    // ★ 공중에서는 못 막는다. 구르기·상호작용과 **같은 조건**이다 —
    //   「공중은 무방비」가 §3.1 과 맞는다(design.md 의 조작 결정표).
    if (!m_body->Grounded())
        return false;

    // ★★ **행동 중에는 못 막는다.** 휘두르는 중에 방패가 서 있으면
    //   「공격하면서 막는」 무적이 된다. 구르기·피격도 마찬가지다.
    switch (m_state)
    {
    case PlayerState::Idle:
    case PlayerState::Run:
        break;
    default:
        return false;
    }

    return m_guardHeld;
}


AABB PlayerController::GuardBox() const
{
    const WeaponHand h = GuardHand();
    if (h == WeaponHand::None)
        return {};

    const ItemType& s  = Weapon(h);
    const Transform&  tr = Owner().transform;

    // ★★ **자세가 상자를 내린다.** 몸이 낮아진 비율만큼 띠도 내려간다 —
    //   그래서 「앉으면 하단을 막고 상단이 빈다」에 특수 규칙이 없다.
    //   §3.8 에서 판정 상자를 자세에 붙여 둔 것이 여기서 또 값을 한다.
    const float scale = m_body->Height() / m_body->StandHeight();

    const float top    = tr.y - s.guardTop    * scale;
    const float bottom = tr.y - s.guardBottom * scale;

    // 바라보는 쪽으로 내민다. 등 뒤는 못 막는다 — 상자가 거기 없으니까.
    const float front = tr.x + tr.facing * s.guardReach;
    const float back  = tr.x;

    return { std::min(front, back), top, std::max(front, back), bottom };
}


void PlayerController::PayGuard(SceneContext& ctx, const AttackData& atk, int damage)
{
    const ItemType& s    = Weapon(GuardHand());
    const int         cost = atk.impact * s.guardCost / 100;

    // ★ 스태미나가 모자라도 **막기는 한다.** 「부족하면 안 나감」이 아니라
    //   「나가고 대가를 치름」이 이 게임의 규칙이고(§3.1 방식 B),
    //   그 대가가 곧 **가드 브레이크**다 — Depleted -> Exhausted 로 경직한다.
    //   ★★ 따로 만든 규칙이 아니라 **이미 있던 것이 그렇게 읽히는** 것이다.
    m_stamina->Spend(cost);

    // 막는 소리는 맞는 소리와 **달라야 한다.** 같으면 막았는지를 귀로 모른다.
    ctx.camera.Shake(kShakeStrength * 0.6f, kShakeTicks / 2);
    ctx.audio.Play("hit", 0.55f, 0.45f,
                   PanFromWorldX(Owner().transform.x, ctx.camera.X()));

    // ★ `Depleted()` 를 보면 안 된다 — 고갈 래치는 **다음 틱의** Stamina::Tick 이
    //   켠다. 방금 음수가 된 순간에는 아직 꺼져 있어서, 이 로그가 가드
    //   브레이크를 **한 번 늦게** 알렸다. 지금 값을 직접 본다.
    Log::Info("[play] ★ 막았다 — {} (방어 {}%)  dmg {} -> {}  스태미나 -{}{}",
              s.name, s.defense, atk.damage, damage, cost,
              m_stamina->Current() < 0.0f ? "  ** 가드 브레이크 **" : "");
}


bool PlayerController::Grounded() const
{
    return m_body->Grounded();
}


bool PlayerController::CanHold(WeaponHand hand) const
{
    if (hand == WeaponHand::None)        return false;
    if (!m_hand[HandSlot(hand)].empty()) return false;   // 그 손이 차 있다
    return !m_parts->IsBroken(ArmOf(hand));
}


WeaponHand PlayerController::PickupHand(const std::string& weaponId) const
{
    // ★★ **양손 무기는 두 팔이 다 성해야 한다.** 한 팔을 잃으면 대검은
    //   못 든다 — 「팔을 잃으면 무기를 바꿔야 한다」가 규칙 없이 성립한다.
    auto it = m_items.find(weaponId);

    // ★ 방어구는 **손에 들지 않는다** — None 을 돌려주면 줍기가 가방으로 보낸다.
    //   빈 부위면 바로 입히는 방법도 있지만, 판금을 줍는 순간 몰래 HEAVY 가
    //   되면 「왜 갑자기 느리지?」가 된다. 입는 것은 **플레이어가 정한다.**
    if (it != m_items.end() && it->second.IsArmor())
        return WeaponHand::None;

    //   ★ 두 칸을 차지하므로 **두 칸이 다 비어 있어야** 한다 — 왼손에 단검을
    //     들고 있으면 대검을 못 줍는다. 「무엇을 내려놓을까」가 생긴다.
    if (it != m_items.end() && it->second.twoHanded)
    {
        return (CanHold(WeaponHand::Right) && CanHold(WeaponHand::Left))
             ? WeaponHand::Right : WeaponHand::None;
    }

    // ★ **주손부터** 묻는다. 손을 고르는 자유는 사라졌지만, 「팔이 잘려서
    //   못 줍는다」는 남아야 한다 — 이 게임에서 팔은 실제로 잘린다.
    if (CanHold(WeaponHand::Right)) return WeaponHand::Right;
    if (CanHold(WeaponHand::Left))  return WeaponHand::Left;
    return WeaponHand::None;
}


std::vector<std::string> PlayerController::ConsumeDrops()
{
    return std::exchange(m_dropRequests, {});
}


void PlayerController::ReleaseHand(WeaponHand hand, Release to)
{
    std::string& slot = m_hand[HandSlot(hand)];
    if (slot.empty())
        return;

    // ★ 양손 무기는 두 칸에 **같은 이름**이 적혀 있다. 한 칸만 비우면
    //   반대 칸에 유령이 남는다.
    //   ★★ 「두 칸이 같다」로 판정하면 **한손 무기 두 자루를 같은 종류로**
    //     들었을 때도 둘 다 사라진다. 판정은 `twoHanded` 로 한다 —
    //     **같아 보이는 것과 같은 것은 다르다.**
    const std::string item = slot;
    const bool        both = Weapon(hand).twoHanded;

    slot.clear();
    if (both)
        m_hand[HandSlot(OtherHand(hand))].clear();

    // ★ 가방으로 보내라 했는데 자리가 없으면 **땅으로.** 물건은 절대
    //   사라지지 않는다 — 부르는 쪽이 자리를 미리 확인했더라도 이 줄은 둔다.
    //   「확인했으니 괜찮다」로 생략하면 확인하는 쪽이 바뀔 때 물건이 증발한다.
    if (to == Release::Bag && StoreInBag(item))
        return;

    m_dropRequests.push_back(item);
}


bool PlayerController::BagHasRoom() const
{
    for (const std::string& s : m_bag)
        if (s.empty())
            return true;
    return false;
}


bool PlayerController::StoreInBag(const std::string& id)
{
    for (std::string& s : m_bag)
    {
        if (s.empty())
        {
            s = id;
            return true;
        }
    }
    return false;   // 가득 찼다 — 넣지 않는다
}


bool PlayerController::EquipFromBag(int slot, WeaponHand hand)
{
    if (hand == WeaponHand::None || slot < 0 || slot >= kBagSize)
        return false;

    const std::string id = m_bag[slot];
    if (id.empty())
        return false;

    auto it = m_items.find(id);
    const bool both = (it != m_items.end()) && it->second.twoHanded;

    // ★ 방어구는 **손에 못 든다.** 입는 것이다(WearFromBag).
    if (it != m_items.end() && it->second.IsArmor())
        return false;

    // ---- 팔이 있어야 든다 ----
    //   ★ 양손 무기는 **두 팔 다.** 줍기(PickupHand)와 같은 규칙이다.
    if (m_parts->IsBroken(ArmOf(hand)))
        return false;
    if (both && m_parts->IsBroken(ArmOf(OtherHand(hand))))
        return false;

    // ---- 밀려날 것이 가방에 들어가는가 ----
    //   ★ 꺼낸 칸이 먼저 비므로 **+1** 이다. 그래서 가득 찬 가방에서도
    //     「단검 ⇄ 대검」 맞바꾸기는 된다.
    //   ★ 양손 무기는 두 칸에 있어도 **한 개**로 센다.
    const std::string& a = m_hand[HandSlot(hand)];
    const std::string& b = m_hand[HandSlot(OtherHand(hand))];
    const bool aTwoHanded = !a.empty() && Weapon(hand).twoHanded;

    int out = a.empty() ? 0 : 1;
    if (both && !b.empty() && !aTwoHanded)
        ++out;

    int room = 1;
    for (const std::string& s : m_bag)
        if (s.empty()) ++room;

    // ★★ 자리가 모자라면 **아예 안 든다.** 넘치는 것을 땅에 떨구면
    //   「장비를 바꿨더니 방패가 발밑에 떨어져 있다」가 되어, 무엇이 어디로
    //   갔는지 모르게 된다. 거절하고 이유를 보여 주는 편이 낫다.
    if (out > room)
        return false;

    m_bag[slot].clear();
    EquipWeapon(hand, id);   // 밀려난 것은 가방으로 간다(방금 빈 칸 포함)
    return true;
}


bool PlayerController::StowHand(WeaponHand hand)
{
    if (hand == WeaponHand::None || m_hand[HandSlot(hand)].empty())
        return false;

    // ★ 가방이 차 있으면 **손에 그대로 둔다.** 땅에 떨구지 않는다 —
    //   「넣어라」를 눌렀는데 떨어지면 그건 넣은 것이 아니다.
    if (!BagHasRoom())
        return false;

    ReleaseHand(hand, Release::Bag);
    return true;
}


void PlayerController::DiscardBag(int slot)
{
    if (slot < 0 || slot >= kBagSize || m_bag[slot].empty())
        return;

    m_dropRequests.push_back(m_bag[slot]);   // 발밑에 떨군다 — Scene 이 놓는다
    m_bag[slot].clear();
}


void PlayerController::DiscardHand(WeaponHand hand)
{
    if (hand != WeaponHand::None)
        ReleaseHand(hand, Release::Ground);
}


void PlayerController::EquipWeapon(WeaponHand hand, const std::string& weaponId)
{
    if (hand == WeaponHand::None)
        return;

    auto it = m_items.find(weaponId);
    const bool both = (it != m_items.end()) && it->second.twoHanded;

    // ★★ 방어구는 손에 **절대** 안 든다. 부르는 쪽(줍기 · 장비 화면)도 막지만
    //   **가장 아래에서 한 번 더** 막는다 — 부르는 길이 하나 늘 때마다 그 길이
    //   검사를 빠뜨릴 수 있다. 실제로 줍기(PickupHand)가 처음엔 방어구를 몰랐다.
    if (it != m_items.end() && it->second.IsArmor())
        return;

    // ★ 자리를 비운다. 밀려난 것은 **가방으로**(8-e) — 8-b 에서는 땅이었다.
    //   가방이 차 있으면 그때 땅이다. 어느 쪽이든 **사라지지 않는다.**
    ReleaseHand(hand, Release::Bag);
    if (both)
        ReleaseHand(OtherHand(hand), Release::Bag);

    m_hand[HandSlot(hand)] = weaponId;

    // ★★ **양손 무기는 두 칸을 차지한다.** 이 한 줄이 「좌/우클릭이 둘 다
    //   휘두른다」와 「왼손이 막힌다」를 **둘 다** 만든다 — 규칙을 조건문으로
    //   쓰지 않고 자료 구조가 말하게 하는 자리다(§1.2 의 기회비용).
    if (both)
        m_hand[HandSlot(OtherHand(hand))] = weaponId;

    Log::Info("[play] {} 를 {} 손에 들었다{}", Weapon(hand).name,
              hand == WeaponHand::Right ? "오른" : "왼",
              both ? " (양손 — 반대 손이 막혔다)" : "");
}


const std::string& PlayerController::HandItem(WeaponHand hand) const
{
    static const std::string kNone;
    return (hand == WeaponHand::None) ? kNone : m_hand[HandSlot(hand)];
}


const ItemType& PlayerController::Weapon(WeaponHand hand) const
{
    auto it = m_items.find(HandItem(hand));
    if (it != m_items.end())
        return it->second;

    // ★ 없으면 죽지 않는다. 카탈로그는 비어 있을 수 없다 —
    //   Start 가 기본 단검을 먼저 넣고 파일로 덮어쓴다.
    //   ※ 빈손일 때도 여기로 온다. 「빈손인가」는 HandArmed 가 따로 답한다 —
    //     여기서 널을 돌려주면 부르는 쪽마다 널 검사가 생긴다.
    return m_items.begin()->second;
}


bool PlayerController::CanRoll() const
{
    // 다리가 부서지면 구를 수 없다. 회피 수단을 통째로 잃는다.
    //
    // ★ 공중에서도 못 구른다(design.md §3.8). 구르기는 땅을 박차는 동작이고,
    //   무엇보다 「점프 + 구르기」가 되면 **무적으로 날아다니게** 된다.
    //
    // ★★ 웅크린 채로도 못 구른다 — **CanJump 와 같은 조건**이다.
    //   구르기도 점프도 땅을 박차는 전신 동작이라 몸을 낮춘 자세에서는 안 나온다.
    //   그래서 웅크리기의 대가가 분명해진다:
    //     중단을 흘리는 대신 **점프도 구르기도 잃는다.**
    //   천장에 눌려 못 일어서는 자리에서는 회피 수단이 통째로 없다 —
    //   낮은 틈이 안전한 곳이 아니라 **선택지가 없는 곳**이 된다.
    return !m_parts->LegsBroken() && m_body->Grounded() && !Crouched();
}


// ----------------------------------------------------------------------------
//  ReloadItems — 파일에서 다시 읽는다 (F6)
//
//    ★ 실패하면 **아무것도 안 바뀐다.** 로그만 남는다.
//      JSON 을 고치다 오타가 나서 게임이 죽으면 아무도 핫 리로드를 안 쓴다 —
//      리로드의 값어치는 「틀려도 안전하다」는 데서 나온다.
//
//    ★ 실패해도 **true/false 를 보고 무언가 하지 않는다.** 부르는 쪽이
//      되돌릴 필요가 없기 때문이다(ItemIO::LoadInto 의 약속).
// ----------------------------------------------------------------------------
void PlayerController::ReloadItems()
{
    std::string err;
    // ★ 없던 무기는 **단검에서 출발한다.** 그래서 대검은 단검과 다른 것만
    //   적으면 된다 — 적 카탈로그가 DefaultGrunt 에서 출발하는 것과 같다.
    if (ItemIO::LoadInto(L"assets/data/items.json",
                           m_items, m_unarmed, DefaultDagger(), &err))
    {
        Log::Info("[weapon] items.json 적용 — 무기 {}종", m_items.size());
        return;
    }

    // ※ 파일이 아예 없는 것도 여기로 온다. 그게 정상 동작이다 —
    //   기본값으로 굴러가고, 나중에 파일을 두면 그때부터 읽힌다.
    Log::Info("[weapon] items.json 을 못 읽었다 ({}) — 이전 값 유지", err);
}


void PlayerController::Rest()
{
    // ★ 되돌리는 것은 **몸**뿐이다. 무기를 어디에 떨궜는지, 어느 방어구를
    //   입었는지는 그대로 간다 — §3.6.1 의 「되돌아간다 / 남는다」 표 그대로다.
    m_parts->Reset();
    m_stamina->Reset();
    m_poise->Reset();
    ApplyArmor();

    m_flash       = 0;
    m_sparkTicks  = 0;
    m_invulnTicks = 0;
}


void PlayerController::PlaceAt(float x)
{
    // ★ 위치만 옮긴다. 체력도 무기도 부위도 그대로 가져간다 —
    //   맵을 건너는 것은 **되돌아가는 것이 아니다**(design.md §3.6.1).
    Owner().transform.x = x;
    m_body->SnapToGround();
}


bool PlayerController::Crouched() const
{
    // ★ 공격 중에도 웅크린 채다. 전에는 공격을 제외했는데, 그건 「공격 행에
    //   눌린 자세가 구워져 있으니 또 누르면 두 번 눌린다」는 **스케일 시절의
    //   사정**이었다. 전용 그림을 그린 지금은 4행이 이미 웅크린 높이라
    //   예외가 필요 없다.
    //
    //   ★★ 그리고 **못 일어서는 자리**가 있다. 자세를 「키를 누르고 있는가」로만
    //     정하면 낮은 틈에서 Ctrl 을 떼는 순간 몸이 천장 속에 박힌다 —
    //     세로 충돌은 「움직이는 중」에만 해결되므로 가만히 커진 몸은
    //     아무도 밀어내지 않는다.
    //
    //   ★★★ 웅크리기는 **지상 전용**이다. 공중에서 Ctrl 을 누르면 그림은 점프
    //     자세인데 상자만 줄어들었다 — 점프 중에는 그림을 자세에 맞춰 다시
    //     걸지 않기 때문이다. 「땅을 딛고 몸을 낮추는」 동작이므로 원래 맞지 않다.
    //
    //   ※ **강제 웅크리기는 공중도 막지 않는다.** 천장에 눌린 채 발밑이 사라지면
    //     몸이 공중에서 커져 천장 속으로 들어간다. 「못 일어선다」는 어디서든
    //     못 일어서는 것이다.
    return m_crouchForced || (m_crouchHeld && m_body->Grounded());
}


bool PlayerController::CanJump() const
{
    // ★ 웅크린 채로는 못 뛴다. 웅크리기는 상태가 아니라 **수식자**라,
    //   조합을 늘리기 시작하면 (웅크린 점프 공격 같은) 경우의 수가 폭발한다.
    //
    //   ★★ Crouched() 를 보므로 **천장이 낮아도** 못 뛴다. 당연한 결과인데
    //     입력만 봤다면 「일어서지도 못하는데 점프는 되는」 자리가 생긴다.
    return !m_parts->LegsBroken() && m_body->Grounded() && !Crouched();
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
    tr.x = m_homeX;
    tr.facing = 1;

    // ★ 세로는 **부활 지점이 들고 온다.** 맵의 지면으로 정하면 발판 위
    //   화톳불에서 쉬고 죽었을 때 아래 지면에서 일어난다.
    m_body->PlaceOnFloor(m_homeY);

    m_parts->Reset();
    m_flash       = 0;
    m_sparkTicks  = 0;
    m_invulnTicks = 0;

    m_rollDirX  =  1.0f;
    m_knockDirX = -1.0f;

    m_currentAttack = nullptr;
    m_pendingHand   = WeaponHand::None;
    m_queuedHand    = WeaponHand::None;
    m_hitThisSwing  = false;
    m_crouchHeld    = false;
    m_crouchForced  = false;
    m_guardHeld       = false;
    m_restingClipLast = nullptr;
    m_stepCooldown  = 0;

    m_deathScreenRequested = false;

    m_stamina->Reset();
    m_poise->Reset();
    ApplyArmor();   // ★ 값의 출처는 입은 방어구다

    m_dropRequests.clear();   // 아직 Scene 이 안 가져간 요청은 버린다

    // ---- 남는다 ----
    //   ★ 손 슬롯(m_hand)도 되돌리지 않는다. 무기를 떨군 채 죽었다면
    //     **빈손으로 부활**하고, 무기는 떨어진 그 자리에 그대로 있다.
    //     되돌리면 「죽으면 무기가 손으로 돌아오는」 게임이 되어
    //     §3.6.1 의 「남는다」가 무의미해진다.
    //
    //   ★ m_armor 는 **일부러 되돌리지 않는다.** 장비는 죽어도 그대로다 —
    //     design.md §3.6.1 의 「남는다」 칸에 실제로 들어간 첫 항목이다.
    //     되돌리면 「죽을 때마다 장비가 벗겨지는」 게임이 된다.

    // ★ ChangeState 를 쓰지 않는다 — 「같은 상태로의 전이는 무시」에 걸린다.
    //   리셋은 상태를 직접 놓고 애니메이션을 forceRestart 로 다시 건다.
    m_state      = PlayerState::Idle;
    m_stateTicks = 0;

    // ★ 여기서도 자세를 묻는다. 「여기만 예외」를 남기지 않는다 —
    //   이유는 EnemyBrain::Reset 의 같은 줄 참조.
    m_sprite->Play(PostureClip(false), true);
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
const AttackData& PlayerController::SelectAttack(PlayerState prev) const
{
    // ⓪ ★★ **그 손에 무기가 없으면 문다.**
    //
    //   전에는 「우클릭 = 물기」라는 **전용 버튼**이었다. 이제 물기는 버튼이
    //   아니라 **상태**다 — 빈손이든 잘렸든, 휘두를 것이 없으면 남는 것은
    //   이빨뿐이다(§3.2.2.1). 규칙이 하나 줄고 의미는 늘었다.
    //
    //   ★ 자세만 반영한다 — 자세마다 **무는 높이**가 다르다.
    //     자세를 `Prone() ? … : …` 로 묻지 않는다. 그렇게 쓰면 자세가 셋이 된
    //     지금 「웅크리기」가 조용히 빠진다 — 실제로 그렇게 빠져 있었다.
    if (!HandArmed(m_pendingHand))
    {
        switch (m_parts->CurrentPosture())
        {
        case Posture::Prone:  return m_unarmed.biteProne;
        case Posture::Crouch: return m_unarmed.biteCrouch;
        case Posture::Stand:  break;
        }
        return m_unarmed.bite;
    }

    // ★ 공중이면 무조건 내려찍기다. 아래의 선택지(웅크리기·콤보·달리기)는
    //   전부 **발이 땅에 있다**는 전제 위에 있다.
    // ★★ **누른 손**의 무기다. 손 슬롯이 둘이 된 뒤로 「내 무기」라는 것이
    //   없다 — 왼손에 단검, 오른손에 대검이면 버튼마다 다른 것이 나간다.
    const ItemType& w = Weapon(m_pendingHand);

    if (!m_body->Grounded())             return w.jump;

    // ① ★ **서 있지 않으면** 낮게 휘두르는 것밖에 못 한다.
    //   ★★ **입력이 아니라 자세**를 본다. 전에는 Ctrl 을 눌렀는지를 물어서,
    //     천장이 낮아 못 일어선 채로 공격하면 서서 휘두르는 공격이 나갔다.
    //     「지금 웅크리고 있나」와 「웅크리기를 누르고 있나」는 다른 질문이다.
    //
    //   물기와 **같은 모양의 switch** 다. 자세가 늘면 컴파일러가 여기도 짚는다.
    switch (m_parts->CurrentPosture())
    {
    case Posture::Prone:  return w.prone;
    case Posture::Crouch: return w.crouch;
    case Posture::Stand:  break;
    }

    // ② 공격 중이었다 -> 2타
    //   ★ 무한 연타 방지는 **예약하는 쪽** 한 곳에만 있다.
    //     전에 여기에도 조건을 하나 더 걸었다가 콤보가 아예 안 나왔다 —
    //     막을 곳은 한 곳이면 충분하다.
    // ★ **기본 공격 뒤에만** 2타가 나온다. DASH·CROUCH·JUMP 뒤에는 안 나온다.
    //   ※ SelectAttack 은 m_currentAttack 이 갱신되기 **전에** 불리므로,
    //     여기서 보는 것은 아직 **직전 공격**이다.
    if (prev == PlayerState::Attack && m_currentAttack == &w.light)
        return w.thrust;

    // ③ ★ 구르기 직후 -> 대시. 「달리는 중」이 아니라 **구르기 뒤**다.
    //   달리는 중으로 두었더니 이동이 거의 항상이라 평타가 안 나왔다.
    if (prev == PlayerState::Roll)       return w.dash;

    return w.light;                                               // ④ 기본
}


const AttackData& PlayerController::CurrentAttack() const
{
    return m_currentAttack ? *m_currentAttack : Weapon(m_pendingHand).light;
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
    m_queuedHand = WeaponHand::None;

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
        ctx.audio.Play("step", 0.5f, 0.35f, PanFromWorldX(tr.x, ctx.camera.X()));
        break;

    case PlayerState::Attack:
    {

        // ★ 어느 공격인지 **여기서 고정한다.**
        m_currentAttack = &SelectAttack(prev);
        const AttackData& atk = *m_currentAttack;

        // ★★ 자세별 예외가 **없다.** 전에는 `Prone() ? 기어가기 : 공격그림` 이었고,
        //   그 탓에 엎드려 공격하면 반복 재생되는 기어가기 그림이 나와
        //   **아무 일도 안 일어나 보였다.**
        //
        //   자세마다 전용 행을 그리자 예외가 통째로 사라졌다 —
        //   **공격 데이터가 자기 그림을 들고 다니기 때문**이다.
        //     서기 2·5·6행 / 웅크리기 4·12행 / 엎드리기 11·13행 / 공중 9행
        m_sprite->Play(atk.clip, true);
        m_hitThisSwing = false;

        // ★ 부족해도 공격은 나간다. 0 미만이면 끝난 뒤 Exhausted 로 간다.
        //   「부족하면 안 나감」이 아니라 「나가고 대가를 치름」이 이 게임의 규칙이다.
        m_stamina->Spend(atk.staminaCost);

        // 공격마다 피치를 달리해 무엇이 나갔는지 소리로도 구분되게 한다.
        const float pitch = (atk.heightFromFoot > 40.0f) ?  0.22f    // 찌르기 = 높게
                          : (atk.heightFromFoot < 20.0f) ? -0.25f    // 웅크리기 = 낮게
                          :  0.0f;
        ctx.audio.Play("swing", 0.55f, pitch + RandomPitch(0.10f), PanFromWorldX(tr.x, ctx.camera.X()));

        Log::Info("[play] {} 발동  [{} {} {}]  높이 {:.0f}  stam -{}",
                  atk.name, atk.startup, atk.active, atk.recovery,
                  atk.heightFromFoot, atk.staminaCost);
        break;
    }

    case PlayerState::Roll:
    {
        m_sprite->Play(kRollClip, true);

        // ★ 방향을 여기서 고정한다. 입력이 없으면 바라보는 방향으로 굴러간다.
        const float mv = ctx.input.MoveX();
        m_rollDirX = (std::abs(mv) > kMoveEpsilon)
            ? mv
            : static_cast<float>(tr.facing);

        m_stamina->Spend(kRoll.staminaCost);
        ctx.audio.Play("swing", 0.4f, -0.35f, PanFromWorldX(tr.x, ctx.camera.X()));  // 낮은 피치
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
        // ★ 자세를 묻지 않는다. 죽으면 **어떤 자세였든 쓰러진다.**
        //   전에는 PostureClip 을 썼는데, 서 있었으면 반복하는 idle 이 걸려
        //   **시체가 계속 숨을 쉬었다.**
        m_sprite->Play(kDeadClip, true);
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
    //
    //   ★★ 무게가 **먼저** 곱해진다(8-f). 무거우면 걷기도, 공중 제어도,
    //     웅크려 걷기도 전부 느려진다 — 몸이 무거운 것이지 걷기만 무거운 게 아니다.
    float speed = kSpeedPerTick * WeightSpeedScale();
    if (!m_body->Grounded())
    {
        speed *= kAirControl;   // 공중에서는 약하게만 조종된다
    }
    else
    {
        // ★ `if` 두 줄로 쓰면 **둘 다 걸린다** — 엎드린 채 Ctrl 을 누르면
        //   0.45 x 0.30 으로 두 번 깎였다. 자세는 **하나**이므로 분기도 하나다.
        switch (m_parts->CurrentPosture())
        {
        case Posture::Crouch: speed *= kCrouchSpeedScale; break;
        case Posture::Prone:  speed *= kProneSpeedScale;  break;
        case Posture::Stand:  break;
        }

        // ★ 방어는 자세와 **곱해진다.** 위의 switch 와 달리 `if` 인 이유:
        //   방어는 자세가 아니라 **수식자**라서 앉은 채로도 막을 수 있다.
        //   (엎드려 Ctrl 로 두 번 깎이던 것과는 다른 경우다)
        if (Guarding())
            speed *= kGuardSpeedScale;
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
        // 느리게 걸으면 발소리도 그만큼 뜸해야 한다.
        m_stepCooldown = (m_parts->CurrentPosture() == Posture::Stand)
            ? kStepIntervalTicks
            : static_cast<int>(kStepIntervalTicks / kCrouchSpeedScale);
        ctx.audio.Play("step", 0.45f, RandomPitch(0.15f), PanFromWorldX(tr.x, ctx.camera.X()));
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

    // ★ 무적 길이는 **무게 등급**이 정한다(§3.10.4). 전체 길이(26틱)는 그대로라
    //   줄어든 무적만큼이 후딜이 된다.
    return m_stateTicks >= kRoll.windup
        && m_stateTicks <  kRoll.windup + RollInvulnTicks();
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
HitResult PlayerController::TakeHit(SceneContext& ctx, const AttackData& atk,
                                    const AABB& atkBox, int part,
                                    float fromX, float /*fromY*/)
{
    // ※ fromY 는 지금 쓰이지 않지만 인자에 남겨 둔다 —
    //   「위에서 맞으면 더 세게 눕는다」 같은 규칙이 오면 여기서 쓴다.
    const Transform& tr = Owner().transform;

    // ---- ★★ 막았는가 (§3.11) ----
    //   **상자끼리 겹치는지**로 정한다. 「상단/중단/하단」이라는 이름도,
    //   「앉으면 하단을 막는다」는 규칙도 코드에 없다 — 방패 상자가 자세를
    //   따라 내려가므로 **좌표만으로** 성립한다.
    //   ★ 등 뒤도 자동으로 못 막는다. 상자가 바라보는 쪽에만 있으니까.
    const bool blocked = Guarding() && Intersects(atkBox, GuardBox());

    // ★★ 막아 낸 비율이 **데미지와 impact 를 같이** 줄인다. 데미지만 줄이면
    //   막아도 매번 휘청여서 「막는 의미」가 없어진다 — 방패가 받아 내는 것은
    //   아픔이 아니라 **충격**이고, 데미지는 그 결과일 뿐이다.
    //   ★ 비율 하나가 두 값을 정하므로 **어긋날 수가 없다.**
    const int soak   = blocked ? Weapon(GuardHand()).defense : 0;
    const int damage = atk.damage * (100 - soak) / 100;
    const int impact = atk.impact * (100 - soak) / 100;

    if (blocked)
        PayGuard(ctx, atk, damage);

    // ★ 튕겨 냈는가 — 막았고, 방패가 그 충격보다 단단했다.
    //   **데미지 규칙은 그대로다**(비율 감소). 튕김이 더하는 것은 딱 하나,
    //   **친 쪽의 경직**뿐이다 — 규칙을 겹쳐 쌓지 않는다.
    const HitResult result =
          !blocked                                       ? HitResult::Hit
        : Weapon(GuardHand()).hardness >= atk.impact     ? HitResult::Deflected
        :                                                  HitResult::Blocked;

    m_parts->Damage(part, damage);
    m_flash = kFlashTicks;

    Log::Info("[play] {} 피격  dmg {}  남은 {}/{}",
              m_parts->Name(part), damage,
              std::max(0, m_parts->Hp(part)), m_parts->MaxHp(part));

    // ---- 부위가 부서졌다 ----
    if (m_parts->IsBroken(part))
        OnPartBroken(part);

    // ---- 사망이 가장 먼저다. 강인도로 버텨도 부위는 깎였다 ----
    //   머리 또는 몸통이 부서지면 즉사.
    if (m_parts->Fatal())
    {
        ctx.camera.Shake(kShakeStrength * 2.5f, kShakeTicks * 3);
        ctx.audio.Play("hit", 1.0f, -0.55f, PanFromWorldX(tr.x, ctx.camera.X()));
        ChangeState(ctx, PlayerState::Dead);
        return result;
    }

    // ---- ★ 경직 여부를 강인도가 정한다 ----
    //   판정 자체는 PoiseComponent 로 옮겼다 — 적도 같은 규칙을 쓰기 때문이다.
    //   나중에 게이지 방식으로 바꾸면 이 줄은 그대로 두고 그쪽만 고친다.
    if (!m_poise->WouldStagger(impact))
    {
        // 버텨냈다 — **상태를 바꾸지 않는다.** 공격 중이었다면 그대로 이어진다.
        // ★ 피격 무적을 주지 않는다. 못 움직이는 구간이 없으므로 스턴락 위험이
        //   없고, 무적은 그 위험을 막기 위한 장치이기 때문이다.
        //   대신 맞을 때마다 HP 가 확실히 깎인다 — 이것이 버티기의 비용이다.
        ctx.camera.Shake(kShakeStrength * 0.6f, kShakeTicks);
        ctx.audio.Play("hit", 0.5f, -0.75f, PanFromWorldX(tr.x, ctx.camera.X()));   // 둔탁하게
        Log::Info("[play] 버텨냄  poise {} >= impact {}", m_poise->Value(), impact);
        return result;
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
    ctx.audio.Play("hit", 0.95f, -0.25f, PanFromWorldX(tr.x, ctx.camera.X()));
    Log::Info("[play] 휘청  poise {} < impact {}   경직 {}틱 / 무적 {}틱",
              m_poise->Value(), impact, kHurt.ticks, kHurt.invuln);

    ChangeState(ctx, PlayerState::Hurt);
    return result;
}


// ----------------------------------------------------------------------------
//  Deflect — 휘두르던 것이 튕겼다 (§3.12)
//
//    ★★ Hurt 를 **빌린다.** 「휘두르기가 끊기고, 짧게 굳고, 뒤로 밀린다」가
//      Hurt 의 모양 그대로라 상태를 늘리지 않는다.
//
//    ★ 다만 **뜻이 다른 부분은 안 가져온다.** 죽는 그림에서 배운 것이다 —
//      「빌려 쓴 것은 원래 뜻을 같이 가져온다」. Hurt 에는 셋이 딸려 있다:
//
//        몸이 젖혀지고 밀린다   → **가져온다** (튕김도 그렇다)
//        붉게 번쩍인다          → 안 가져온다 (맞은 게 아니다) — 대신 금빛
//        피격 무적              → 안 가져온다 (벽을 쳤다고 무적이 되면 안 된다)
//
//      무적은 TakeHit 이 주는 것이라 Hurt 상태 자체에는 없다 — 그래서
//      여기서 ChangeState 만 하면 무적이 **저절로 안 따라온다.**
// ----------------------------------------------------------------------------
void PlayerController::Deflect(SceneContext& ctx)
{
    if (m_state != PlayerState::Attack)
        return;

    const Transform& tr = Owner().transform;

    m_hitThisSwing = true;                               // 이 휘두르기는 끝났다
    m_knockDirX    = -static_cast<float>(tr.facing);     // 친 방향의 반대로
    m_sparkTicks   = kSparkTicks;

    // 쇠끼리 부딪히는 소리. ★ 피격음과 **피치가 달라야** 한다 —
    //   귀만으로도 「맞았다」와 「튕겼다」가 갈려야 한다.
    ctx.camera.Shake(kShakeStrength * 1.2f, kShakeTicks);
    ctx.audio.Play("hit", 0.8f, 0.7f, PanFromWorldX(tr.x, ctx.camera.X()));
    Log::Info("[play] ★ 튕겼다 — {} 가 벽에 걸렸다", CurrentAttack().name);

    ChangeState(ctx, PlayerState::Hurt);
}


void PlayerController::Tick(SceneContext& ctx, bool consumeEdgeInput)
{
    // ---- 시간은 상태와 무관하게 매 틱 흐른다 ----
    ++m_stateTicks;

    if (m_flash > 0)        --m_flash;
    if (m_sparkTicks > 0)   --m_sparkTicks;
    if (m_invulnTicks > 0)  --m_invulnTicks;

    // ★ 입력이 가로 하나뿐이다. 전에는 (x, y) 벡터였고, 아무것도 안 하는 y 가
    //   정규화에 끼어들어 **↑를 같이 누르면 가로 속도가 0.707 배**가 되었다.
    const float moveX = ctx.input.MoveX();
    const bool  moving = (std::abs(moveX) > kMoveEpsilon);

    // 웅크리기는 **지속 입력**이라 엣지가 아니다. 매 틱 물어봐도 된다.
    m_crouchHeld = ctx.input.CrouchHeld();

    // ★ 방어도 지속 입력이다(§3.11 ③ 홀드). **방패를 든 손의 버튼**을
    //   보므로, 어느 손에 들었는지가 조작을 정한다 — 새 키가 없다.
    switch (GuardHand())
    {
    case WeaponHand::Left:  m_guardHeld = ctx.input.LeftHandHeld();  break;
    case WeaponHand::Right: m_guardHeld = ctx.input.RightHandHeld(); break;
    case WeaponHand::None:  m_guardHeld = false;                     break;
    }

    // ★ 막는 동안은 천천히 찬다. 스태미나는 **이 컴포넌트보다 먼저** 틱하므로
    //   다음 틱부터 적용된다 — 1틱 늦는 것은 눈에 안 보인다.
    m_stamina->SetRegenScale(Guarding() ? kGuardRegenScale : 1.0f);

    // ★ 천장이 낮으면 Ctrl 을 떼어도 못 일어선다.
    //   ★★ **묻는 순서가 중요하다.** 일어설 수 있는지를 먼저 묻고,
    //     그 답으로 자세를 정하고, 자세를 몸과 부위에 알려 준다.
    //     거꾸로 하면 「이미 커진 몸」으로 여유를 재게 되어 영영 못 일어선다.
    m_crouchForced = !m_crouchHeld && !m_body->CanStandUp();

    // ★ 자세를 부위에 먼저 알려 주고, **부위가 계산한 자세**를 몸에 넘긴다.
    //   몸  : 어디를 **지나갈 수 있는가** (높이 하나)
    //   부위: 어디를 **맞는가**          (상자 다섯)
    //
    //   ★★ 몸에 Crouched() 를 그대로 넘기면 안 된다. 그건 「웅크렸는가」이지
    //     자세가 아니다 — **엎드림을 말할 수 없다.** 실제로 그래서 엎드린 몸이
    //     선 크기(44)로 남아 있었다.
    //     엎드리기를 아는 것은 부위(다리가 부서졌다)이므로 거기서 받아 온다.
    m_parts->SetCrouching(Crouched());

    // ★ 공격 모션이 몸을 낮추는가. 플레이어의 공격은 전부 Stand 이지만
    //   **적과 같은 자리에 같은 줄**을 둔다 — 한쪽에만 있으면 나중에
    //   몸을 던지는 무기(도약 찌르기 같은 것)를 넣을 때 여기만 빠진다.
    m_parts->SetAttackPosture(m_state == PlayerState::Attack
                                  ? CurrentAttack().posture
                                  : Posture::Stand);

    //   ★ 지형 상자는 **StancePosture** — 공격 모션은 빼고.
    //     공격 중에 줄였다가 낮은 틈에서 끝나면 천장에 박힌다.
    m_body->SetPosture(m_parts->StancePosture());

    // ★ 쉬는 자세의 그림이 바뀌면 **다시 건다.** Ctrl 도 방어도 상태를
    //   바꾸지 않으므로 ChangeState 가 안 불린다 — 그냥 두면 「웅크렸는데
    //   서 있는 그림」이 된다.
    //
    //   ★★ 전에는 `Crouched() != m_crouchedLast` 였다. 방어가 붙으면서
    //     **자세는 그대로인데 그림만 바뀌는** 경우가 생겼고, 조건을 하나 더
    //     얹으면 다음에 또 얹어야 한다. 그래서 **답을 직접 비교**한다 —
    //     무엇이 그림을 바꾸든 여기는 안 고친다(handoff §9.1 의 응용).
    const AnimationClip* resting = &PostureClip(m_state == PlayerState::Run);
    if (resting != m_restingClipLast)
    {
        m_restingClipLast = resting;

        // 서 있거나 걷는 중에만. 공격·구르기 도중에 그림을 갈아치우면
        // 프레임 데이터와 그림이 어긋난다.
        if (m_state == PlayerState::Idle || m_state == PlayerState::Run)
            m_sprite->Play(*resting, true);
    }

    const bool jumpPressed = (consumeEdgeInput && ctx.input.JumpPressed());
    const bool rollPressed = (consumeEdgeInput && ctx.input.RollPressed());

    // ★ 두 버튼을 **하나의 값**으로 합친다. 아래에서 「어느 손이 눌렸나」만
    //   보면 되고, 「무엇이 나가는가」는 SelectAttack 한 곳이 정한다.
    //   둘이 같은 틱에 눌리면 왼손이 이긴다 — 순서를 정해 두지 않으면
    //   프레임마다 다른 쪽이 이기는 것처럼 보인다.
    const WeaponHand pressed =
          (consumeEdgeInput && ctx.input.LeftHandPressed())  ? WeaponHand::Left
        : (consumeEdgeInput && ctx.input.RightHandPressed()) ? WeaponHand::Right
        :                                                      WeaponHand::None;

    // ★★ **방패를 든 손은 공격 버튼이 아니다.** 그 손은 막는 손이다 —
    //   「버튼은 손이고, 그 손에 든 것이 무엇을 할지 정한다」(§3.2.1.1).
    //   방어를 위한 새 키도, 「방어 중이면 공격 금지」라는 규칙도 없다.
    const WeaponHand handPressed = IsShieldHand(pressed) ? WeaponHand::None : pressed;

    switch (m_state)
    {
    case PlayerState::Idle:
    case PlayerState::Run:
        // ★★ **고갈이 가장 먼저다.** 여기가 빠져 있었다 — 공격과 구르기는
        //   끝날 때 고갈을 보는데, **서서 막다가** 바닥나면 아무도 안 봤다.
        //   그래서 로그에는 「가드 브레이크」가 찍히는데 경직은 안 걸리고,
        //   음수 스태미나로 계속 막을 수 있었다.
        //   ★ 「스태미나를 쓰는 곳」마다 검사를 붙이는 대신 **서 있는 상태가
        //     본다** — 나중에 스태미나를 쓰는 것이 늘어도 여기는 안 고친다.
        if (m_stamina->Depleted())
        {
            ChangeState(ctx, PlayerState::Exhausted);
            break;
        }

        UpdateMovement(ctx, moveX);

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
        // ★ 여기에 **줍기 분기가 있었다.** 「발밑에 무기가 있으면 그 버튼은
        //   줍기」였는데, 그러면 떨군 무기를 밟고 선 동안 손이 통째로 막힌다.
        //   줍기는 E 로 옮겼고(Scene 이 받는다), 손 버튼은 **언제나 손**이다.
        else if (handPressed != WeaponHand::None)
        {
            m_pendingHand = handPressed;
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
        UpdateMovement(ctx, moveX);   // 공중 제어력은 UpdateMovement 가 안다

        // ★ 공중에서는 손 버튼이 **언제나 공격**이다. 줍기는 E 로 옮겼고,
        //   그 E 도 땅을 밟고 있을 때만 듣는다(PlayScene::UpdateFocus) —
        //   「뛰어넘으며 주워지지 않는다」는 규칙은 자리를 옮겨 살아 있다.
        if (handPressed != WeaponHand::None)
        {
            m_pendingHand = handPressed;
            ChangeState(ctx, PlayerState::Attack);
        }
        else if (m_body->Grounded())
        {
            // ★ 착지도 「상태가 끝났다」가 아니라 **「발이 닿았다」**로 판정한다.
            ctx.audio.Play("step", 0.6f, -0.35f,
                           PanFromWorldX(Owner().transform.x, ctx.camera.X()));
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
        //   ★ 예약에 **손까지** 담는다. 왼손으로 1타를 내고 오른손으로 이어치는
        //     것도 성립해야 하기 때문이다 — 「이어친다」는 무기의 성질이지
        //     손의 성질이 아니다.
        if (handPressed != WeaponHand::None && m_currentAttack == &Weapon(m_pendingHand).light
            && m_stateTicks >= atk.startup + atk.active)
        {
            m_queuedHand = handPressed;
        }

        // ※ 적을 실제로 때렸는지는 PlayScene 이 검사한다(헤더 주석 참조).

        // ★ 상태의 길이는 애니메이션이 아니라 프레임 데이터가 정한다.
        if (m_stateTicks >= atk.TotalTicks())
        {
            // ★ 고갈이 콤보보다 우선한다. 「2타를 예약해 두면 고갈을 피한다」가
            //   되면 스태미나 시스템에 구멍이 생긴다.
            if (m_stamina->Depleted())
                ChangeState(ctx, PlayerState::Exhausted);
            else if (m_queuedHand != WeaponHand::None)
            {
                // ★ ChangeState 가 m_queuedHand 를 지우므로 **먼저** 옮겨 담는다.
                m_pendingHand = m_queuedHand;
                // ★ force = true. Attack -> Attack 이라 「같은 상태면 무시」에 걸린다.
                ChangeState(ctx, PlayerState::Attack, true);
            }
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
        if (handPressed != WeaponHand::None
            && m_stateTicks >= kRoll.windup + RollInvulnTicks())
        {
            m_queuedHand = handPressed;
        }

        if (m_stateTicks >= kRoll.TotalTicks())
        {
            // 고갈이 콤보보다 우선한다 — 공격이 끝날 때와 같은 순서다.
            if (m_stamina->Depleted())
                ChangeState(ctx, PlayerState::Exhausted);
            else if (m_queuedHand != WeaponHand::None)
            {
                m_pendingHand = m_queuedHand;
                ChangeState(ctx, PlayerState::Attack);
            }
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

    // ※ F2(두 벌 갈아입기)가 여기 있었다. 장비 화면이 대신한다(8-f).

    // ★ F6 — 무브셋을 파일에서 다시 읽는다.
    //   숫자 하나 고칠 때마다 빌드하지 않아도 되는 것이 이 단계의 전부다.
    if (consumeEdgeInput && ctx.input.DataReloadPressed())
        ReloadItems();

    // ※ F8/F9(카탈로그에서 꺼내 들기)가 여기 있었다. 장비 화면이 대신한다.

    // ★ 임시 디버그 키 — 부위를 부러뜨렸다 되돌린다.
    //
    //     F4 다리    : 엎드린 자세의 판정 상자와 「중단을 흘린다」를 바로 본다.
    //     F7 오른팔  : **무기를 떨구게** 한다. 줍기(E)를 확인하려면 땅에 무기가
    //                 있어야 하는데, 무기가 떨어지는 경우는 **팔이 잘렸을 때뿐**
    //                 이다(일부러 버리는 키는 없다 — 그건 8단계 인벤토리의 몫).
    //
    //   실제로 부러뜨리려면 잡몹의 공격을 네 번 맞아야 해서 확인 한 번에
    //   십수 초가 걸린다. **만든 사람이 확인하기 어려운 기능은 있어도 없는 것과 같다.**
    //
    //   ※ 죽은 뒤에는 받지 않는다. RestingState 로 되돌리면 되살아나 버린다.
    const int breakPart =
          (consumeEdgeInput && ctx.input.LegBreakPressed()) ? Part_Legs
        : (consumeEdgeInput && ctx.input.ArmBreakPressed()) ? Part_RightArm
        : Part_Count;   // 아무 키도 안 눌렸다

    if (breakPart != Part_Count && !IsDead())
    {
        const bool wasBroken = m_parts->IsBroken(breakPart);
        if (wasBroken)
        {
            m_parts->Restore(breakPart);
        }
        else
        {
            m_parts->Damage(breakPart, m_parts->MaxHp(breakPart));

            // ★★ **피격과 같은 길을 지난다.** 전에는 F4 가 Damage 만 불렀는데,
            //   그러면 팔에 썼을 때 「잘렸는데 무기는 그대로 들려 있는」 몸이
            //   만들어진다 — 디버그 키로만 재현되는 버그는 찾기가 제일 어렵다.
            OnPartBroken(breakPart);
        }

        // ★ 자세가 바뀌었으니 그림을 다시 건다.
        //   상자는 Prone() 을 매번 보므로 즉시 바뀌지만, 애니메이션은
        //   **전이할 때만** 갈린다 — 그래서 force 로 한 번 흔들어 준다.
        ChangeState(ctx, RestingState(moving), true);

        Log::Info("[play] (디버그) {} {} — 엎드림 {}  손 L[{}] R[{}]",
                  m_parts->Name(breakPart),
                  wasBroken ? "복구" : "파괴",
                  m_parts->Prone() ? "ON" : "OFF",
                  HandItem(WeaponHand::Left), HandItem(WeaponHand::Right));
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
    else if (m_sparkTicks > 0)
        tint = DirectX::XMVectorSet(1.00f, 0.92f, 0.55f, 1.0f);   // 튕김 = 금빛 (맞은 게 아니다)
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
        // ★ 손 슬롯(48~86)과 **겹치지 않게** 오른쪽으로 민다.
        //   F1 을 켜면 숫자가 아이콘 위에 얹혀 둘 다 못 읽었다.
        m_parts->DrawHpList(renderer, 100.0f, Config::kCanvasHeight - 78.0f);

    // ★ 무기를 잃었다는 것은 **반드시 보여야 한다.**
    //   「왜 공격이 안 되지」를 플레이어가 추측하게 두면 안 된다.
    //   ★ 「주울 수 있다」는 이제 여기서 안 띄운다. 무기 **위에** `E : PICK UP`
    //     이 뜨므로(Scene), 같은 말을 두 곳에서 하면 한쪽이 낡는다.
    if (!HandArmed(WeaponHand::Right) && !HandArmed(WeaponHand::Left))
    {
        // ★ 「할 수 있는 것」을 같이 알려 준다. 못 하는 것만 말하면 막힌 느낌이 든다.
        renderer.DrawString("NO WEAPON - BOTH HANDS BITE",
                            12.0f, Config::kCanvasHeight - 92.0f,
                            DirectX::Colors::Crimson, 1);
    }

    // ---- 방어구 ----
    //   ★ 장비로 바뀌는 값이므로 **항상** 보여야 한다.
    //     안 보이면 「같은 공격에 왜 이번엔 안 밀렸지?」를 확인할 수 없다.
    //   ★★ **두 손을 다** 적는다. 한 줄로 「내 무기」를 적던 때는 손이
    //     하나뿐인 셈이라 맞았지만, 이제 어느 손에 무엇이 있는지가 전부다.
    //     양손 무기는 두 칸이 같은 것이므로 `L=R` 이 곧 「양손」이다.
    const bool twoH = HandArmed(WeaponHand::Right)
                   && m_hand[HandSlot(WeaponHand::Right)]
                      == m_hand[HandSlot(WeaponHand::Left)];

    auto label = [this](WeaponHand h) -> const char*
    {
        return HandArmed(h) ? Weapon(h).name.c_str() : "-";
    };

    //   ★ 방어구 이름 대신 **무게 등급과 강인도**를 적는다. 네 조각의 이름을
    //     늘어놓으면 아무것도 안 읽힌다 — 싸움 중에 알아야 하는 것은
    //     「지금 버티는가(poise)」와 「지금 잘 구르는가(무게)」다.
    renderer.DrawString(
        twoH ? std::format("{} {}  POISE {}  BOTH HANDS {}",
                           WeightClassName(Weight()), EquipWeight(), TotalPoise(),
                           label(WeaponHand::Right))
             : std::format("{} {}  POISE {}  L {}  R {}",
                           WeightClassName(Weight()), EquipWeight(), TotalPoise(),
                           label(WeaponHand::Left), label(WeaponHand::Right)),
        12.0f, Config::kCanvasHeight - 52.0f, DirectX::Colors::SlateGray, 1);

    // ---- 상태 ----
    //   ★ 상태 머신을 눈으로 보기 위한 표시.
    //     , 로 멈추고 . 로 밟으며 t 를 세면 프레임 데이터를 직접 확인할 수 있다.
    if (m_state == PlayerState::Attack)
    {
        const AttackData& a = CurrentAttack();
        renderer.DrawString(
            std::format("{}{}  t{:<3}{}   [{} {} {}]  h{:.0f}{}",
                        a.name, (m_currentAttack == &Weapon(m_pendingHand).thrust) ? " (2nd)" : "",
                        m_stateTicks, AttackPhase(m_stateTicks, a),
                        a.startup, a.active, a.recovery, a.heightFromFoot,
                        (m_queuedHand != WeaponHand::None) ? "  >> NEXT" : ""),
            6.0f, 6.0f,
            AttackActive() ? DirectX::Colors::Red : DirectX::Colors::Orange, 1);
    }
    else if (m_state == PlayerState::Roll)
    {
        // ★ **지금 무게로 실제로 적용되는** 구간을 보여 준다. 상수(kRoll)를
        //   그대로 적으면 HEAVY 인데도 「무적 12」가 떠서 화면이 거짓말을 한다.
        RollData r = kRoll;
        r.invincible = RollInvulnTicks();
        r.recovery   = kRoll.TotalTicks() - r.windup - r.invincible;

        renderer.DrawString(
            std::format("STATE ROLL  t{:<3}{}   [{} {} {}]  {}",
                        m_stateTicks, RollPhase(m_stateTicks, r),
                        r.windup, r.invincible, r.recovery,
                        WeightClassName(Weight())),
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
                        Crouched() ? " (CROUCH)" : "", m_stateTicks,
                        (m_invulnTicks > 0) ? std::format("   invuln {}", m_invulnTicks)
                                            : std::string{}),
            6.0f, 6.0f,
            (m_state == PlayerState::Exhausted || IsDead()) ? DirectX::Colors::Red
                                                            : DirectX::Colors::Orange, 1);
    }
}
