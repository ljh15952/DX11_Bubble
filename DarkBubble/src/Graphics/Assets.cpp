#include "Graphics/Assets.h"
#include "Graphics/Renderer.h"
#include "Core/Log.h"


void Assets::Initialize(Renderer& renderer)
{
    m_renderer = &renderer;
}


std::wstring Assets::NormalizeKey(std::wstring_view path)
{
    std::wstring key(path);
    for (wchar_t& c : key)
    {
        if (c == L'\\')
            c = L'/';                              // 경로 구분자 통일
        else if (c >= L'A' && c <= L'Z')
            c = static_cast<wchar_t>(c + 32);      // ASCII 범위만 소문자화
    }
    return key;

    // 유니코드 전체를 제대로 소문자화하려면 로캘에 따라 규칙이 달라진다
    // (터키어의 I 문제 등). 리소스 경로는 ASCII 로 유지하는 편이 안전하다.
}


Assets::TextureHandle Assets::Texture(std::wstring_view path)
{
    if (!m_renderer)
    {
        Log::Error("[assets] Initialize 가 호출되지 않았다");
        return nullptr;
    }

    const std::wstring key = NormalizeKey(path);

    const auto it = m_textures.find(key);
    if (it != m_textures.end())
    {
        ++m_hitCount;
        return it->second;   // 실패(nullptr)였던 것도 그대로 돌려준다
    }

    // ★ 실패한 결과도 캐시에 넣는다.
    //   없는 파일을 Scene 에 들어올 때마다 다시 읽으면
    //   매번 디스크를 건드리고 에러 로그가 도배된다.
    //   대신 파일을 고친 뒤에는 프로그램을 다시 시작해야 반영된다.
    //   (개발 중에 핫 리로드가 필요해지면 그때 Reload() 를 추가하면 된다)
    const std::wstring full(path);
    TextureHandle tex = m_renderer->LoadTexture(full.c_str());
    ++m_loadCount;
    m_textures.emplace(key, tex);
    return tex;
}


void Assets::Clear()
{
    if (!m_textures.empty())
    {
        Log::Info("[assets] 캐시 비움 — 보관 {} 개, 디스크 읽기 {} 회, 캐시 적중 {} 회",
                  m_textures.size(), m_loadCount, m_hitCount);
    }
    m_textures.clear();
}
