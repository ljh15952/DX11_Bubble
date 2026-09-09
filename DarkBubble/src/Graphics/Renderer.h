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
#include <string_view>

#include <SpriteBatch.h>
#include <CommonStates.h>
#include <DirectXColors.h>

#include "Core/AABB.h"
#include "Graphics/BitmapFont.h"

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

    // ---- 한 프레임의 흐름 ----
    //   BeginFrame(view)  : 캔버스 바인딩 · 클리어 · **월드 레이어** 배치 시작
    //                       view 행렬이 적용되므로 카메라·흔들림이 여기에만 걸린다
    //   BeginUILayer()    : 월드 배치를 닫고 **UI 레이어** 배치 시작 (항등 행렬)
    //   EndFrame()        : UI 배치를 닫고 패스 2(확대) 실행 후 Present
    //
    //   ★ 레이어를 나누는 이유:
    //     체력 바나 대사 상자가 화면 흔들림을 같이 타면 고장난 것처럼 보인다.
    //     카메라가 있는 게임은 예외 없이 이 구조를 가진다.
    void BeginFrame(const DirectX::XMMATRIX& viewMatrix);
    void BeginUILayer();
    void EndFrame();

    DirectX::SpriteBatch& Sprites() { return *m_spriteBatch; }

    // ---- 도형 그리기 ----
    //   1×1 흰 텍스처를 목표 사각형 크기로 늘려서 그린다.
    //   SpriteBatch 만으로 선과 상자를 그리는 표준 트릭이다.
    //   전용 셰이더나 정점 버퍼를 만들 필요가 없다.
    void DrawFilledRect(const AABB& box, DirectX::FXMVECTOR color);
    void DrawRectOutline(const AABB& box, DirectX::FXMVECTOR color, float thickness = 1.0f);

    // ---- 텍스트 ----
    //   폰트를 Renderer 가 들고 있는 이유:
    //   Scene::Render 는 Renderer 만 받으므로(의도적인 설계),
    //   폰트가 SceneContext 쪽에 있으면 그리기 중에 쓸 수가 없다.
    //   텍스트 그리기는 엄연히 렌더링 서비스이므로 여기 두는 것이 자연스럽다.
    //
    //   ※ 이름이 DrawText 가 아닌 이유: windows.h 가 DrawText 를 매크로로 정의해서
    //     DrawTextW 로 치환되어 버린다. Win32 와 섞어 쓸 때 흔한 함정이다.
    void  DrawString(std::string_view text, float x, float y,
                     DirectX::FXMVECTOR color = DirectX::Colors::White, int scale = 1);
    void  DrawStringCentered(std::string_view text, float centerX, float y,
                             DirectX::FXMVECTOR color = DirectX::Colors::White, int scale = 1);

    // ★ 폭과 높이가 짝으로 있어야 한다.
    //   높이가 없어서 「줄 수 × 셀 높이」를 손으로 적는 코드가 생기고,
    //   서식 문자열에 줄을 하나 추가하면 배경판만 조용히 어긋난다.
    float MeasureString(std::string_view text, int scale = 1) const;
    float MeasureStringHeight(std::string_view text, int scale = 1) const;

    const BitmapFont& Font() const { return m_uiFont; }

    // ---- 디버그 그리기 (F1) ----
    //   ★ 이 플래그가 Scene 이 아니라 여기 있는 이유:
    //
    //     Scene 에 두면 토글을 Scene::Update(= 틱) 안에서 읽어야 하는데,
    //     엣지 입력은 틱이 0회 도는 프레임에서 사라진다.
    //     그래서 프레임 정지( , ) 중에는 F1 이 **아예 동작하지 않았다** —
    //     멈춘 화면에서 히트박스를 보는 것이 F1 의 주 용도인데도.
    //
    //     반대로 이것은 「표시 설정」이므로 틱과 아무 관계가 없다.
    //     Game 이 F3 처럼 프레임당 1회 토글하면 정지 중에도 즉시 반응한다.
    //
    //     Renderer 가 가진 것이 자연스러운 이유:
    //       ① Render / RenderUI 양쪽에서 접근 가능한 유일한 객체다
    //          (Scene::Render 는 의도적으로 Renderer 만 받는다)
    //       ② 디버그 그리기 수단(DrawRectOutline 등)을 이미 소유하고 있다
    bool DebugDraw() const      { return m_debugDraw; }
    void SetDebugDraw(bool on)  { m_debugDraw = on; }

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

    // 엔진 기본 UI 폰트
    BitmapFont m_uiFont;

    int m_windowW = 0;
    int m_windowH = 0;
    int m_canvasW  = 0;
    int m_canvasH  = 0;

    bool m_debugDraw = false;   // F1. Game 이 토글하고 Scene 이 읽는다

    // 백버퍼 RTV 분실 경고를 한 번만 찍기 위한 표시.
    // 매 프레임 찍으면 초당 60줄이 쌓여 진짜 로그가 묻힌다.
    bool m_reportedNoBackBuffer = false;
};
