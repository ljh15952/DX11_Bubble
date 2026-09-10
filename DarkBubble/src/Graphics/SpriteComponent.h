// ============================================================================
//  SpriteComponent.h
//    스프라이트시트 한 장을 애니메이션시켜 그린다.
//
//  ---- ★ 이것이 첫 엔진 컴포넌트인 이유 ----
//    §9 의 규칙은 「두 번째 사용자가 생겼을 때 엔진으로 올린다」인데,
//    이건 **이미 두 명이 쓰고 있다.** PlayScene 안에 같은 코드가 두 번 있다:
//
//        renderer.Sprites().Draw(sheet, {round(x), round(y)}, &src, tint,
//                                0.0f, {originX, originY}, scale,
//                                facing < 0 ? FlipHorizontally : None);
//
//    인자가 8개인 호출이 플레이어용·적용으로 복사되어 있고,
//    「정수로 반올림」과 「facing 으로 반전」이라는 규칙도 두 벌이다.
//    그림을 그리는 규칙이 두 곳에 있으면 한쪽만 고치는 날이 온다.
//
//  ---- 이 컴포넌트가 소유하는 것 ----
//    텍스처 · 애니메이션 재생 상태 · 원점 · 틴트 · 스케일 · 표시 여부.
//    **위치와 방향은 소유하지 않는다** — 그건 Transform 의 것이고,
//    여기서는 Owner().transform 을 읽기만 한다.
//    이 경계가 「무엇이 무엇을 아는가」를 명확하게 유지한다.
// ============================================================================
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <DirectXMath.h>
#include <DirectXColors.h>

#include "Core/Component.h"
#include "Graphics/Animation.h"

class SpriteComponent final : public Component
{
public:
    // cellW / cellH = 시트 한 칸의 크기.
    SpriteComponent(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sheet,
                    int cellW, int cellH);

    const char* TypeName() const override { return "Sprite"; }

    void Tick(SceneContext& ctx, bool consumeEdgeInput) override;      // 애니메이션을 1틱 진행
    void Render(Renderer& renderer) override;

    // ---- 애니메이션 ----
    //   ★ AnimationPlayer 를 밖으로 노출하지 않는다(캡슐화).
    //     노출하면 밖에서 Tick 을 또 부르는 코드가 생기고,
    //     그러면 애니메이션이 두 배로 빨라진다 — 이 프로젝트가 이미 밟은 종류의 함정이다.
    void Play(const AnimationClip& clip, bool forceRestart = false);
    bool Finished() const { return m_anim.Finished(); }
    int  Frame()    const { return m_anim.Frame(); }

    // ---- 표현 ----
    void SetOrigin(float x, float y) { m_originX = x; m_originY = y; }
    void SetTint(DirectX::FXMVECTOR color);
    void ClearTint()                 { SetTint(DirectX::Colors::White); }
    void SetScale(float x, float y)  { m_scaleX = x; m_scaleY = y; }
    void SetVisible(bool v)          { m_visible = v; }

private:
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sheet;
    AnimationPlayer m_anim;

    int m_cellW = 0;
    int m_cellH = 0;

    // 원점은 기본이 「발밑 가운데」다 — 이 프로젝트의 규칙(§3.7 계열).
    // 세로로 눌러도 발이 뜨지 않고, 공격 히트박스 계산이 짧아진다.
    float m_originX = 0.0f;
    float m_originY = 0.0f;

    // ★ XMVECTOR 가 아니라 XMFLOAT4 로 들고 있는다.
    //   XMVECTOR 는 16바이트 정렬을 요구하는데 이 객체는 힙에 만들어진다.
    //   XMFLOAT4 는 정렬 제약이 없고, 쓸 때 XMLoadFloat4 로 올리면 된다.
    DirectX::XMFLOAT4 m_tint{ 1.0f, 1.0f, 1.0f, 1.0f };

    float m_scaleX  = 1.0f;
    float m_scaleY  = 1.0f;
    bool  m_visible = true;
};
