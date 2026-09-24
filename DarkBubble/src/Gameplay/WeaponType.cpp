#include "Gameplay/WeaponType.h"

#include "Core/Json.h"
#include "Core/Log.h"

namespace
{
    // ------------------------------------------------------------------------
    //  Validate — 문서에 적어 둔 함정을 **코드가 검사한다**
    //
    //    ★ handoff §8 에 두 번 적혀 있던 것이다:
    //        · frames × ticksPerFrame == TotalTicks   아니면 그림과 판정이 어긋난다
    //        · startup % ticksPerFrame == 0           아니면 타격 프레임이 안 맞는다
    //
    //      숫자를 파일로 뺀 순간 이 규칙을 **어기기가 훨씬 쉬워졌다** —
    //      빌드도 안 하고 고치니까. 그래서 읽을 때 봐 준다.
    //
    //    ★★ 경고만 하고 **막지는 않는다.** 밸런스를 만지는 중에는 잠깐 어긋난
    //      값으로도 굴려 보고 싶다. 막으면 도구가 방해가 된다.
    //
    //    ★ 무기 이름을 같이 찍는다. 카탈로그가 되면서 「어느 무기의 light 인가」
    //      를 알 수 없으면 경고가 쓸모없어진다 — 적 쪽에서 이미 배운 것이다.
    // ------------------------------------------------------------------------
    void Validate(const std::string& who, const char* what, const AttackData& a)
    {
        const int total = a.TotalTicks();
        const int shown = a.clip.frameCount * a.clip.ticksPerFrame;

        if (!a.clip.loop && shown != total)
        {
            Log::Info("[weapon] ! {}.{} : 그림 {}틱 != 프레임 데이터 {}틱 "
                      "({}프레임 x {}틱)",
                      who, what, shown, total, a.clip.frameCount, a.clip.ticksPerFrame);
        }

        if (a.clip.ticksPerFrame > 0 && (a.startup % a.clip.ticksPerFrame) != 0)
        {
            Log::Info("[weapon] ! {}.{} : startup {} 이 틱/프레임 {} 로 안 나눠떨어진다 "
                      "— 타격 그림과 판정이 어긋난다",
                      who, what, a.startup, a.clip.ticksPerFrame);
        }
    }

    // 무기 하나를 읽는다. ★ 적힌 항목만 덮어쓴다 — 나머지는 base 그대로다.
    void ReadWeapon(const JsonValue& v, const std::string& id, WeaponType& w)
    {
        w.name      = v["name"]     .Str(w.name);
        w.icon      = v["icon"]     .Int(w.icon);
        w.twoHanded = v["twoHanded"].Bool(w.twoHanded);

        // ---- 방패 (§3.11) ----
        w.guardTop    = v["guardTop"]   .Flt(w.guardTop);
        w.guardBottom = v["guardBottom"].Flt(w.guardBottom);
        w.guardReach  = v["guardReach"] .Flt(w.guardReach);
        w.defense     = v["defense"]    .Int(w.defense);
        w.guardCost   = v["guardCost"]  .Int(w.guardCost);
        w.hardness    = v["hardness"]   .Int(w.hardness);
        ReadClip(v["guardClip"],       w.guardClip);
        ReadClip(v["guardCrouchClip"], w.guardCrouchClip);

        struct Entry { const char* key; AttackData* target; };
        const Entry entries[] = {
            { "light",  &w.light  },
            { "crouch", &w.crouch },
            { "dash",   &w.dash   },
            { "thrust", &w.thrust },
            { "jump",   &w.jump   },
            { "prone",  &w.prone  },
        };

        for (const Entry& e : entries)
            ReadAttack(v[e.key], *e.target);

        for (const Entry& e : entries)
            Validate(id, e.key, *e.target);
    }
}


bool WeaponIO::LoadInto(const wchar_t* path, WeaponCatalog& out, UnarmedSet& unarmed,
                        const WeaponType& base, std::string* error)
{
    std::string err;
    const auto root = Json::ParseFile(path, &err);
    if (!root)
    {
        if (error) *error = err;
        return false;
    }

    const JsonValue& list = (*root)["weapons"];
    if (list.IsNull())
    {
        if (error) *error = "weapons 항목이 없다";
        return false;
    }

    // ★ **사본에 읽고 마지막에 옮긴다.** 도중에 실패해도 반쯤 적용되지 않는다.
    //   절반만 적용된 밸런스는 틀린 밸런스보다 나쁘다 — 무엇이 적용됐는지
    //   알 수 없기 때문이다.
    WeaponCatalog tempWeapons = out;
    UnarmedSet    tempUnarmed = unarmed;

    for (const auto& kv : list.Members())
    {
        // 있던 무기면 그 값에서, 없던 무기면 base(단검)에서 출발한다.
        WeaponType w = tempWeapons.count(kv.first) ? tempWeapons[kv.first] : base;
        ReadWeapon(kv.second, kv.first, w);
        tempWeapons[kv.first] = w;
    }

    // ---- 맨손 ----
    //   ★ 무기 목록 **바깥**에 있다. 무기가 아니기 때문이다(헤더 주석 참조).
    //     없어도 실패로 치지 않는다 — C++ 기본값이 이미 들어 있다.
    const JsonValue& hands = (*root)["unarmed"];
    if (!hands.IsNull())
    {
        struct Entry { const char* key; AttackData* target; };
        const Entry entries[] = {
            { "bite",       &tempUnarmed.bite       },
            { "biteCrouch", &tempUnarmed.biteCrouch },
            { "biteProne",  &tempUnarmed.biteProne  },
        };

        for (const Entry& e : entries)
            ReadAttack(hands[e.key], *e.target);

        for (const Entry& e : entries)
            Validate("unarmed", e.key, *e.target);
    }

    // ★★ **원소를 갈아 끼우지 않고 값만 옮긴다.** 컨트롤러가 들고 있는
    //   `m_currentAttack` 이 카탈로그 안의 AttackData 를 가리키므로,
    //   map 의 노드를 지웠다 만들면 그 포인터가 끊긴다.
    for (const auto& kv : tempWeapons)
        out[kv.first] = kv.second;

    unarmed = tempUnarmed;
    return true;
}
