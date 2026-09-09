#include "Graphics/Renderer.h"
#include "Core/Log.h"

#include <WICTextureLoader.h>

using Microsoft::WRL::ComPtr;

namespace
{
    // 엔진 기본 UI 폰트. ASCII 32~126, 8×14 셀, 16 열.
    constexpr const wchar_t* kUIFontPath = L"assets/textures/font_8x14.png";
    constexpr int kUIFontCellW   = 8;
    constexpr int kUIFontCellH   = 14;
    constexpr int kUIFontColumns = 16;

    // 뷰포트를 채우는 작은 도우미.
    // MinDepth/MaxDepth 를 0/1 로 넣지 않으면 깊이 범위가 0 이 되어
    // 아무것도 그려지지 않는다. 에러도 안 나므로 찾기가 매우 어렵다.
    D3D11_VIEWPORT MakeViewport(int width, int height)
    {
        D3D11_VIEWPORT vp = {};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width    = static_cast<float>(width);
        vp.Height   = static_cast<float>(height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        return vp;
    }
}


bool Renderer::Initialize(HWND hwnd, int windowWidth, int windowHeight,
                          int canvasWidth, int canvasHeight)
{
    m_windowW = windowWidth;
    m_windowH = windowHeight;
    m_canvasW  = canvasWidth;
    m_canvasH  = canvasHeight;

    // ---- (1) 스왑체인 서술자 ----
    //      {} 초기화 덕분에 안 쓰는 필드(RefreshRate / ScanlineOrdering / Scaling / Flags)는
    //      전부 0 = "알아서 해라" 가 된다. 창 모드에선 어차피 무시되는 값들이다.
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferDesc.Width  = static_cast<UINT>(windowWidth);
    scd.BufferDesc.Height = static_cast<UINT>(windowHeight);
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // RGBA 각 8비트. 셰이더에선 0.0~1.0
    scd.SampleDesc.Count   = 1;                          // MSAA 끔 (도트가 뭉개지므로)
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;  // 이 버퍼의 용도 = 그림 대상
    scd.BufferCount  = 2;                                // 플립 모델은 최소 2 장
    scd.OutputWindow = hwnd;
    scd.Windowed     = TRUE;                             // 개발 중엔 반드시 창 모드
    scd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;    // 복사 없이 버퍼 포인터를 맞바꾼다

    // ---- (2) 원하는 기능 레벨 ----
    //      배열 위에서부터 시도해서 첫 번째로 성공한 것을 쓴다.
    const D3D_FEATURE_LEVEL wanted[] = { D3D_FEATURE_LEVEL_11_0 };

    // ---- (3) 생성 플래그 ----
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;   // 잘못된 호출을 출력 창에 문장으로 알려준다
#endif

    D3D_FEATURE_LEVEL obtained = {};

    // ---- (4) 디바이스 + 컨텍스트 + 스왑체인을 한 번에 만든다 ----
    //      디버그 레이어가 없을 때 재시도해야 하므로 람다로 묶어둔다.
    auto createDevice = [&](UINT createFlags) -> HRESULT
    {
        return D3D11CreateDeviceAndSwapChain(
            nullptr,                    //  1. 어댑터: nullptr = 기본 GPU
            D3D_DRIVER_TYPE_HARDWARE,   //  2. 실제 GPU 사용 (1번이 nullptr 일 때만 HARDWARE 가능)
            nullptr,                    //  3. 소프트웨어 래스터라이저 (안 씀)
            createFlags,                //  4. (3) 의 플래그
            wanted,                     //  5. 원하는 기능 레벨 배열
            ARRAYSIZE(wanted),          //  6. 배열 길이
            D3D11_SDK_VERSION,          //  7. 항상 이 매크로
            &scd,                       //  8. (1) 의 서술자
            &m_swapChain,               //  9. [출력] 스왑체인
            &m_device,                  // 10. [출력] 디바이스
            &obtained,                  // 11. [출력] 실제로 얻은 기능 레벨
            &m_context);                // 12. [출력] 컨텍스트
    };

    HRESULT hr = createDevice(flags);

    // 「グラフィックス ツール」(선택적 기능) 이 안 깔려 있으면 디버그 레이어 생성이 실패한다.
    // 개발 편의를 위해, 그 경우엔 디버그 플래그를 빼고 한 번 더 시도한다.
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING && (flags & D3D11_CREATE_DEVICE_DEBUG))
    {
        Log::Warn("[D3D] 디버그 레이어 없음 -> 플래그를 빼고 재시도. "
                  "「グラフィックス ツール」 설치를 권합니다");
        hr = createDevice(flags & ~D3D11_CREATE_DEVICE_DEBUG);
    }

    if (FAILED(hr))
    {
        Log::Error("[D3D] 디바이스 생성 실패 hr=0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    Log::Info("[D3D] 디바이스 생성 성공 FeatureLevel=0x{:04X}", static_cast<unsigned>(obtained));

    if (!CreateBackBufferTarget())        return false;
    if (!CreateCanvasTarget(m_canvasW, m_canvasH)) return false;

    // ---- 그리기 수단 ----
    //   SpriteBatch 는 Draw 를 모았다가 End() 에서 한꺼번에 GPU 로 보낸다.
    m_spriteBatch = std::make_unique<DirectX::SpriteBatch>(m_context.Get());

    //   CommonStates 는 자주 쓰는 블렌드/샘플러 상태를 미리 만들어 둔 것.
    m_states = std::make_unique<DirectX::CommonStates>(m_device.Get());

    if (!CreateWhitePixel())
        return false;

    // 폰트는 없어도 게임은 돌아간다. 실패해도 죽이지 않고 경고만 남긴다.
    // (BitmapFont::Draw 는 텍스처가 없으면 조용히 아무것도 안 한다)
    if (!m_uiFont.Load(*this, kUIFontPath, kUIFontCellW, kUIFontCellH, kUIFontColumns))
        Log::Warn("[D3D] UI 폰트 로드 실패 — 텍스트가 표시되지 않는다");

    const RECT dst = ComputeCanvasDestRect();
    Log::Info("[D3D] 준비 완료  캔버스 {}x{} -> 화면 {}x{} (표시 영역 {},{} ~ {},{})",
              m_canvasW, m_canvasH, m_windowW, m_windowH,
              dst.left, dst.top, dst.right, dst.bottom);
    return true;
}


// ----------------------------------------------------------------------------
//  OnResize
//    창 크기가 바뀌면 스왑체인 버퍼를 새 크기로 다시 만든다.
//
//    ★ 캔버스 텍스처(640x360)는 손대지 않는다. 백버퍼만 바뀐다.
//      그래서 게임 좌표는 창 크기와 완전히 무관하게 유지된다.
//      이것이 저해상도 렌더 타겟 구조의 가장 큰 실용적 이득이다.
// ----------------------------------------------------------------------------
bool Renderer::OnResize(int windowWidth, int windowHeight)
{
    if (!m_swapChain || windowWidth <= 0 || windowHeight <= 0)
        return true;   // 최소화 등. 에러는 아니다.

    m_windowW = windowWidth;
    m_windowH = windowHeight;

    // ★ ResizeBuffers 전에 백버퍼를 참조하는 것을 전부 놓아야 한다.
    //   참조가 하나라도 남아 있으면 실패한다.
    //
    //   ① 우리가 들고 있는 RTV
    m_backBufferRTV.Reset();

    //   ② 컨텍스트에 묶여 있는 렌더 타겟 바인딩.
    //     이것도 엄연한 참조다. 놓지 않으면
    //     「still bound」 경고가 나거나 ResizeBuffers 가 실패한다.
    ID3D11RenderTargetView* nullTargets[] = { nullptr };
    m_context->OMSetRenderTargets(1, nullTargets, nullptr);

    // 0                    = 버퍼 개수 유지
    // DXGI_FORMAT_UNKNOWN  = 포맷 유지
    HRESULT hr = m_swapChain->ResizeBuffers(
        0,
        static_cast<UINT>(windowWidth),
        static_cast<UINT>(windowHeight),
        DXGI_FORMAT_UNKNOWN,
        0);

    if (FAILED(hr))
    {
        Log::Error("[D3D] ResizeBuffers 실패 hr=0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    // 새 백버퍼에 대한 RTV 를 다시 만든다. 이전 것은 이미 무효다.
    if (!CreateBackBufferTarget())
        return false;

    // 복구됐으므로 「한 번만 경고」 표시를 되돌린다.
    // 안 되돌리면 나중에 또 잃었을 때 아무 말도 남지 않는다.
    m_reportedNoBackBuffer = false;

    const RECT dst = ComputeCanvasDestRect();
    Log::Info("[D3D] 창 {}x{} -> 표시 영역 {},{} ~ {},{}  (배율 x{})",
              m_windowW, m_windowH, dst.left, dst.top, dst.right, dst.bottom,
              (dst.right - dst.left) / m_canvasW);
    return true;
}


void Renderer::Shutdown()
{
    m_spriteBatch.reset();
    m_states.reset();
    m_whitePixel.Reset();
    m_canvasSRV.Reset();
    m_canvasRTV.Reset();
    m_backBufferRTV.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();
}


// ----------------------------------------------------------------------------
//  CreateBackBufferTarget — 패스 2 의 대상 (실제 화면)
// ----------------------------------------------------------------------------
bool Renderer::CreateBackBufferTarget()
{
    // 인덱스 0 만 접근할 수 있다. FLIP 모델에서 버퍼 로테이션은 DXGI 가 알아서 한다.
    // backBuffer 는 지역 변수다. 텍스처의 소유자는 스왑체인이고,
    // 우리는 RTV 를 만들기 위해 잠깐 참조할 뿐이다. 함수를 나가면 자동 Release.
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr))
    {
        Log::Error("[D3D] GetBuffer 실패 hr=0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_backBufferRTV);
    if (FAILED(hr))
    {
        Log::Error("[D3D] 백버퍼 RTV 생성 실패 hr=0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }
    return true;
}


// ----------------------------------------------------------------------------
//  CreateCanvasTarget — 패스 1 의 대상 (저해상도 캔버스 텍스처)
//
//    ★ 여기가 이번 작업의 핵심이다.
//      같은 텍스처에 RTV 와 SRV 를 둘 다 만든다.
//      텍스처는 "무엇" 이고, 뷰는 "어떻게 볼 것인가" 다.
// ----------------------------------------------------------------------------
bool Renderer::CreateCanvasTarget(int width, int height)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = static_cast<UINT>(width);
    desc.Height           = static_cast<UINT>(height);
    desc.MipLevels        = 1;   // 0 으로 두면 밉 체인을 전부 만든다. 2D 엔 불필요.
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;

    // ★ 두 용도를 미리 선언해야 한다. 나중에 추가할 수 없다.
    desc.BindFlags = D3D11_BIND_RENDER_TARGET      // 패스 1: 여기에 그린다
                   | D3D11_BIND_SHADER_RESOURCE;   // 패스 2: 여기서 읽는다

    // 초기 데이터가 없으므로 2번째 인자는 nullptr. 매 프레임 클리어하니 상관없다.
    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &tex);
    if (FAILED(hr))
    {
        Log::Error("[D3D] 캔버스 텍스처 생성 실패 hr=0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    hr = m_device->CreateRenderTargetView(tex.Get(), nullptr, &m_canvasRTV);
    if (FAILED(hr))
    {
        Log::Error("[D3D] 캔버스 RTV 생성 실패 hr=0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    hr = m_device->CreateShaderResourceView(tex.Get(), nullptr, &m_canvasSRV);
    if (FAILED(hr))
    {
        Log::Error("[D3D] 캔버스 SRV 생성 실패 hr=0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }
    return true;
}


// ----------------------------------------------------------------------------
//  CreateWhitePixel
//    1×1 크기의 흰색 텍스처를 코드로 만든다. 파일이 필요 없다.
//    이걸 원하는 크기로 늘려 그리면 채워진 사각형이 되고,
//    가늘게 늘리면 선이 된다.
// ----------------------------------------------------------------------------
bool Renderer::CreateWhitePixel()
{
    // RGBA 각 0xFF = 불투명한 흰색. 픽셀 하나이므로 4 바이트.
    const uint32_t pixel = 0xFFFFFFFFu;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = 1;
    desc.Height           = 1;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_IMMUTABLE;        // 만든 뒤 절대 안 바뀜
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;   // 셰이더가 읽는 용도

    // SysMemPitch = 한 줄의 바이트 수. 1 픽셀 × 4 바이트.
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem     = &pixel;
    init.SysMemPitch = sizeof(uint32_t);

    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = m_device->CreateTexture2D(&desc, &init, &tex);
    if (FAILED(hr))
    {
        Log::Error("[D3D] 1x1 텍스처 생성 실패 hr=0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    hr = m_device->CreateShaderResourceView(tex.Get(), nullptr, &m_whitePixel);
    if (FAILED(hr))
    {
        Log::Error("[D3D] 1x1 SRV 생성 실패 hr=0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }
    return true;
}


// ----------------------------------------------------------------------------
//  ComputeCanvasDestRect
//    캔버스 텍스처를 창 안에 정수배로 확대해 중앙 정렬한다. 남는 공간은 레터박스.
//
//    왜 정수배를 고집하는가:
//      ×1.5 처럼 비정수로 늘리면 원본 1 픽셀이 화면에서 1 픽셀 또는 2 픽셀로
//      들쭉날쭉해진다. 캐릭터가 움직일 때 그 불균일이 위치에 따라 바뀌어
//      픽셀이 지글거린다(pixel shimmer). 도트 게임에서 가장 보기 싫은 현상이다.
// ----------------------------------------------------------------------------
RECT Renderer::ComputeCanvasDestRect() const
{
    const int scaleX = m_windowW / m_canvasW;
    const int scaleY = m_windowH / m_canvasH;
    int scale = (scaleX < scaleY) ? scaleX : scaleY;
    if (scale < 1)
        scale = 1;   // 창이 캔버스보다 작으면 어쩔 수 없이 잘린다

    const int w = m_canvasW * scale;
    const int h = m_canvasH * scale;
    const int x = (m_windowW - w) / 2;
    const int y = (m_windowH - h) / 2;

    return { static_cast<LONG>(x), static_cast<LONG>(y),
             static_cast<LONG>(x + w), static_cast<LONG>(y + h) };
}


ComPtr<ID3D11ShaderResourceView> Renderer::LoadTexture(const wchar_t* path)
{
    ComPtr<ID3D11ShaderResourceView> srv;

    // 3번째 인자(ID3D11Resource**) 는 텍스처 원본. 크기를 잴 때만 필요하므로 nullptr.
    // 경로는 작업 디렉터리 기준. 프로젝트 설정에서 $(SolutionDir) 로 잡아뒀다.
    const HRESULT hr = DirectX::CreateWICTextureFromFile(
        m_device.Get(), path, nullptr, srv.GetAddressOf());

    if (FAILED(hr))
    {
        Log::Error("[RES] 텍스처 로드 실패 hr=0x{:08X} : {}",
                   static_cast<unsigned>(hr), Log::ToUtf8(path));
        Log::Error("[RES] 디버깅 작업 디렉터리가 $(SolutionDir) 인지 확인하세요");
        return nullptr;
    }

    Log::Info("[RES] 텍스처 로드 : {}", Log::ToUtf8(path));
    return srv;
}


// ----------------------------------------------------------------------------
//  BeginFrame — 패스 1 시작. 게임은 저해상도 캔버스 텍스처에 그려진다.
//               월드 레이어이므로 뷰 행렬(카메라 + 흔들림)이 적용된다.
// ----------------------------------------------------------------------------
void Renderer::BeginFrame(const DirectX::XMMATRIX& viewMatrix)
{
    // 캔버스 텍스처를 그림 대상으로 묶는다. 3번째 인자는 깊이/스텐실 뷰. 2D 라서 없다.
    // ※ &m_canvasRTV 가 아니라 GetAddressOf() 인 것에 주의.
    //   &ComPtr 은 ReleaseAndGetAddressOf() 라서 먼저 Release 해버린다.
    m_context->OMSetRenderTargets(1, m_canvasRTV.GetAddressOf(), nullptr);

    // 뷰포트도 캔버스 크기로 바꾼다.
    // 이걸 빼먹으면 창 크기 뷰포트가 남아 있어서 텍스처의 좌상단 1/4 에만 그려진다.
    const D3D11_VIEWPORT vp = MakeViewport(m_canvasW, m_canvasH);
    m_context->RSSetViewports(1, &vp);

    // SpriteBatch 에게도 명시적으로 알려준다.
    // 알려주지 않으면 Flush 시점의 컨텍스트 뷰포트를 읽어 가는데,
    // 패스가 두 개라 어느 쪽인지 헷갈릴 여지가 생긴다.
    m_spriteBatch->SetViewport(vp);

    const float clearColor[4] = { 0.10f, 0.10f, 0.12f, 1.0f };   // 어두운 청회색
    m_context->ClearRenderTargetView(m_canvasRTV.Get(), clearColor);

    //   NonPremultiplied : WIC 로 읽은 PNG 는 알파가 곱해지지 않은(straight) 상태다.
    //                      기본값(premultiplied)으로 두면 가장자리에 검은 테두리가 생긴다.
    //   PointClamp       : 점 샘플링. 기본값인 선형 보간을 쓰면 도트가 흐려진다.
    //
    //   마지막 인자가 뷰 행렬이다. 이 배치의 모든 스프라이트에 적용된다.
    //   중간 인자들은 기본값(nullptr)을 그대로 쓴다.
    m_spriteBatch->Begin(
        DirectX::SpriteSortMode_Deferred,
        m_states->NonPremultiplied(),
        m_states->PointClamp(),
        nullptr,          // depthStencilState
        nullptr,          // rasterizerState
        nullptr,          // setCustomShaders
        viewMatrix);      // ★ 카메라 + 흔들림
}


// ----------------------------------------------------------------------------
//  BeginUILayer — 월드 배치를 닫고 UI 배치를 연다.
//                 행렬을 넘기지 않으므로 항등 행렬 = 카메라 무시.
// ----------------------------------------------------------------------------
void Renderer::BeginUILayer()
{
    m_spriteBatch->End();     // 월드 레이어의 Draw 들이 여기서 GPU 로 나간다

    m_spriteBatch->Begin(
        DirectX::SpriteSortMode_Deferred,
        m_states->NonPremultiplied(),
        m_states->PointClamp());
}


// ----------------------------------------------------------------------------
//  EndFrame — 패스 1 종료 후 패스 2 실행
// ----------------------------------------------------------------------------
void Renderer::EndFrame()
{
    m_spriteBatch->End();   // UI 레이어의 Draw 들이 여기서 GPU 로 나간다

    // OnResize 가 실패하면 백버퍼 RTV 가 없는 상태가 된다.
    // 그대로 진행하면 ClearRenderTargetView(nullptr) 로 죽는다.
    if (!m_backBufferRTV)
    {
        // ★ 한 번만 찍는다. 매 프레임 찍으면 초당 60줄이 쌓여
        //   정작 원인이 된 앞쪽 로그가 스크롤 밖으로 밀려난다.
        if (!m_reportedNoBackBuffer)
        {
            m_reportedNoBackBuffer = true;
            Log::Error("[D3D] 백버퍼 RTV 가 없다 — 이후 프레임은 표시하지 않는다 "
                       "(이 경고는 한 번만 표시된다)");
        }
        return;
    }

    // ---- 패스 2: 캔버스 텍스처를 화면에 확대해서 그린다 ----
    //
    //  ★ 순서가 중요하다.
    //    먼저 렌더 타겟을 백버퍼로 바꿔야 캔버스 텍스처의 RTV 바인딩이 풀린다.
    //    같은 텍스처를 RTV 와 SRV 로 동시에 묶을 수는 없어서,
    //    순서를 반대로 하면 D3D 가 강제로 하나를 풀고 디버그 레이어에 경고를 낸다.
    m_context->OMSetRenderTargets(1, m_backBufferRTV.GetAddressOf(), nullptr);

    const D3D11_VIEWPORT vp = MakeViewport(m_windowW, m_windowH);
    m_context->RSSetViewports(1, &vp);
    m_spriteBatch->SetViewport(vp);

    // 레터박스(남는 여백)는 검은색
    const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->ClearRenderTargetView(m_backBufferRTV.Get(), black);

    //   Opaque     : 화면을 꽉 채우는 불투명 한 장이라 알파 블렌딩이 필요 없다
    //   PointClamp : ★ 확대가 실제로 일어나는 곳. 선형 보간을 쓰면 전부 흐려진다
    m_spriteBatch->Begin(
        DirectX::SpriteSortMode_Deferred,
        m_states->Opaque(),
        m_states->PointClamp());

    m_spriteBatch->Draw(m_canvasSRV.Get(), ComputeCanvasDestRect());

    m_spriteBatch->End();

    // ★ 캔버스 텍스처를 픽셀 셰이더 입력에서 풀어 준다.
    //
    //   SpriteBatch::End() 는 그리기만 끝낼 뿐 바인딩을 해제하지 않는다.
    //   그대로 두면 캔버스가 PS 입력에 묶인 채 프레임이 끝나고,
    //   다음 프레임의 BeginFrame 이 같은 텍스처를 렌더 타겟으로 삼으려다
    //   "입력과 출력에 동시에 묶였다" 는 해저드 경고를 낸다.
    //
    //     D3D11 WARNING: ... is still bound on input! [DEVICE_OMSETRENDERTARGETS_HAZARD]
    //     D3D11 WARNING: Forcing PS shader resource slot 0 to NULL.
    //
    //   D3D 가 알아서 풀어 주므로 화면은 정상이지만, 매 프레임 경고가 쌓여
    //   진짜 경고가 묻힌다. 같은 리소스를 입력과 출력으로 동시에 쓸 수 없다는
    //   규칙은 렌더 타겟을 텍스처로 재활용하는 구조에서 항상 따라다닌다.
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    m_context->PSSetShaderResources(0, 1, nullSRV);

    // SyncInterval: 0 = 즉시(테어링 발생, fps 무제한)
    //               1 = 다음 수직 동기까지 대기 = 60fps 고정
    //               2 = 두 번째 동기까지 = 30fps
    m_swapChain->Present(1, 0);
}


void Renderer::DrawFilledRect(const AABB& box, DirectX::FXMVECTOR color)
{
    // 목적지 사각형을 넘기는 Draw 오버로드는 텍스처를 그 크기로 늘려 준다.
    const RECT dst = {
        static_cast<LONG>(box.left),
        static_cast<LONG>(box.top),
        static_cast<LONG>(box.right),
        static_cast<LONG>(box.bottom)
    };
    m_spriteBatch->Draw(m_whitePixel.Get(), dst, color);
}


void Renderer::DrawString(std::string_view text, float x, float y,
                          DirectX::FXMVECTOR color, int scale)
{
    m_uiFont.Draw(*this, text, x, y, color, scale);
}


void Renderer::DrawStringCentered(std::string_view text, float centerX, float y,
                                  DirectX::FXMVECTOR color, int scale)
{
    m_uiFont.DrawCentered(*this, text, centerX, y, color, scale);
}


float Renderer::MeasureString(std::string_view text, int scale) const
{
    return m_uiFont.MeasureWidth(text, scale);
}


float Renderer::MeasureStringHeight(std::string_view text, int scale) const
{
    return m_uiFont.MeasureHeight(text, scale);
}


void Renderer::DrawRectOutline(const AABB& box, DirectX::FXMVECTOR color, float thickness)
{
    const float t = thickness;
    DrawFilledRect({ box.left,      box.top,        box.right,     box.top + t }, color);   // 위
    DrawFilledRect({ box.left,      box.bottom - t, box.right,     box.bottom  }, color);   // 아래
    DrawFilledRect({ box.left,      box.top,        box.left + t,  box.bottom  }, color);   // 왼쪽
    DrawFilledRect({ box.right - t, box.top,        box.right,     box.bottom  }, color);   // 오른쪽
}
