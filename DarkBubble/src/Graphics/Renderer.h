// ============================================================================
//  Renderer.h
//    D3D11 장치와 그리기 수단을 소유한다.
//    "무엇을 그릴지" 는 모른다. 그건 Game 의 몫이다.
//
//  ---- 2 패스 렌더링 ----
//    패스 1 : 게임을 저해상도 캔버스 텍스처(640x360)에 그린다
//    패스 2 : 그 텍스처를 창 크기에 맞춰 정수배로 확대해 백버퍼에 그린다
//
//    캔버스 텍스처는 RTV(그림 대상)와 SRV(셰이더 입력) 를 둘 다 가진다.
//    같은 메모리를 패스 1 에서는 캔버스로, 패스 2 에서는 그림으로 쓴다.
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
    bool Initialize(HWND hwnd, int windowWidth, int windowHeight,
                    int canvasWidth, int canvasHeight);

    // 창 크기가 바뀌었을 때 스왑체인 버퍼를 다시 만든다.
    // ★ 캔버스 텍스처는 640x360 그대로다. 백버퍼만 바뀐다.
    //   그래서 게임 로직과 좌표는 창 크기와 완전히 무관해진다.
    bool OnResize(int windowWidth, int windowHeight);

    // 전역/멤버 소멸 순서 문제를 피하려고 명시적으로 놓는다.
    void Shutdown();

    // 이미지 파일을 읽어 SRV(셰이더 입력 뷰)를 돌려준다. 실패하면 nullptr.
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> LoadTexture(const wchar_t* path);

    // BeginFrame : 패스 1 시작 — 캔버스 텍스처 바인딩, 뷰포트 640x360, 클리어
    // EndFrame   : 패스 1 종료 후 패스 2 실행 — 확대해서 백버퍼에 그리고 Present
    // 그 사이에 Sprites().Draw(...) 를 호출한다. 좌표는 캔버스 기준(640x360).
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
    bool CreateBackBufferTarget();
    bool CreateCanvasTarget(int width, int height);
    bool CreateWhitePixel();

    // 캔버스 텍스처를 화면 어디에 얼마나 크게 그릴지. 정수배 + 중앙 정렬(레터박스).
    RECT ComputeCanvasDestRect() const;

    Microsoft::WRL::ComPtr<ID3D11Device>        m_device;    // 공장   : 리소스를 만든다
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;   // 리모컨 : 명령을 쏜다
    Microsoft::WRL::ComPtr<IDXGISwapChain>      m_swapChain; // 화면   : Present

    // 패스 2 의 대상 = 실제 화면
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_backBufferRTV;

    // 패스 1 의 대상 = 저해상도 캔버스 텍스처.
    // 같은 텍스처에 두 종류의 뷰를 만든다.
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   m_canvasRTV;   // 여기에 그린다
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_canvasSRV;   // 여기서 읽는다

    // 도형 그리기용 1×1 흰 텍스처
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_whitePixel;

    // DirectXTK 는 COM 이 아니라 평범한 C++ 클래스다. 그래서 unique_ptr.
    std::unique_ptr<DirectX::SpriteBatch>  m_spriteBatch;
    std::unique_ptr<DirectX::CommonStates> m_states;

    int m_windowW = 0;
    int m_windowH = 0;
    int m_canvasW  = 0;
    int m_canvasH  = 0;
};
