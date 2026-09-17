#include "Gameplay/EnemyType.h"

#include "Core/Json.h"
#include "Core/Log.h"

#include <cmath>

namespace
{
    constexpr AnimationClip kSwingClip { /*row*/ 2, 5, 12, /*loop*/ false };
    constexpr AnimationClip kBiteClip  { /*row*/ 3, 6,  9, /*loop*/ false };

    // ---- 공격 ----
    //   ★ startup 24틱은 일부러 길다. 예고를 보고 반응할 시간이다.
    //     플레이어 구르기의 무적은 [t+4, t+16) 이고 판정은 [24, 28) 이므로
    //     9틱(= invincible 12 − active 4 + 1)의 회피 창이 생긴다.
    //   ★ 24 / 12 = 2 — 팔이 뻗는 프레임이 판정과 **같은 틱**에 시작한다.
    //     어긋나면 플레이어가 아무리 연습해도 회피를 배울 수 없다.
    constexpr AttackData kSwing{
        /*name*/     "SWING",
        /*startup*/  24,
        /*active*/    4,
        /*recovery*/ 32,            // 합계 60 = 5프레임 × 12틱
        /*reach*/     8.0f,
        /*width*/    26.0f,
        // ★★ 발끝 26~36 의 **띠**다. 두껍게 만들면 안 된다 —
        //   위아래 양쪽에 「맞지 않아야 하는 것」이 붙어 있기 때문이다:
        //
        //       37 ~ 55   서 있는 머리   ← 닿으면 평타 두 대에 죽는다
        //     ★ 37
        //       중단 띠 (28~37)          팔 20~36 · 몸통 18~37 에 닿는다
        //     ★ 28
        //       ~ 27      웅크린 몸 전체 ← 닿으면 웅크리기가 의미를 잃는다
        //       ~ 25      엎드린 몸 전체
        //
        //   위쪽을 넘기면 잡몹의 평타가 머리에 닿아 두 대에 죽고,
        //   아래쪽을 넘기면 「웅크려 흘린다」가 사라진다. 9픽셀이 그 사이다.
        /*height*/    9.0f,
        /*heightFromFoot*/ 32.5f,
        /*damage*/   18,
        /*staminaCost*/ 0,          // 적은 스태미나를 쓰지 않는다
        /*impact*/   18,
        /*clip*/     kSwingClip,
    };

    // 물어뜯기 — 다리가 부서져 기어다닐 때.
    //   ★ 「다리 파괴 = 무해」로 만들지 않기 위한 데이터다.
    //     그렇게 하면 항상 다리부터 노리는 것이 정답이 되어
    //     기획서 §3.2 의 전술적 선택이 사라진다.
    constexpr AttackData kBite{
        /*name*/     "BITE",
        /*startup*/  18,            // 예고가 짧다 (18 / 9 = 2)
        /*active*/    3,
        /*recovery*/ 33,            // 합계 54 = 6프레임 × 9틱
        /*reach*/     4.0f,
        /*width*/    20.0f,
        /*height*/   16.0f,
        /*heightFromFoot*/ 12.0f,   // 낮게 — 발밑을 노린다
        /*damage*/   10,
        /*staminaCost*/ 0,
        /*impact*/   12,
        /*clip*/     kBiteClip,
    };

    // ★ 물기는 **몸을 던지는** 그림이다(3행 실루엣 = 발끝 1~25, 서면 0~53).
    //   그 동안은 낮은 표적이 되어야 그림과 판정이 같은 말을 한다.
    //   ※ 값을 초기화 목록 뒤에 따로 얹는다 — AttackData 는 집합 초기화라
    //     가운데 필드를 건너뛸 수 없고, 그렇다고 전부 적으면 기존 주석이 흩어진다.
    constexpr AttackData kBiteLow = []{
        AttackData a = kBite;
        a.posture = Posture::Prone;
        return a;
    }();

    // ---- 부위 (design.md §3.2.2) ----
    //   ★ 잡몹은 **팔이 없다**(maxHp 0). 스프라이트에 팔이 그려져 있지 않고,
    //     팔을 주면 플레이어 공격 11대가 필요해져 너무 질겨진다.
    //     보스에게는 숫자만 넣으면 팔이 생긴다.
    constexpr PartsProfile kGruntParts{
        /*maxHp*/ { /*head*/ 20, /*L.arm*/ 0, /*R.arm*/ 0, /*torso*/ 100, /*legs*/ 40 },
        /*proneWhenLegsBroken*/ true,
    };

    // ---- 강인도 ----
    //   ★ 플레이어 공격의 impact 는 light 14 / crouch 14 / dash 16 / thrust 18.
    //     15 로 두면 **끊을 수 있는 둘이 정확히 비싼 둘**이 된다 —
    //     「적을 끊으려면 스태미나를 더 낸다」가 데이터만으로 성립한다.
    //     13 이면 전부 끊겨 선택이 사라지고, 20 이면 아무것도 못 끊어 죽는다.
    constexpr int kGruntPoise = 15;

    void ReadParts(const JsonValue& v, PartsProfile& p)
    {
        if (v.IsNull())
            return;

        // ★ 이름으로 적는다. `[20, 0, 0, 100, 40]` 이라고 두면 순서를 외워야
        //   하고, 부위를 하나 추가하는 날 **전부 한 칸씩 밀린다** —
        //   AttackData 가운데에 필드를 넣었다가 겪은 것과 같은 일이다.
        const JsonValue& hp = v["maxHp"];
        p.maxHp[Part_Head]     = hp["head"] .Int(p.maxHp[Part_Head]);
        p.maxHp[Part_LeftArm]  = hp["lArm"] .Int(p.maxHp[Part_LeftArm]);
        p.maxHp[Part_RightArm] = hp["rArm"] .Int(p.maxHp[Part_RightArm]);
        p.maxHp[Part_Torso]    = hp["torso"].Int(p.maxHp[Part_Torso]);
        p.maxHp[Part_Legs]     = hp["legs"] .Int(p.maxHp[Part_Legs]);

        p.proneWhenLegsBroken =
            v["proneWhenLegsBroken"].Bool(p.proneWhenLegsBroken);
    }


    void ReadType(const JsonValue& v, EnemyType& t)
    {
        ReadParts(v["parts"], t.parts);
        t.poise = v["poise"].Int(t.poise);

        ReadAttack(v["swing"], t.swing);
        ReadAttack(v["bite"],  t.bite);

        t.attackCooldown = v["attackCooldown"].Int(t.attackCooldown);

        const JsonValue& mv = v["move"];
        t.walkPerTick  = mv["walk"] .Flt(t.walkPerTick);
        t.crawlPerTick = mv["crawl"].Flt(t.crawlPerTick);

        const JsonValue& rg = v["range"];
        t.swingRange    = rg["swing"]        .Flt(t.swingRange);
        t.biteRange     = rg["bite"]         .Flt(t.biteRange);
        t.yTolerance    = rg["yTolerance"]   .Flt(t.yTolerance);
        t.personalSpace = rg["personalSpace"].Flt(t.personalSpace);

        const JsonValue& si = v["sight"];
        t.sightHalfAngleDeg = si["halfAngleDeg"].Flt(t.sightHalfAngleDeg);
        t.sightRange        = si["range"]       .Flt(t.sightRange);
        t.hearRange         = si["hearRange"]   .Flt(t.hearRange);
        t.forgetTicks       = si["forgetTicks"] .Int(t.forgetTicks);

        // ★ 각도는 **도로 적고 여기서 cos 으로 바꾼다.**
        //   파일에 0.5736 이 적혀 있으면 아무도 그게 55도인 줄 모른다.
        t.sightCos = std::cos(t.sightHalfAngleDeg * 3.14159265f / 180.0f);

        const JsonValue& hu = v["hurt"];
        t.hurtTicks     = hu["ticks"]    .Int(t.hurtTicks);
        t.hurtKnockback = hu["knockback"].Flt(t.hurtKnockback);
        t.hurtLift      = hu["lift"]     .Flt(t.hurtLift);
    }


    // 문서에 적어 둔 함정을 코드가 검사한다(WeaponType.cpp 와 같은 규칙).
    void Validate(const char* who, const char* what, const AttackData& a)
    {
        const int total = a.TotalTicks();
        const int shown = a.clip.frameCount * a.clip.ticksPerFrame;

        if (!a.clip.loop && shown != total)
            Log::Info("[enemy] ! {}.{} : 그림 {}틱 != 프레임 데이터 {}틱",
                      who, what, shown, total);

        if (a.clip.ticksPerFrame > 0 && (a.startup % a.clip.ticksPerFrame) != 0)
            Log::Info("[enemy] ! {}.{} : startup {} 이 틱/프레임 {} 로 안 나눠떨어진다",
                      who, what, a.startup, a.clip.ticksPerFrame);
    }
}


bool EnemyTypeIO::LoadInto(const wchar_t* path, EnemyCatalog& out, std::string* error)
{
    std::string err;
    const auto root = Json::ParseFile(path, &err);
    if (!root)
    {
        if (error) *error = err;
        return false;
    }

    const JsonValue& types = (*root)["enemies"];
    if (types.IsNull())
    {
        if (error) *error = "enemies 항목이 없다";
        return false;
    }

    // ★ **사본에 읽고 마지막에 옮긴다.** 도중에 실패해도 반쯤 적용되지 않는다.
    EnemyCatalog temp = out;

    for (const auto& kv : types.Members())
    {
        // 없던 종류면 기본값에서 시작한다 — 파일에 적힌 것만 덮어쓰면 된다.
        // ★ 없던 종류는 **잡몹에서 출발한다.** 빈 EnemyType 에서 시작하면
        //   공격이 AttackData 의 일반 기본값(플레이어의 평타 모양)이 되어
        //   「적을 하나 더 적었더니 이상한 것이 나왔다」가 된다.
        EnemyType t = temp.count(kv.first) ? temp[kv.first] : DefaultGrunt();
        ReadType(kv.second, t);
        Validate(kv.first.c_str(), "swing", t.swing);
        Validate(kv.first.c_str(), "bite",  t.bite);
        temp[kv.first] = t;
    }

    // ★★ **원소를 갈아 끼우지 않고 값만 옮긴다.** EnemyBrain 이 종류를
    //   참조로 들고 있으므로, map 의 노드를 지웠다 만들면 그 참조가 끊긴다.
    for (const auto& kv : temp)
        out[kv.first] = kv.second;

    return true;
}


EnemyType DefaultGrunt()
{
    EnemyType e;
    e.parts = kGruntParts;
    e.poise = kGruntPoise;
    e.swing = kSwing;
    e.bite  = kBiteLow;   // ★ 물기는 몸을 낮춘다(posture = Prone)
    return e;
}
