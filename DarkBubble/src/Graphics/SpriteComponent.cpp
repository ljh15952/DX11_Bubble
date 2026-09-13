#include "Graphics/SpriteComponent.h"

#include "Core/GameObject.h"
#include "Graphics/Renderer.h"

#include <cmath>

SpriteComponent::SpriteComponent(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sheet,
                                 int cellW, int cellH)
    : m_sheet(std::move(sheet))
    , m_cellW(cellW)
    , m_cellH(cellH)
{
    // 기본 원점 = 발밑 가운데. 이 프로젝트의 좌표 규칙이다.
    m_originX = cellW * 0.5f;
    m_originY = static_cast<float>(cellH);
}


void SpriteComponent::Play(const AnimationClip& clip, bool forceRestart)
{
    m_anim.Play(clip, forceRestart);
}


void SpriteComponent::SetTint(DirectX::FXMVECTOR color)
{
    DirectX::XMStoreFloat4(&m_tint, color);
}


int SpriteComponent::AddLayer(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sheet,
                              bool visible)
{
    m_layers.push_back({ std::move(sheet), visible });
    return static_cast<int>(m_layers.size()) - 1;
}


void SpriteComponent::SetLayerVisible(int layer, bool visible)
{
    // ★ 범위 밖은 조용히 무시한다 — 「-1 = 레이어 없음」을 부르는 쪽에서
    //   따로 검사하지 않아도 되게 하려는 것이다.
    //   번호는 AddLayer 가 돌려준 값 그대로만 들어온다는 전제다.
    if (layer < 0 || layer >= static_cast<int>(m_layers.size()))
        return;

    m_layers[static_cast<size_t>(layer)].visible = visible;
}


void SpriteComponent::Tick(SceneContext&, bool)
{
    // ★ 애니메이션 진행을 컴포넌트가 스스로 한다.
    //
    //   전에는 PlayScene::Update 가 m_playerAnim.Tick() 과 m_enemyAnim.Tick() 을
    //   각각 불러야 했고, 실제로 **리팩터링에서 한 줄을 옮기지 않아 애니메이션이
    //   멈추는 버그**를 냈다(handoff §8).
    //   컴포넌트가 자기 시간을 스스로 굴리면 그 종류의 누락이 불가능해진다.
    m_anim.Tick();
}


void SpriteComponent::Render(Renderer& renderer)
{
    if (!m_visible || !m_sheet)
        return;

    const Transform& tr = Owner().transform;

    // ★ 「계산은 소수, 그리기는 정수」.
    //   소수 위치에 그리면 시트의 옆 칸을 물어와 1픽셀 선이 생긴다.
    //   이 규칙이 이제 **한 곳에만** 있다 — 전에는 두 곳에 복사되어 있었다.
    const DirectX::XMFLOAT2 pos{ std::round(tr.x), std::round(tr.y) };

    // 시트는 오른쪽을 보고 그려져 있으므로 왼쪽을 볼 때 뒤집는다.
    const DirectX::SpriteEffects fx = (tr.facing < 0)
        ? DirectX::SpriteEffects_FlipHorizontally
        : DirectX::SpriteEffects_None;

    const RECT src = m_anim.SourceRect(m_cellW, m_cellH);

    // ★ 인자를 **한 번만** 만든다. 아래 레이어들이 이걸 그대로 쓴다 —
    //   위치·칸·틴트·원점·반전이 한 곳에서 나오므로 레이어가 본체와
    //   어긋날 수가 없다.
    const DirectX::XMVECTOR tint   = DirectX::XMLoadFloat4(&m_tint);
    const DirectX::XMFLOAT2 origin{ m_originX, m_originY };
    const DirectX::XMFLOAT2 scale { m_scaleX,  m_scaleY  };

    renderer.Sprites().Draw(m_sheet.Get(), pos, &src, tint,
                            0.0f,          // 회전 없음
                            origin, scale, fx);

    // 겹쳐 그리기. 더한 순서가 곧 위로 올라가는 순서다 —
    // GameObject 의 「붙인 순서 = 실행 순서」와 같은 규칙이라 따로 외울 것이 없다.
    for (const Layer& layer : m_layers)
    {
        if (!layer.visible || !layer.sheet)
            continue;

        renderer.Sprites().Draw(layer.sheet.Get(), pos, &src, tint,
                                0.0f, origin, scale, fx);
    }
}
