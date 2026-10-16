#include "Gameplay/Npc.h"

#include "Core/Json.h"

namespace
{
    std::vector<std::string> ReadNames(const JsonValue& v)
    {
        std::vector<std::string> out;
        for (size_t i = 0; i < v.Size(); ++i)
        {
            std::string s = v[i].Str();
            if (!s.empty())
                out.push_back(std::move(s));
        }
        return out;
    }
}


bool NpcIO::LoadInto(const wchar_t* path, NpcCatalog& out, std::string* error)
{
    std::string err;
    const auto root = Json::ParseFile(path, &err);
    if (!root)
    {
        if (error) *error = err;
        return false;
    }

    NpcCatalog fresh;
    for (const auto& [id, n] : (*root)["npcs"].Members())
    {
        NpcType type;
        type.row = n["row"].Int(0);

        const JsonValue& talk = n["talk"];
        for (size_t i = 0; i < talk.Size(); ++i)
        {
            TalkOption t;
            t.ifAll  = ReadNames(talk[i]["if"]);
            t.ifNone = ReadNames(talk[i]["ifNot"]);
            t.say    = talk[i]["say"].Str();
            t.sets   = ReadNames(talk[i]["sets"]);

            if (t.say.empty())
                continue;   // 할 말이 없는 갈래는 고를 이유가 없다
            type.talk.push_back(std::move(t));
        }

        if (type.talk.empty())
            continue;
        fresh[id] = std::move(type);
    }

    if (fresh.empty())
    {
        if (error) *error = "npcs 가 비어 있다";
        return false;
    }

    out = std::move(fresh);
    return true;
}


const TalkOption* PickTalk(const NpcType& npc, const FlagSet& flags)
{
    for (const TalkOption& t : npc.talk)
    {
        bool ok = true;
        for (const std::string& f : t.ifAll)
            if (!flags.count(f)) { ok = false; break; }
        for (const std::string& f : t.ifNone)
            if (ok && flags.count(f)) { ok = false; break; }

        if (ok)
            return &t;
    }
    return nullptr;
}
