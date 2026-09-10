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


void SpriteComponent::Tick(SceneContext&)
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

    renderer.Sprites().Draw(
        m_sheet.Get(),
        pos,
        &src,
        DirectX::XMLoadFloat4(&m_tint),
        0.0f,                                              // 회전 없음
        DirectX::XMFLOAT2(m_originX, m_originY),
        DirectX::XMFLOAT2(m_scaleX, m_scaleY),
        fx);
}
