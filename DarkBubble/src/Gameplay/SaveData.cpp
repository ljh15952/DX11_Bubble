#include "Gameplay/SaveData.h"

#include "Core/Json.h"

#include <cstdio>
#include <filesystem>
#include <format>
#include <io.h>        // _commit
#include <windows.h>   // MoveFileExW

namespace
{
    bool Fail(std::string* error, std::string what)
    {
        if (error) *error = std::move(what);
        return false;
    }

    // ---- 쓰기 ----
    //   ★ 일반적인 JSON 쓰개를 만들지 않는다. **우리가 정한 모양 하나**만 쓰면
    //     되므로 도우미 셋이면 끝난다 — 읽기(Json.cpp)는 어떤 모양이든 받아야
    //     해서 파서가 필요했지만, 쓰기는 아니다.
    constexpr char kQuote     = 0x22;   // "
    constexpr char kBackslash = 0x5C;   // 역슬래시 (※ 일본어 로캘 글꼴에서는 ¥ 로 보인다)

    // 문자열 하나를 JSON 글로. ★ **우리 파서가 읽는 이스케이프만** 쓴다(\uXXXX 는 없다).
    //   쓰는 쪽과 읽는 쪽이 같은 약속을 지켜야 왕복이 된다 — save_test 가 확인한다.
    std::string Quote(std::string_view s)
    {
        std::string out(1, kQuote);
        for (const char c : s)
        {
            switch (c)
            {
            case kQuote:     out += kBackslash; out += kQuote;     break;
            case kBackslash: out += kBackslash; out += kBackslash; break;
            case 0x0A:       out += kBackslash; out += 'n';        break;
            case 0x09:       out += kBackslash; out += 't';        break;
            case 0x0D:       out += kBackslash; out += 'r';        break;
            default:         out += c;                             break;
            }
        }
        out += kQuote;
        return out;
    }

    // ["a", "", "b"] — ★ **빈 칸도 적는다.** 가방은 위치가 정보다.
    std::string List(const std::vector<std::string>& v)
    {
        std::string out = "[";
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i) out += ", ";
            out += Quote(v[i]);
        }
        return out + "]";
    }

    // 맨 바깥 키 하나. `  "name": `
    std::string Key(std::string_view name)
    {
        return "  " + Quote(name) + ": ";
    }

    // ---- 읽기 ----
    // 이름 목록. ★ 빈 문자열도 **그대로** 둔다 — 빈 칸이다.
    std::vector<std::string> ReadNames(const JsonValue& v)
    {
        std::vector<std::string> out;
        for (size_t i = 0; i < v.Size(); ++i)
            out.push_back(v[i].Str());
        return out;
    }

    bool ReadSave(const JsonValue& root, SaveData& out, std::string* error)
    {
        // ★ 판부터 본다. 객체가 아니면 version 이 Null 이라 0 이 되어 여기서 걸린다.
        const int version = root["version"].Int(0);
        if (version != SaveIO::kVersion)
            return Fail(error, std::format("모르는 판 {} (이 게임은 {})", version, SaveIO::kVersion));

        // ★★ **빈 저장에서** 출발한다 — NewGame 이 아니다(FromText 주석).
        SaveData s;

        const JsonValue& hands = root["hands"];
        s.hands[0] = hands["right"].Str();
        s.hands[1] = hands["left"] .Str();

        s.bag   = ReadNames(root["bag"]);
        s.armor = ReadNames(root["armor"]);
        s.rings = ReadNames(root["rings"]);

        const JsonValue& rest = root["rest"];
        s.restMap   = rest["map"]  .Str();
        s.restPoint = rest["point"].Str();

        const JsonValue& chests = root["openedChests"];
        for (size_t i = 0; i < chests.Size(); ++i)
        {
            std::string key = chests[i].Str();
            if (!key.empty())
                s.openedChests.push_back(std::move(key));
        }

        const JsonValue& drops = root["drops"];
        for (size_t i = 0; i < drops.Size(); ++i)
        {
            const JsonValue& d = drops[i];
            SavedDrop sd;
            sd.map  = d["map"] .Str();
            sd.item = d["item"].Str();
            sd.x    = d["x"]   .Flt();
            sd.y    = d["y"]   .Flt();

            // 어느 맵의 무엇인지 모르면 놓을 수가 없다.
            if (sd.map.empty() || sd.item.empty())
                continue;
            s.drops.push_back(std::move(sd));
        }

        out = std::move(s);
        return true;
    }
}


// ============================================================================
//  NewGame — 기본 저장
//
//    ★ PlayerController::Start 에 있던 시작 장비가 그대로 왔다. 이유도 같이 왔다.
// ============================================================================
SaveData SaveData::NewGame()
{
    SaveData s;

    // ★ 단검을 오른손에. 손은 「이름 하나」라 기본값이 **빈손**이다 —
    //   여기서 들려 주지 않으면 맨손으로 시작한다(8-b 에서 실제로 그랬다).
    s.hands[0] = "dagger";

    // ★ 천 한 벌. 옛 CLOTH(poise 10)가 네 조각으로 갈라진 것이라 합이 그대로
    //   10 이다 — 8-f 전과 **같은 몸**으로 시작한다.
    s.armor = { "cloth_hood", "cloth_coat", "cloth_pants", "cloth_shoes" };

    // ★ 初心者の指輪 를 끼고 시작한다(§1.1 「입수: 최초」). 이게 있어서 적의 `!` 가
    //   보인다 — 빼 보면 그제야 「예고가 지문이었다」가 드러난다.
    s.rings = { "novice_ring" };

    // ★ 가방은 **빈 채로**(8-h). 물건은 맵의 상자에 있다 — 찾는 것이 없으면
    //   찾는 재미도 없다. 칸 수를 여기 안 적는다(PlayerController 가 안다).

    // 첫 맵. restPoint 가 비었다 = 아직 아무 데서도 안 쉬었다 → 그 맵의 start.
    s.restMap = "field";
    return s;
}


// ============================================================================
//  ToText — 저장 → 글
//
//    ★ 사람이 열어 볼 수 있게 줄을 나눈다. 그리고 **같은 상태면 같은 글**이다:
//      목록은 들어온 순서 그대로, 소수는 한 자리로 고정한다.
// ============================================================================
std::string SaveIO::ToText(const SaveData& s)
{
    std::string t = "{\n";
    t += Key("version") + std::to_string(kVersion) + ",\n";

    // 손은 둘로 고정이라 **이름을 붙인다**(오른손 · 왼손). [0] 과 [1] 로 적으면
    //   파일만 보고는 어느 쪽인지 모른다.
    t += Key("hands") + "{ " + Quote("right") + ": " + Quote(s.hands[0]) + ", "
                             + Quote("left")  + ": " + Quote(s.hands[1]) + " },\n";
    t += Key("bag")   + List(s.bag)   + ",\n";
    t += Key("armor") + List(s.armor) + ",\n";
    t += Key("rings") + List(s.rings) + ",\n";

    t += Key("rest") + "{ " + Quote("map")   + ": " + Quote(s.restMap)   + ", "
                            + Quote("point") + ": " + Quote(s.restPoint) + " },\n";

    t += Key("openedChests") + List(s.openedChests) + ",\n";

    t += Key("drops") + "[";
    for (size_t i = 0; i < s.drops.size(); ++i)
    {
        const SavedDrop& d = s.drops[i];
        t += (i == 0) ? "\n    " : ",\n    ";
        t += "{ " + Quote("map") + ": " + Quote(d.map) + ", "
                  + Quote("item") + ": " + Quote(d.item)
                  + std::format(", {}: {:.1f}, {}: {:.1f} }}", Quote("x"), d.x, Quote("y"), d.y);
    }
    t += s.drops.empty() ? "]\n" : "\n  ]\n";

    t += "}\n";
    return t;
}


bool SaveIO::FromText(std::string_view text, SaveData& out, std::string* error)
{
    std::string err;
    const auto root = Json::Parse(text, &err);
    if (!root)
        return Fail(error, err);
    return ReadSave(*root, out, error);
}


SaveIO::LoadResult SaveIO::Load(const wchar_t* path, SaveData& out, std::string* error)
{
    // ★ 「없다」를 **먼저 따로** 묻는다. Json::ParseFile 은 「못 열었다」와
    //   「깨졌다」를 둘 다 실패로 돌려주는데, 둘은 할 일이 다르다.
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return LoadResult::Missing;

    std::string err;
    const auto root = Json::ParseFile(path, &err);
    if (!root || !ReadSave(*root, out, &err))
    {
        if (error) *error = err;
        return LoadResult::Broken;
    }
    return LoadResult::Ok;
}


// ============================================================================
//  Write — 임시 파일에 쓰고 바꿔치기
// ============================================================================
bool SaveIO::Write(const wchar_t* path, const std::string& text, std::string* error)
{
    const std::filesystem::path target(path);
    const std::filesystem::path temp = target.wstring() + L".tmp";

    // 폴더가 없으면 만든다(처음 저장할 때). 이미 있으면 아무 일도 안 한다.
    std::error_code ec;
    if (target.has_parent_path())
        std::filesystem::create_directories(target.parent_path(), ec);

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, temp.c_str(), L"wb") != 0 || !fp)
        return Fail(error, "임시 파일을 열 수 없다");

    // ★ BOM 을 붙인다 — 이 프로젝트의 파일은 전부 그렇고(일본어 로캘 VS 대책),
    //   JSON 파서가 건너뛴다. 지금은 ASCII 뿐이지만 사람이 VS 로 열어 볼 수 있다.
    static const unsigned char kBom[3] = { 0xEF, 0xBB, 0xBF };

    // ★ fflush 는 C 런타임의 버퍼를 OS 에 넘길 뿐이다. _commit 이 OS 의 캐시를
    //   **디스크까지** 내린다. 이것 없이 바꿔치기하면 전원이 나갔을 때 이름만
    //   바뀐 **빈 파일**이 남을 수 있다 — 바꿔치기의 뜻이 사라진다.
    const bool written =
        std::fwrite(kBom, 1, sizeof(kBom), fp) == sizeof(kBom) &&
        std::fwrite(text.data(), 1, text.size(), fp) == text.size() &&
        std::fflush(fp) == 0 &&
        _commit(_fileno(fp)) == 0;
    std::fclose(fp);

    if (!written)
    {
        DeleteFileW(temp.c_str());
        return Fail(error, "임시 파일에 끝까지 쓰지 못했다");
    }

    // ★★ 한 번에 바꿔치기. 같은 드라이브 안의 이름 바꾸기에는 **중간이 없다** —
    //   옛 파일이거나 새 파일이거나 둘 중 하나다. WRITE_THROUGH = 바꾼 것까지 디스크에.
    if (!MoveFileExW(temp.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD code = GetLastError();   // ★ DeleteFileW 가 덮어쓰기 전에 읽는다
        DeleteFileW(temp.c_str());
        return Fail(error, std::format("바꿔치기 실패 (오류 {})", code));
    }
    return true;
}


bool SaveIO::SetAside(const wchar_t* path, std::string* error)
{
    // saves/save.json -> saves/save.bad.json
    //   ★ 앞서 치워 둔 것이 있으면 **새것으로 바꾼다.** 깨진 파일을 쌓아 두지는 않는다 —
    //     보고 싶은 것은 「방금 무엇이 깨졌나」다.
    std::filesystem::path bad(path);
    bad.replace_extension(L".bad.json");

    if (!MoveFileExW(path, bad.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return Fail(error, std::format("옮기기 실패 (오류 {})", GetLastError()));
    return true;
}
