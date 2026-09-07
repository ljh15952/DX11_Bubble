// ============================================================================
//  Renderer.h
//    D3D11 장치와 그리기 수단을 소유한다.
//    "무엇을 그릴지" 는 모른다. 그건 Game 의 몫이다.
// ============================================================================
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

#include <SpriteBatch.h>
#include <CommonStates.h>
#include <DirectXColors.h>

#include "Core/AABB.h"

class Renderer
{
public:
    bool Initialize(HWND hwnd, int width, int height);

    // 전역/멤버 소멸 순서 문제를 피하려고 명시적으로 놓는다.
    void Shutdown();

    // 이미지 파일을 읽어 SRV(셰이더 입력 뷰)를 돌려준다. 실패하면 nullptr.
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> LoadTexture(const wchar_t* path);

    // BeginFrame  : 렌더 타겟 바인딩 -> 화면 클리어 -> SpriteBatch 시작
    // EndFrame    : SpriteBatch 종료 -> Present
    // 그 사이에 Sprites().Draw(...) 를 호출한다.
    void BeginFrame();
    void EndFrame();

    DirectX::SpriteBatch& Sprites() { return *m_spriteBatch; }

    // ---- 도형 그리기 ----
    //   1×1 흰 텍스처를 목표 사각형 크기로 늘려서 그린다.
    //   SpriteBatch 만으로 선과 상자를 그리는 표준 트릭이다.
    //   전용 셰이더나 정점 버퍼를 만들 필요가 없다.
    void DrawFilledRect(const AABB& box, DirectX::FXMVECTOR color);
    void DrawRectOutline(const AABB& box, DirectX::FXMVECTOR color, float thickness = 1.0f);

private:
    bool CreateWhitePixel();

    Microsoft::WRL::ComPtr<ID3D11Device>           m_device;      // 공장   : 리소스를 만든다
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_context;     // 리모컨 : 명령을 쏜다
    Microsoft::WRL::ComPtr<IDXGISwapChain>         m_swapChain;   // 화면   : Present
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;         // 그림 대상 지정

    // 도형 그리기용 1×1 흰 텍스처
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_whitePixel;

    // DirectXTK 는 COM 이 아니라 평범한 C++ 클래스다. 그래서 unique_ptr.
    std::unique_ptr<DirectX::SpriteBatch>  m_spriteBatch;
    std::unique_ptr<DirectX::CommonStates> m_states;
};
