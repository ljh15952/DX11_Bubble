// ============================================================================
//  weapons_test.cpp — weapons.json 이 C++ 파서로 읽히는지 + **규칙을 지키는지**.
//  돌리는 법은 tools/json_test.cpp 첫머리와 같다(파일 이름만 바꾼다).
//
//  ---- ★ 왜 이 검사가 필요해졌는가 ----
//    무기가 하나일 때는 숫자를 고칠 곳이 한 군데뿐이라 눈으로 됐다.
//    카탈로그가 되는 순간 **같은 규칙을 무기 수만큼** 지켜야 한다 —
//    그리고 어긴 결과는 「그림과 판정이 살짝 어긋난다」라서 눈에 잘 안 띈다.
//
//    로더도 같은 것을 보지만 **경고만** 한다(밸런스를 만지는 중에는 어긋난
//    값으로도 굴려 보고 싶다). 여기서는 **실패로 친다** — 커밋 전에 보는 눈이다.
// ============================================================================
#include "Core/Json.h"

#include <cstdio>
#include <string>

static int g_fail = 0;
static void Check(bool ok, const std::string& what)
{
    std::printf("%s  %s\n", ok ? "  ok" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

// 공격 하나의 프레임 데이터가 그림과 맞는지.
//   ★ 규칙 세 줄이 전부다. handoff §8 에 두 번 적혀 있던 것이고,
//     세 번째는 시트가 6칸이라는 사실에서 온다(없는 칸을 읽으면 빈 칸이 나온다).
static void One(const std::string& who, const JsonValue& a)
{
    if (a.IsNull()) { Check(false, who + " : 항목이 없다"); return; }

    const int su   = a["startup"] .Int(-1);
    const int ac   = a["active"]  .Int(-1);
    const int rec  = a["recovery"].Int(-1);
    const int fr   = a["clip"]["frames"].Int(-1);
    const int tk   = a["clip"]["ticks"] .Int(-1);
    const int row  = a["clip"]["row"]   .Int(-1);

    if (su < 0 || ac < 0 || rec < 0 || fr <= 0 || tk <= 0)
    { Check(false, who + " : 값이 빠졌다"); return; }

    Check(fr <= 6,                  who + " : clip.frames <= 6 (시트가 6칸)");
    Check(su % tk == 0,             who + " : startup 이 틱/프레임으로 나눠떨어진다");
    Check(fr * tk == su + ac + rec, who + " : 그림 길이 == 프레임 데이터 길이");
    Check(row >= 0,                 who + " : clip.row 가 있다");
}

int main()
{
    std::string err;
    auto root = Json::ParseFile(L"assets/data/weapons.json", &err);
    Check(root.has_value(), "weapons.json 을 읽었다");
    if (!root) { std::printf("      오류: %s\n", err.c_str()); return 1; }

    // ---- 맨손은 **무기 바깥**에 있다 ----
    const JsonValue& un = (*root)["unarmed"];
    Check(!un.IsNull(), "unarmed 가 weapons 바깥에 있다");
    for (const char* k : { "bite", "biteCrouch", "biteProne" })
        One(std::string("unarmed.") + k, un[k]);

    // ---- 무기 카탈로그 ----
    const JsonValue& list = (*root)["weapons"];
    Check(!list.IsNull(), "weapons 항목이 있다");
    Check(list.Members().size() >= 2, "무기가 둘 이상이다 (하나뿐이면 「바꾼다」를 확인할 수 없다)");

    const char* kMoves[] = { "light", "crouch", "dash", "thrust", "jump", "prone" };

    // 막는 띠가 있으면 방패다 — WeaponType::IsShield() 와 같은 판정.
    auto isShield = [](const JsonValue& w)
    {
        return w["guardTop"].Flt(0.0f) > w["guardBottom"].Flt(0.0f);
    };

    for (const auto& kv : list.Members())
    {
        const std::string& id = kv.first;
        const JsonValue&   w  = kv.second;

        Check(!w["name"].Str().empty(), id + " : name 이 있다");
        Check(w["icon"].Int(-1) >= 0,   id + " : icon 이 있다");

        // ---- 방패 ----
        if (isShield(w))
        {
            const int def  = w["defense"]  .Int(-1);
            const int cost = w["guardCost"].Int(-1);
            Check(def > 0 && def <= 100, id + " : defense 가 1~100%");
            Check(cost > 0,              id + " : guardCost 가 있다");

            // ★ 방패는 **휘두르지 않는다.** 공격 데이터는 base(단검)에서
            //   물려받은 채 남아 있고 아무도 안 읽는다 — 그래서 아래의
            //   「무기마다 다른 그림 행」 검사에서 빼야 한다. 안 그러면
            //   단검과 같은 행을 쓴다고 실패한다.
            Check(w["guardClip"]["row"].Int(-1) >= 0,       id + " : guardClip 이 있다");
            Check(w["guardCrouchClip"]["row"].Int(-1) >= 0, id + " : guardCrouchClip 이 있다");
            continue;
        }

        for (const char* k : kMoves)
            One(id + "." + k, w[k]);

        // ★★ **무기마다 다른 행을 봐야 한다.** 같은 행을 가리키면 판정만
        //   다르고 그림이 같아서, 무엇을 들었는지 화면에서 알 수가 없다 —
        //   6-a·b 에서 무브셋 4종이 전부 같은 행이던 것과 똑같은 실수다.
        for (const auto& other : list.Members())
        {
            if (other.first == id || isShield(other.second)) continue;
            bool same = true;
            for (const char* k : kMoves)
            {
                if (w[k]["clip"]["row"].Int(-1) != other.second[k]["clip"]["row"].Int(-2))
                { same = false; break; }
            }
            Check(!same, id + " 와 " + other.first + " 가 다른 그림 행을 쓴다");
        }
    }

    // ---- ★★ 방패끼리도 **다른 그림**이어야 한다 ----
    //   방패의 성격은 덮는 띠의 높이다. 그림이 같으면 「이게 어디까지 막는지」를
    //   화면에서 알 수 없고, 고르는 일이 숫자 읽기가 된다.
    for (const auto& a : list.Members())
    {
        if (!isShield(a.second)) continue;
        for (const auto& b : list.Members())
        {
            if (b.first == a.first || !isShield(b.second)) continue;
            Check(a.second["guardClip"]["row"].Int(-1)
                    != b.second["guardClip"]["row"].Int(-2),
                  a.first + " 와 " + b.first + " 가 다른 방어 그림을 쓴다");
        }
    }

    // ---- 맨손은 무기 안에 **없어야** 한다 ----
    //   ★ 옛 모양(무기 안의 bite)이 남아 있으면 조용히 무시되어,
    //     「고쳤는데 안 먹는다」가 된다. 남은 것을 찾아 준다.
    for (const auto& kv : list.Members())
    {
        const bool stray = !kv.second["bite"].IsNull()
                        || !kv.second["biteCrouch"].IsNull()
                        || !kv.second["biteProne"].IsNull();
        Check(!stray, kv.first + " 안에 물기가 남아 있지 않다 (맨손은 무기가 아니다)");
    }

    std::printf("\n%s (%d)\n", g_fail ? "== 실패 있음 ==" : "== 전부 통과 ==", g_fail);
    return g_fail;
}
