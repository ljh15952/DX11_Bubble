// ============================================================================
//  save_test.cpp — 저장(SaveData)의 확인. 게임과 따로 돈다 (9).
//
//  돌리는 법은 tools/json_test.cpp 첫머리와 같다. 컴파일 줄만 다르다:
//    cl /nologo /std:c++20 /EHsc /W4 /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I DarkBubble\src tools\save_test.cpp DarkBubble\src\Gameplay\SaveData.cpp DarkBubble\src\Core\Json.cpp /Fe:save_test.exe
//
//  ★ 게임을 돌려 보지 않고 알 수 있는 것은 여기서 잡는다:
//    ① 쓴 글을 다시 읽으면 **같은 저장**이 되는가 (왕복)
//    ② ★ **같은 저장이면 같은 글**인가 — PlayScene 이 글끼리 비교해서 쓸지를 정한다
//    ③ 깨진 글 · 모르는 판을 **거절**하는가. 없는 칸은 「없다」로 읽는가
//    ④ 파일: 없음과 깨짐을 구분하는가 · 바꿔치기 뒤에 임시 파일이 안 남는가
//    ⑤ ★ 두 파일에 걸친 약속: 기본 저장의 물건이 items.json 에, 첫 맵이 maps/ 에 있는가
//
//  ※ 파일 시험은 **saves/_test/** 에서만 한다. saves/save.json(진짜 저장)은 안 건드린다.
// ============================================================================
#include "Gameplay/SaveData.h"
#include "Core/Json.h"

#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

static int g_fail = 0;
static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "  ok" : "FAIL", what);
    if (!ok) ++g_fail;
}

static bool SameDrops(const std::vector<SavedDrop>& a, const std::vector<SavedDrop>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].map != b[i].map || a[i].item != b[i].item) return false;
        if (a[i].x != b[i].x || a[i].y != b[i].y)           return false;
    }
    return true;
}

static bool Same(const SaveData& a, const SaveData& b)
{
    return a.hands == b.hands && a.bag == b.bag && a.armor == b.armor && a.rings == b.rings
        && a.restMap == b.restMap && a.restPoint == b.restPoint
        && a.openedChests == b.openedChests && SameDrops(a.drops, b.drops);
}

static std::string ReadAll(const std::wstring& path)
{
    std::string out;
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp)
        return out;

    char   buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0)
        out.append(buf, n);
    std::fclose(fp);
    return out;
}

int main()
{
    std::string err;

    // ========================================================================
    //  ⑤ 기본 저장
    // ========================================================================
    std::printf("-- NewGame --\n");
    const SaveData fresh = SaveData::NewGame();
    Check(fresh.hands[0] == "dagger" && fresh.hands[1].empty(), "단검은 오른손, 왼손은 비었다");
    Check(fresh.armor.size() == 4,                              "천 한 벌 = 네 조각");
    Check(!fresh.rings.empty() && fresh.rings[0] == "novice_ring", "初心者の指輪 를 끼고 시작한다");
    Check(fresh.bag.empty(),                                    "가방은 빈 채로 (물건은 상자에)");
    Check(fresh.restPoint.empty(),                              "아직 아무 데서도 안 쉬었다 → 맵의 start");
    Check(fresh.openedChests.empty() && fresh.drops.empty(),    "열린 상자도 바닥의 물건도 없다");

    // ★★ 두 파일에 걸친 약속. 기본 저장이 items.json 에 없는 물건을 적고 있으면
    //   새 게임이 **맨몸으로** 시작한다(RestoreLoadout 이 버린다) — 저장 코드만 봐도,
    //   물건 파일만 봐도 안 보이는 실수다.
    auto items = Json::ParseFile(L"assets/data/items.json", &err);
    Check(items.has_value(), "items.json 을 읽었다");
    if (items)
    {
        const JsonValue& cat = (*items)["items"];

        std::vector<std::string> ids;
        for (const std::string& id : fresh.hands) if (!id.empty()) ids.push_back(id);
        for (const std::string& id : fresh.armor) if (!id.empty()) ids.push_back(id);
        for (const std::string& id : fresh.rings) if (!id.empty()) ids.push_back(id);

        bool known = true;
        for (const std::string& id : ids)
        {
            if (cat[id].IsNull())
            {
                known = false;
                std::printf("      없는 물건: %s\n", id.c_str());
            }
        }
        Check(known, "기본 저장의 물건이 전부 items.json 에 있다");

        // ★ **자리가 맞는가.** 틀리면 RestoreLoadout 이 가방으로 보낸다 —
        //   「새 게임인데 왜 투구가 가방에?」가 된다.
        std::set<std::string> slots;
        bool armorOk = true;
        for (const std::string& id : fresh.armor)
        {
            const std::string slot = cat[id]["armorSlot"].Str();
            if (slot.empty() || !slots.insert(slot).second) armorOk = false;
        }
        Check(armorOk, "천 한 벌이 전부 방어구이고 부위가 안 겹친다");
        Check(cat[fresh.rings[0]]["ring"].Bool(false), "初心者 는 지문이다 (지문 칸에 들어간다)");
        Check(cat[fresh.hands[0]]["armorSlot"].IsNull() && !cat[fresh.hands[0]]["ring"].Bool(false),
              "단검은 손에 드는 것이다");
    }

    const std::wstring firstMap =
        L"assets/data/maps/" + std::wstring(fresh.restMap.begin(), fresh.restMap.end()) + L".json";
    Check(Json::ParseFile(firstMap.c_str(), &err).has_value(), "기본 저장의 첫 맵 파일이 있다");

    // ========================================================================
    //  ① ② 왕복
    // ========================================================================
    std::printf("-- 왕복 --\n");
    SaveData full;
    full.hands = { "greatsword", "greatsword" };                       // 양손 무기 = 두 칸
    full.bag   = { "torch", "", "", "kite", "", "plate_helm" };        // 빈 칸이 사이사이에
    full.armor = { "plate_mail", "", "cloth_pants", "hunter_boots" };  // 빈 부위가 있다
    full.rings = { "", "firefly_ring" };                               // 왼쪽 칸만 비었다
    full.restMap   = "cave";
    full.restPoint = "deep";
    full.openedChests = { "cave:firefly", "field:torch" };
    full.drops = {
        { "cave",  "buckler",  470.5f, 640.0f },
        { "field", "dagger",  1234.0f, 456.0f },
    };

    const std::string text = SaveIO::ToText(full);
    std::printf("%s", text.c_str());   // 보기용 — 파일이 이렇게 생겼다

    SaveData back;
    Check(SaveIO::FromText(text, back, &err), "쓴 글을 다시 읽는다");
    Check(Same(back, full), "왕복해도 같다 (양손 무기 두 칸 · 가방의 빈 칸 · 지문의 자리까지)");
    Check(SaveIO::ToText(back) == text, "★ 같은 저장이면 같은 글이다 (1초마다의 비교가 여기에 기댄다)");

    // ★ 소수가 글에서 한 자리로 잘려도 **글은 흔들리지 않는다.** 470.04 는 470.0 이
    //   되어 돌아오지만, 그걸 다시 쓰면 같은 글이다 — 그래서 한 번 쓰고 나면
    //   가만히 있는 물건이 매초 다시 써지는 일이 없다.
    SaveData fuzzy = full;
    fuzzy.drops[0].x = 470.04f;
    SaveData fuzzyBack;
    const std::string fuzzyText = SaveIO::ToText(fuzzy);
    Check(SaveIO::FromText(fuzzyText, fuzzyBack, &err) && SaveIO::ToText(fuzzyBack) == fuzzyText,
          "소수가 잘려도 다시 쓰면 같은 글이다");

    // 이스케이프 — **우리 파서가 읽는 것만** 쓰는가(따옴표 · 역슬래시 · 줄바꿈 · 탭)
    SaveData odd = full;
    odd.restPoint = std::string("a") + char(0x22) + "b" + char(0x5C) + "c" + char(0x0A) + char(0x09) + "d";
    SaveData oddBack;
    Check(SaveIO::FromText(SaveIO::ToText(odd), oddBack, &err) && oddBack.restPoint == odd.restPoint,
          "따옴표 · 역슬래시 · 줄바꿈 · 탭도 왕복한다");

    SaveData freshBack;
    Check(SaveIO::FromText(SaveIO::ToText(fresh), freshBack, &err) && Same(freshBack, fresh),
          "기본 저장도 왕복한다 (빈 목록 · 빈 칸)");

    // ========================================================================
    //  ③ 거절 · 없는 칸
    // ========================================================================
    std::printf("-- 거절 --\n");
    SaveData keep = full;
    Check(!SaveIO::FromText(R"({ "version": 2 })", keep, &err), "모르는 판(2)은 거절한다");
    std::printf("      (%s)\n", err.c_str());
    Check(!SaveIO::FromText(R"({ "bag": [] })", keep, &err),   "판이 없으면 거절한다");
    Check(!SaveIO::FromText("[1, 2, 3]", keep, &err),          "객체가 아니면 거절한다");
    Check(!SaveIO::FromText(text.substr(0, text.size() / 2), keep, &err), "반쯤 잘린 글은 거절한다");
    Check(Same(keep, full), "거절하면 out 을 안 건드린다");

    // ★★ 없는 칸은 「없다」 — 시작 장비가 끼어들지 않는다(저장은 기록이다)
    SaveData bare;
    Check(SaveIO::FromText(R"({ "version": 1 })", bare, &err), "판만 있는 글도 읽힌다");
    Check(bare.hands[0].empty() && bare.armor.empty() && bare.rings.empty() && bare.restMap.empty(),
          "★ 없는 칸은 비어 있다 — 단검·천 옷이 몰래 생기지 않는다");

    SaveData partial;
    Check(SaveIO::FromText(R"({ "version": 1, "drops": [ { "map": "field", "x": 1 },
                                                         { "map": "cave", "item": "kite", "x": 5, "y": 6 } ] })",
                           partial, &err)
          && partial.drops.size() == 1 && partial.drops[0].item == "kite",
          "어디에 무엇인지 모르는 물건은 건너뛴다");

    // ========================================================================
    //  ④ 파일
    // ========================================================================
    std::printf("-- 파일 (saves/_test/) --\n");
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = L"saves/_test";
    fs::remove_all(dir, ec);   // 지난번에 중간에 멈췄으면 남은 것을 치운다

    const std::wstring path = (dir / L"save.json").wstring();
    SaveData loaded;

    Check(SaveIO::Load(path.c_str(), loaded, &err) == SaveIO::LoadResult::Missing,
          "없는 파일 → Missing (「깨졌다」가 아니다)");

    Check(SaveIO::Write(path.c_str(), text, &err), "쓴다 (폴더가 없으면 만든다)");
    Check(!fs::exists(path + L".tmp", ec),         "임시 파일이 안 남았다 (바꿔치기 됐다)");

    const std::string raw = ReadAll(path);
    Check(raw.size() > 3 && static_cast<unsigned char>(raw[0]) == 0xEF
                         && static_cast<unsigned char>(raw[1]) == 0xBB
                         && static_cast<unsigned char>(raw[2]) == 0xBF, "BOM 이 붙었다");
    Check(raw.substr(3) == text, "파일 내용 = 글 그대로");

    Check(SaveIO::Load(path.c_str(), loaded, &err) == SaveIO::LoadResult::Ok && Same(loaded, full),
          "파일로 왕복해도 같다");

    Check(SaveIO::Write(path.c_str(), SaveIO::ToText(fresh), &err), "이미 있는 파일을 바꿔치기한다");
    Check(SaveIO::Load(path.c_str(), loaded, &err) == SaveIO::LoadResult::Ok && Same(loaded, fresh),
          "바꿔친 뒤에는 새 내용이다");

    // 반쯤 쓰인 파일 — 바꿔치기가 **막으려는** 바로 그것을 일부러 만든다
    {
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, path.c_str(), L"wb") == 0 && fp)
        {
            std::fputs(text.substr(0, text.size() / 2).c_str(), fp);
            std::fclose(fp);
        }
    }
    SaveData untouched = full;
    Check(SaveIO::Load(path.c_str(), untouched, &err) == SaveIO::LoadResult::Broken,
          "반쯤 쓰인 파일 → Broken");
    std::printf("      (%s)\n", err.c_str());
    Check(Same(untouched, full), "깨졌으면 out 을 안 건드린다");

    Check(SaveIO::SetAside(path.c_str(), &err), "깨진 파일을 옆으로 치운다");
    Check(!fs::exists(path, ec) && fs::exists(dir / L"save.bad.json", ec),
          "save.json 은 없고 save.bad.json 이 있다 (지우지 않았다)");

    fs::remove_all(dir, ec);
    fs::remove(L"saves", ec);   // ★ **비어 있을 때만** 지워진다 — 진짜 저장이 있으면 남는다
    Check(!fs::exists(dir, ec), "시험 폴더를 치웠다");

    std::printf("\n%s (%d)\n", g_fail ? "== 실패 있음 ==" : "== 전부 통과 ==", g_fail);
    return g_fail;
}
