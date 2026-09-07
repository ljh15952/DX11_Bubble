#include "Core/Game.h"
#include "Core/Constants.h"

#include <DirectXColors.h>
#include <algorithm>
#include <chrono>
#include <iostream>

namespace
{
    // ---- 플레이어 (임시) ----
    constexpr float kSpriteSize         = 64.0f;
    constexpr float kPlayerSpeedPerSec  = 300.0f;                        // 초당 300 픽셀
    constexpr float kPlayerSpeedPerTick = kPlayerSpeedPerSec / 60.0f;    // 틱당 5 픽셀
}


bool Game::Initialize(HINSTANCE hInstance, int nCmdShow)
{
    if (!m_window.Create(hInstance, nCmdShow,
                         Config::kClientWidth, Config::kClientHeight, L"DarkBubble"))
    {
        MessageBoxW(nullptr, L"창 생성에 실패했습니다.", L"DarkBubble", MB_OK | MB_ICONERROR);
        return false;
    }

    if (!m_renderer.Initialize(m_window.Handle(),
                               m_window.ClientWidth(), m_window.ClientHeight()))
    {
        MessageBoxW(nullptr, L"D3D11 초기화에 실패했습니다.", L"DarkBubble", MB_OK | MB_ICONERROR);
        return false;
    }

    m_testTexture = m_renderer.LoadTexture(L"assets/textures/test.png");
    if (!m_testTexture)
    {
        MessageBoxW(nullptr, L"리소스 로드에 실패했습니다.\n출력 창을 확인하세요.",
                    L"DarkBubble", MB_OK | MB_ICONERROR);
        return false;
    }

    m_input.Initialize();

    std::cout << "[game] 초기화 완료\n";
    return true;
}


void Game::Shutdown()
{
    m_testTexture.Reset();
    m_renderer.Shutdown();
}


// ----------------------------------------------------------------------------
//  Run — 고정 타임스텝 게임 루프
// ----------------------------------------------------------------------------
int Game::Run()
{
    using Clock = std::chrono::steady_clock;

    auto   previous    = Clock::now();
    double accumulator = 0.0;   // 아직 처리하지 못한 시간을 저금해 두는 통장

    for (;;)
    {
        // ① 쌓여 있는 메시지를 전부 처리한다. false 면 WM_QUIT.
        if (!m_window.PumpMessages())
            break;

        // ② 지난 프레임이 실제로 얼마나 걸렸는지 잰다
        const auto now = Clock::now();
        double frameTime = std::chrono::duration<double>(now - previous).count();
        previous = now;

        // 죽음의 나선 방지. 브레이크포인트에 10 초 멈춰 있었다면
        // 이 상한이 없을 경우 Update 를 600 번 부르려다 얼어붙는다.
        if (frameTime > Config::kMaxFrameSeconds)
            frameTime = Config::kMaxFrameSeconds;

        accumulator += frameTime;

        // ③ 입력 폴링은 "프레임당 1회". Update 안에서 하면 안 된다.
        m_input.Poll();

        // ④ 통장이 찰 때마다 정확히 1 틱씩 처리한다.
        //    남은 잔액은 버리지 않고 다음 프레임으로 이월된다.
        bool firstTickThisFrame = true;
        while (accumulator >= Config::kTickSeconds)
        {
            Update(firstTickThisFrame);
            firstTickThisFrame = false;
            accumulator -= Config::kTickSeconds;
        }

        // ⑤ 그리기는 프레임당 1회
        Render();
    }

    return m_window.ExitCode();
}


void Game::Update(bool consumeEdgeInput)
{
    // ---- 지속 입력: 이동 ----
    const Input::MoveIntent move = m_input.Move();

    m_player.x += move.x * kPlayerSpeedPerTick;
    m_player.y += move.y * kPlayerSpeedPerTick;

    // 화면 밖으로 나가지 않게
    m_player.x = std::clamp(m_player.x, 0.0f,
                            static_cast<float>(Config::kClientWidth)  - kSpriteSize);
    m_player.y = std::clamp(m_player.y, 0.0f,
                            static_cast<float>(Config::kClientHeight) - kSpriteSize);

    // ---- 엣지 입력: 프레임의 첫 틱에서만 소비 ----
    if (consumeEdgeInput)
    {
        if (m_input.QuitPressed())
            m_window.Close();

        if (m_input.AttackPressed())
            std::cout << "[input] 공격 (나중에 여기에 상태머신이 들어간다)\n";
    }
}


void Game::Render()
{
    m_renderer.BeginFrame();

    // 움직이지 않는 기준점 — 4 배 확대 (점 샘플링 확인용)
    m_renderer.Sprites().Draw(
        m_testTexture.Get(), DirectX::XMFLOAT2(900.0f, 100.0f),
        nullptr, DirectX::Colors::White, 0.0f,
        DirectX::XMFLOAT2(0.0f, 0.0f), 4.0f);

    // 플레이어 — 방향키 / 게임패드 스틱으로 움직인다
    m_renderer.Sprites().Draw(
        m_testTexture.Get(), DirectX::XMFLOAT2(m_player.x, m_player.y));

    m_renderer.EndFrame();
}
