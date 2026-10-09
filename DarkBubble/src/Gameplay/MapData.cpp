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
    m.dark        = world["dark"]   .Bool(m.dark);

    const JsonValue& solids = (*root)["solids"];
    for (size_t i = 0; i < solids.Size(); ++i)
        m.solids.push_back(ReadBox(solids[i]));

    // ★ 발판은 **따로** 적는다. 같은 배열에 두고 「얇으면 발판」으로 추리하면
    //   얇은 천장(동굴 윗면)까지 발판이 되어 뛰어 올라가 버린다 — 모양이
    //   비슷하다고 뜻이 같은 것은 아니다.
    const JsonValue& platforms = (*root)["platforms"];
    for (size_t i = 0; i < platforms.Size(); ++i)
        m.platforms.push_back(ReadBox(platforms[i]));

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

    // ---- 상자 (8-h) ----
    //   ★ id 나 item 이 없으면 **건너뛴다.** id 가 없으면 「열렸는가」를 기억할
    //     이름이 없고, item 이 없으면 여는 의미가 없다 — 둘 다 조용히 틀린
    //     상자가 되느니 없는 편이 낫다.
    const JsonValue& chests = (*root)["chests"];
    for (size_t i = 0; i < chests.Size(); ++i)
    {
        const JsonValue& c = chests[i];
        MapInteract it;
        it.kind = InteractKind::Chest;
        it.box  = ReadBox(c["box"]);
        it.id   = c["id"]  .Str();
        it.item = c["item"].Str();
        it.name = it.item;

        if (it.id.empty() || it.item.empty())
        {
            Log::Info("[map] id 나 item 이 없는 상자가 있다 — 건너뛴다");
            continue;
        }
        m.interacts.push_back(std::move(it));
    }

    // ---- NPC (10-a) ----
    // ★ NPC 를 화톳불 **앞에** 읽는다. 목록 순서가 곧 초점의 우선순위다(UpdateFocus) —
    //   둘 사이에 서면 먼저 읽힌 쪽이 잡힌다. 말 걸기는 Esc 로 닫으면 그만이지만
    //   쉬기는 적을 되살린다 — **되돌릴 수 없는 쪽을 뒤로** 민다(줍기가 앞인 것과 같다).
    //   ★ id 와 dialogue 가 **필수**다. dialogue 가 없으면 말을 걸어도 할 말이
    //     없고, id 가 없으면 10-b 에서 「이 사람과 이야기했다」를 기억할 이름이
    //     없다 — 상자 · 화톳불과 같은 태도로 **건너뛴다.**
    const JsonValue& npcs = (*root)["npcs"];
    for (size_t i = 0; i < npcs.Size(); ++i)
    {
        const JsonValue& n = npcs[i];
        MapInteract it;
        it.kind     = InteractKind::Npc;
        it.box      = ReadBox(n["box"]);
        it.id       = n["id"]      .Str();
        it.dialogue = n["dialogue"].Str();
        it.row      = n["row"]     .Int(0);
        it.facing   = (n["facing"].Int(1) < 0) ? -1 : 1;
        it.name     = it.id;

        if (it.id.empty() || it.dialogue.empty())
        {
            Log::Info("[map] id 나 dialogue 가 없는 NPC 가 있다 — 건너뛴다");
            continue;
        }
        m.interacts.push_back(std::move(it));
    }

    // ---- 세이브 포인트(화톳불) ----
    //   ★ id 가 **필수**다(9) — 상자와 같은 이유. id 가 없으면 저장 파일이
    //     「여기서 쉬었다」를 적을 수가 없어서, 껐다 켜면 맵의 시작에서 일어난다.
    //     그건 게임을 껐다 켜 봐야 드러난다 — 조용히 틀린 화톳불이 되느니
    //     **안 보이는** 편이 맵을 만들 때 바로 보인다.
    const JsonValue& saves = (*root)["savePoints"];
    for (size_t i = 0; i < saves.Size(); ++i)
    {
        const JsonValue& s = saves[i];
        MapInteract it;
        it.kind = InteractKind::SavePoint;
        it.box  = ReadBox(s["box"]);
        it.id   = s["id"]  .Str();
        it.name = s["name"].Str("save");

        if (it.id.empty())
        {
            Log::Info("[map] id 가 없는 화톳불 '{}' — 건너뛴다", it.name);
            continue;
        }
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
