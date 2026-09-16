#include "Gameplay/MapData.h"

#include "Core/Json.h"
#include "Core/Log.h"

namespace
{
    // [x0, y0, x1, y1] 을 읽는다. 사각형은 **배열**이 읽기 좋다 —
    //   네 값의 순서가 왼쪽·위·오른쪽·아래로 이미 널리 쓰이는 관례이고,
    //   이름을 붙이면 파일이 네 배로 길어진다.
    //   ★ 부위 HP 를 이름으로 적은 것과 반대로 보이지만 기준은 하나다:
    //     **나중에 항목이 늘어날 수 있는 것은 이름으로, 고정된 것은 배열로.**
    //     사각형은 영원히 네 값이다.
    AABB ReadBox(const JsonValue& v)
    {
        return { v[size_t(0)].Flt(), v[size_t(1)].Flt(),
                 v[size_t(2)].Flt(), v[size_t(3)].Flt() };
    }
}


float MapData::EntryX(const std::string& name) const
{
    auto it = entries.find(name);
    if (it != entries.end())
        return it->second;

    // ★ 없으면 죽지 않는다. 맵 하나 고치다 오타가 났다고 게임이 멈추면
    //   맵을 만드는 일이 무서워진다 — JSON 로더들과 같은 태도다.
    Log::Info("[map] 입구 '{}' 가 없다 — start 로 대신한다", name);

    it = entries.find("start");
    if (it != entries.end())
        return it->second;

    Log::Info("[map] start 도 없다 — 맵 가운데에 놓는다");
    return worldWidth * 0.5f;
}


bool MapIO::Load(const wchar_t* path, MapData& out, std::string* error)
{
    std::string err;
    const auto root = Json::ParseFile(path, &err);
    if (!root)
    {
        if (error) *error = err;
        return false;
    }

    // ★ 통째로 만들어 두고 마지막에 옮긴다. 맵은 부분 갱신이 의미 없고,
    //   도중에 실패했을 때 **절반만 바뀐 맵**이 남으면 그게 제일 나쁘다.
    MapData m;

    const JsonValue& world = (*root)["world"];
    m.worldWidth  = world["w"].Flt(m.worldWidth);
    m.worldHeight = world["h"].Flt(m.worldHeight);
    m.groundY     = world["groundY"].Flt(m.groundY);

    const JsonValue& solids = (*root)["solids"];
    for (size_t i = 0; i < solids.Size(); ++i)
        m.solids.push_back(ReadBox(solids[i]));

    const JsonValue& enemies = (*root)["enemies"];
    for (size_t i = 0; i < enemies.Size(); ++i)
    {
        const JsonValue& e = enemies[i];
        MapEnemySpawn s;
        s.x      = e["x"]     .Flt(s.x);
        s.facing = e["facing"].Int(s.facing);
        s.type   = e["type"]  .Str(s.type);
        m.enemies.push_back(std::move(s));
    }

    // ★ 파일에서는 나뉘어 있고(읽기 좋다) 코드에서는 한 목록이다(찾기 좋다).
    const JsonValue& portals = (*root)["portals"];
    for (size_t i = 0; i < portals.Size(); ++i)
    {
        const JsonValue& p = portals[i];
        MapInteract it;
        it.kind  = InteractKind::Portal;
        it.box   = ReadBox(p["box"]);
        it.to    = p["to"]   .Str();
        it.entry = p["entry"].Str("start");
        it.name  = p["name"] .Str();

        if (it.to.empty())
        {
            Log::Info("[map] 목적지 없는 포탈이 있다 — 건너뛴다");
            continue;
        }
        m.interacts.push_back(std::move(it));
    }

    const JsonValue& saves = (*root)["savePoints"];
    for (size_t i = 0; i < saves.Size(); ++i)
    {
        const JsonValue& s = saves[i];
        MapInteract it;
        it.kind = InteractKind::SavePoint;
        it.box  = ReadBox(s["box"]);
        it.name = s["name"].Str("save");
        m.interacts.push_back(std::move(it));
    }

    for (const auto& kv : (*root)["entries"].Members())
        m.entries[kv.first] = kv.second.Flt();

    if (m.solids.empty())
    {
        // ★ 지형이 없으면 플레이어가 영원히 떨어진다. 그건 읽기 실패로 친다 —
        //   「읽히기는 했는데 못 노는 맵」은 조용해서 더 나쁘다.
        if (error) *error = "solids 가 비어 있다";
        return false;
    }

    out = std::move(m);
    return true;
}
