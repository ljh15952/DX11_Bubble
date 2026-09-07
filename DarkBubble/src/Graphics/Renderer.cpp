#include "Graphics/Renderer.h"

#include <WICTextureLoader.h>
#include <format>
#include <iostream>

using Microsoft::WRL::ComPtr;


bool Renderer::Initialize(HWND hwnd, int width, int height)
{
    // ---- (1) 스왑체인 서술자 ----
    //      {} 초기화 덕분에 안 쓰는 필드(RefreshRate / ScanlineOrdering / Scaling / Flags)는
    //      전부 0 = "알아서 해라" 가 된다. 창 모드에선 어차피 무시되는 값들이다.
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferDesc.Width  = static_cast<UINT>(width);
    scd.BufferDesc.Height = static_cast<UINT>(height);
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
        OutputDebugStringW(L"[D3D] 디버그 레이어 없음 -> 플래그를 빼고 재시도\n");
        hr = createDevice(flags & ~D3D11_CREATE_DEVICE_DEBUG);
    }

    if (FAILED(hr))
    {
        OutputDebugStringW(
            std::format(L"[D3D] 생성 실패 hr=0x{:08X}\n", static_cast<unsigned>(hr)).c_str());
        return false;
    }

    OutputDebugStringW(
        std::format(L"[D3D] 초기화 성공 FeatureLevel=0x{:04X}\n",
                    static_cast<unsigned>(obtained)).c_str());

    // ---- (5) 스왑체인에서 백버퍼(텍스처)를 꺼낸다 ----
    //      인덱스 0 만 접근할 수 있다. FLIP 모델에서 버퍼 로테이션은 DXGI 가 알아서 한다.
    //      backBuffer 는 지역 변수다. 텍스처의 소유자는 스왑체인이고,
    //      우리는 RTV 를 만들기 위해 잠깐 참조할 뿐이다. 함수를 나가면 자동 Release.
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr))
    {
        OutputDebugStringW(
            std::format(L"[D3D] GetBuffer 실패 hr=0x{:08X}\n", static_cast<unsigned>(hr)).c_str());
        return false;
    }

    // ---- (6) 그 텍스처를 "그림 대상"으로 해석하는 뷰를 만든다 ----
    //      2번째 인자 nullptr = "텍스처의 포맷을 그대로 써라".
    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv);
    if (FAILED(hr))
    {
        OutputDebugStringW(
            std::format(L"[D3D] RTV 생성 실패 hr=0x{:08X}\n", static_cast<unsigned>(hr)).c_str());
        return false;
    }

    // ---- (7) 뷰포트: 셰이더의 -1~+1 좌표를 실제 픽셀로 바꾸는 변환 규칙 ----
    //      MinDepth/MaxDepth 를 0/1 로 넣지 않으면 깊이 범위가 0 이 되어
    //      아무것도 그려지지 않는다. 에러도 안 나므로 찾기가 매우 어렵다.
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width    = static_cast<float>(width);
    vp.Height   = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    // ---- (8) 그리기 수단 ----
    //      SpriteBatch 는 Draw 를 모았다가 End() 에서 한꺼번에 GPU 로 보낸다.
    //      스프라이트를 1000 장 그려도 드로우 콜은 몇 번으로 줄어든다.
    m_spriteBatch = std::make_unique<DirectX::SpriteBatch>(m_context.Get());

    //      CommonStates 는 자주 쓰는 블렌드/샘플러 상태를 미리 만들어 둔 것.
    //      직접 만들면 서술자를 몇십 줄 채워야 한다.
    m_states = std::make_unique<DirectX::CommonStates>(m_device.Get());

    if (!CreateWhitePixel())
        return false;

    OutputDebugStringW(L"[D3D] RTV / 뷰포트 / SpriteBatch 준비 완료\n");
    return true;
}


void Renderer::Shutdown()
{
    m_spriteBatch.reset();
    m_states.reset();
    m_whitePixel.Reset();
    m_rtv.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();
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

    // 텍스처를 만들 때 초기 데이터를 같이 넘긴다.
    // SysMemPitch = 한 줄의 바이트 수. 1 픽셀 × 4 바이트.
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem     = &pixel;
    init.SysMemPitch = sizeof(uint32_t);

    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = m_device->CreateTexture2D(&desc, &init, &tex);
    if (FAILED(hr))
    {
        OutputDebugStringW(
            std::format(L"[D3D] 1x1 텍스처 생성 실패 hr=0x{:08X}\n",
                        static_cast<unsigned>(hr)).c_str());
        return false;
    }

    // 텍스처(메모리) 를 "셰이더 입력" 으로 해석하는 뷰를 만든다. RTV 와 같은 구조.
    hr = m_device->CreateShaderResourceView(tex.Get(), nullptr, &m_whitePixel);
    if (FAILED(hr))
    {
        OutputDebugStringW(
            std::format(L"[D3D] 1x1 SRV 생성 실패 hr=0x{:08X}\n",
                        static_cast<unsigned>(hr)).c_str());
        return false;
    }
    return true;
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


void Renderer::DrawRectOutline(const AABB& box, DirectX::FXMVECTOR color, float thickness)
{
    const float t = thickness;
    DrawFilledRect({ box.left,      box.top,        box.right,     box.top + t }, color);      // 위
    DrawFilledRect({ box.left,      box.bottom - t, box.right,     box.bottom  }, color);      // 아래
    DrawFilledRect({ box.left,      box.top,        box.left + t,  box.bottom  }, color);      // 왼쪽
    DrawFilledRect({ box.right - t, box.top,        box.right,     box.bottom  }, color);      // 오른쪽
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
        OutputDebugStringW(
            std::format(L"[RES] 텍스처 로드 실패 hr=0x{:08X} : {}\n",
                        static_cast<unsigned>(hr), path).c_str());
        std::wcout << L"[RES] " << path << L" 를 못 찾았습니다.\n"
                   << L"      디버깅 작업 디렉터리가 $(SolutionDir) 인지 확인하세요.\n";
        return nullptr;
    }
    return srv;
}


void Renderer::BeginFrame()
{
    // ---- 렌더 타겟을 파이프라인에 묶는다 ----
    //      FLIP 모델은 Present() 때마다 이 바인딩이 풀린다. 그래서 매 프레임 다시 묶는다.
    //      OM = Output Merger, 파이프라인의 마지막 단계.
    //      ※ &m_rtv 가 아니라 GetAddressOf() 인 것에 주의.
    //        &ComPtr 은 ReleaseAndGetAddressOf() 라서 먼저 Release 해버린다.
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);

    // ---- 화면을 지운다 ---- (RGBA 각 0.0~1.0. 어두운 청회색)
    const float clearColor[4] = { 0.10f, 0.10f, 0.12f, 1.0f };
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);

    // ---- 스프라이트 배치 시작 ----
    //      NonPremultiplied : WIC 로 읽은 PNG 는 알파가 곱해지지 않은(straight) 상태다.
    //                         기본값(premultiplied)으로 두면 가장자리에 검은 테두리가 생긴다.
    //      PointClamp       : 점 샘플링. 기본값인 선형 보간을 쓰면 도트가 흐려진다.
    m_spriteBatch->Begin(
        DirectX::SpriteSortMode_Deferred,
        m_states->NonPremultiplied(),
        m_states->PointClamp());
}


void Renderer::EndFrame()
{
    m_spriteBatch->End();

    // SyncInterval: 0 = 즉시(테어링 발생, fps 무제한)
    //               1 = 다음 수직 동기까지 대기 = 60fps 고정
    //               2 = 두 번째 동기까지 = 30fps
    m_swapChain->Present(1, 0);
}
