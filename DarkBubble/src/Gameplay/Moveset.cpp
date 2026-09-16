#include "Gameplay/Moveset.h"

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
    // ------------------------------------------------------------------------
    void Validate(const char* who, const AttackData& a)
    {
        const int total = a.TotalTicks();
        const int shown = a.clip.frameCount * a.clip.ticksPerFrame;

        if (!a.clip.loop && shown != total)
        {
            Log::Info("[moveset] ! {} : 그림 {}틱 != 프레임 데이터 {}틱 "
                      "({}프레임 x {}틱)",
                      who, shown, total, a.clip.frameCount, a.clip.ticksPerFrame);
        }

        if (a.clip.ticksPerFrame > 0 && (a.startup % a.clip.ticksPerFrame) != 0)
        {
            Log::Info("[moveset] ! {} : startup {} 이 틱/프레임 {} 로 안 나눠떨어진다 "
                      "— 타격 그림과 판정이 어긋난다",
                      who, a.startup, a.clip.ticksPerFrame);
        }
    }
}


bool MovesetIO::LoadInto(const wchar_t* path, Moveset& out, std::string* error)
{
    std::string err;
    const auto root = Json::ParseFile(path, &err);
    if (!root)
    {
        if (error) *error = err;
        return false;
    }

    // ★ **사본에 읽고 마지막에 옮긴다.** 도중에 실패해도 out 이 반쯤 바뀐
    //   상태로 남지 않는다. 절반만 적용된 밸런스는 틀린 밸런스보다 나쁘다 —
    //   무엇이 적용됐는지 알 수 없기 때문이다.
    Moveset temp = out;

    const JsonValue& dagger = (*root)["dagger"];
    if (dagger.IsNull())
    {
        if (error) *error = "dagger 항목이 없다";
        return false;
    }

    struct Entry { const char* key; AttackData* target; };
    const Entry entries[] = {
        { "light",      &temp.light      },
        { "crouch",     &temp.crouch     },
        { "dash",       &temp.dash       },
        { "thrust",     &temp.thrust     },
        { "jump",       &temp.jump       },
        { "prone",      &temp.prone      },
        { "bite",       &temp.bite       },
        { "biteCrouch", &temp.biteCrouch },
        { "biteProne",  &temp.biteProne  },
    };

    for (const Entry& e : entries)
        ReadAttack(dagger[e.key], *e.target);

    for (const Entry& e : entries)
        Validate(e.key, *e.target);

    out = temp;
    return true;
}
