// ============================================================================
//  Assets.h
//    한 번 읽은 리소스를 경로를 열쇠로 보관해 두고 재사용한다.
//
//    없으면 이런 일이 생긴다:
//      - Scene 에 들어올 때마다 같은 PNG 를 디코딩하고 GPU 텍스처를 새로 만든다
//      - 적 10 종이 같은 시트를 쓰면 텍스처가 10 개 생긴다 (VRAM 낭비)
//      - 같은 그림인데 텍스처가 달라서 SpriteBatch 가 배치를 못 묶는다
//
//    마지막 항목이 특히 크다. 5단계에서 스프라이트 시트를 하나로 모으는
//    이유가 드로우 콜을 줄이기 위해서인데, 중복 로드가 그 노력을 무효로 만든다.
//
//    ---- 소유권 ----
//    ComPtr 이 이미 참조 카운트를 세므로 캐시가 하나를 들고,
//    사용하는 쪽이 각자 하나를 든다. 캐시를 비우면 아무도 안 쓰는 것부터 사라진다.
// ============================================================================
#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <string_view>
#include <unordered_map>

class Renderer;

class Assets
{
public:
    using TextureHandle = Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;

    void Initialize(Renderer& renderer);

    // 같은 경로를 다시 요청하면 디스크를 읽지 않고 보관해 둔 것을 돌려준다.
    // 실패하면 nullptr. 그 실패도 캐시된다(아래 .cpp 주석 참조).
    TextureHandle Texture(std::wstring_view path);

    // ★ 반드시 Renderer::Shutdown 보다 먼저 호출해야 한다.
    //   D3D 디바이스가 사라진 뒤에 텍스처를 해제하면 안 된다.
    void Clear();

    // F3 오버레이에 찍어서 캐시가 실제로 일하는지 눈으로 본다.
    size_t Count()     const { return m_textures.size(); }
    size_t LoadCount() const { return m_loadCount; }   // 실제로 디스크를 읽은 횟수
    size_t HitCount()  const { return m_hitCount;  }   // 캐시가 막아 준 횟수

private:
    // 「assets/a.png」 「assets\a.png」 「Assets/A.PNG」 가 전부 다른 열쇠가 되면
    // 캐시가 무용지물이 된다. 구분자와 대소문자를 통일한다.
    static std::wstring NormalizeKey(std::wstring_view path);

    Renderer* m_renderer = nullptr;
    std::unordered_map<std::wstring, TextureHandle> m_textures;

    size_t m_loadCount = 0;
    size_t m_hitCount  = 0;
};
