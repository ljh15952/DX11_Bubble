#include "Core/Game.h"
#include "Core/Constants.h"
#include "Core/Log.h"
#include "Scenes/TitleScene.h"

#include <DirectXColors.h>
#include <chrono>
#include <format>
#include <memory>


bool Game::Initialize(HINSTANCE hInstance, int nCmdShow)
{
    if (!m_window.Create(hInstance, nCmdShow,
                         Config::kClientWidth, Config::kClientHeight, L"DarkBubble"))
    {
        MessageBoxW(nullptr, L"창 생성에 실패했습니다.", L"DarkBubble", MB_OK | MB_ICONERROR);
        return false;
    }

    if (!m_renderer.Initialize(m_window.Handle(),
                               m_window.ClientWidth(), m_window.ClientHeight(),
                               Config::kCanvasWidth, Config::kCanvasHeight))
    {
        MessageBoxW(nullptr, L"D3D11 초기화에 실패했습니다.", L"DarkBubble", MB_OK | MB_ICONERROR);
        return false;
    }

    m_input.Initialize();
    m_assets.Initialize(m_renderer);
    m_audio.Initialize();

    // 공용 효과음. 지금은 몇 개뿐이라 여기서 다 읽는다.
    // 종류가 늘어나면 Scene 마다 Enter 에서 필요한 것만 읽는 방식으로 옮기면 된다.
    m_audio.Load("ui_confirm", L"assets/sounds/ui_confirm.wav");
    m_audio.Load("ui_cancel",  L"assets/sounds/ui_cancel.wav");
    m_audio.Load("hit",        L"assets/sounds/hit.wav");
    m_audio.Load("step",       L"assets/sounds/step.wav");

    Log::Info("[game] 초기화 완료");
    return true;
}


// ----------------------------------------------------------------------------
//  Shutdown
//    ★ 해제 순서가 중요하다. 텍스처를 참조하는 쪽부터 놓아야 한다.
//
//      Scene (텍스처 참조)  →  Assets (캐시)  →  Renderer (D3D 디바이스)
//
//    거꾸로 하면 디바이스가 사라진 뒤에 텍스처를 해제하게 된다.
// ----------------------------------------------------------------------------
void Game::Shutdown()
{
    SceneContext ctx{ m_renderer, m_input, m_scenes, m_assets, m_audio };
    m_scenes.Clear();
    m_scenes.ApplyPending(ctx);

    m_assets.Clear();
    m_audio.Shutdown();
    m_renderer.Shutdown();
}


// ----------------------------------------------------------------------------
//  Run — 고정 타임스텝 게임 루프
//
//    Game 은 Scene 이 무엇을 하는지 모른다. 순서만 지킨다.
// ----------------------------------------------------------------------------
int Game::Run()
{
    using Clock = std::chrono::steady_clock;

    // Scene 이 일할 때 필요한 것들. 참조만 담으므로 한 번 만들어 계속 쓴다.
    SceneContext ctx{ m_renderer, m_input, m_scenes, m_assets, m_audio };

    // 첫 Scene 을 올린다. 요청은 지연되므로 여기서 한 번 적용해 준다.
    m_scenes.Replace(std::make_unique<TitleScene>());
    m_scenes.ApplyPending(ctx);
    if (m_scenes.Empty())
    {
        Log::Error("[game] 첫 Scene 진입 실패");
        return 1;
    }

    auto   previous    = Clock::now();
    double accumulator = 0.0;   // 아직 처리하지 못한 시간을 저금해 두는 통장

    for (;;)
    {
        // ① 쌓여 있는 메시지를 전부 처리한다. false 면 WM_QUIT.
        if (!m_window.PumpMessages())
            break;

        // ①-b 창 크기가 바뀌었으면 스왑체인을 다시 만든다.
        int newW = 0, newH = 0;
        if (m_window.ConsumeResize(newW, newH))
            m_renderer.OnResize(newW, newH);

        // ② 지난 프레임이 실제로 얼마나 걸렸는지 잰다
        const auto now = Clock::now();
        double frameTime = std::chrono::duration<double>(now - previous).count();
        previous = now;

        // 죽음의 나선 방지. 브레이크포인트에 10 초 멈춰 있었다면
        // 이 상한이 없을 경우 Update 를 600 번 부르려다 얼어붙는다.
        if (frameTime > Config::kMaxFrameSeconds)
            frameTime = Config::kMaxFrameSeconds;

        accumulator += frameTime;

        // FPS 측정. 1 초에 한 번만 갱신해야 숫자가 안 튀어서 읽을 수 있다.
        m_lastFrameMs = frameTime * 1000.0;
        m_fpsAccum += frameTime;
        ++m_fpsFrames;
        if (m_fpsAccum >= 1.0)
        {
            m_fps       = m_fpsFrames / m_fpsAccum;
            m_fpsFrames = 0;
            m_fpsAccum  = 0.0;
        }

        // ③ 입력 폴링은 "프레임당 1회". Update 안에서 하면 안 된다.
        m_input.Poll();

        // 오디오도 프레임당 1회. 끝난 소리를 정리하고 장치 분실을 복구한다.
        // 틱 루프 안이 아니라 여기인 이유: 소리는 XAudio2 가 자기 스레드에서
        // 흘려보내므로 게임 틱과 보조를 맞출 필요가 없다.
        m_audio.Update();

        // 오버레이 토글은 Scene 과 무관한 엔진 기능이라 여기서 처리한다.
        // Poll 직후이므로 "방금 눌림" 이 정확히 한 번만 잡힌다.
        if (m_input.StatsTogglePressed())
        {
            m_showStats = !m_showStats;
            Log::Info("[game] 통계 오버레이 {}", m_showStats ? "ON" : "OFF");
        }

        // ④ 통장이 찰 때마다 정확히 1 틱씩 처리한다.
        bool firstTickThisFrame = true;
        while (accumulator >= Config::kTickSeconds)
        {
            Log::SetTick(++m_tickCount);
            m_scenes.UpdateStack(ctx, firstTickThisFrame);
            firstTickThisFrame = false;
            accumulator -= Config::kTickSeconds;
        }

        // ⑤ ★ Scene 전환은 틱 루프가 전부 끝난 뒤에 적용한다.
        //    Scene 이 자기 Update 안에서 자기를 교체해도 안전한 이유가 이것이다.
        m_scenes.ApplyPending(ctx);

        // 스택이 비면 게임을 끝낸다. (TitleScene 의 Esc 등)
        if (m_scenes.Empty())
        {
            m_window.Close();
            continue;   // 다음 반복에서 WM_QUIT 를 받아 빠져나간다
        }

        // ⑥ 그리기는 프레임당 1회
        m_renderer.BeginFrame();
        m_scenes.RenderStack(m_renderer);
        if (m_showStats)
            DrawStatsOverlay();   // Scene 위에 항상 덮어 그린다
        m_renderer.EndFrame();
    }

    return m_window.ExitCode();
}


// ----------------------------------------------------------------------------
//  DrawStatsOverlay
//    5단계에서 스태미나와 프레임 데이터를 조정할 때, 화면에 숫자가 실시간으로
//    보이는 것과 로그를 뒤지는 것은 작업 속도가 몇 배 차이난다.
// ----------------------------------------------------------------------------
void Game::DrawStatsOverlay()
{
    const std::string text = std::format(
        "FPS {:5.1f}  FRAME {:5.2f}ms\n"
        "TICK {}\n"
        "SCENE {} (depth {})\n"
        "TEX {}  load {} / hit {}",
        m_fps, m_lastFrameMs,
        m_tickCount,
        m_scenes.TopName(), m_scenes.Depth(),
        m_assets.Count(), m_assets.LoadCount(), m_assets.HitCount());

    // 글자가 배경에 묻히지 않게 반투명 판을 먼저 깐다
    const float w = m_renderer.MeasureString(text, 1);
    const float h = static_cast<float>(m_renderer.Font().CellHeight() * 4);
    m_renderer.DrawFilledRect(
        AABB::FromXYWH(2.0f, 2.0f, w + 8.0f, h + 6.0f),
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.55f));

    m_renderer.DrawString(text, 6.0f, 5.0f, DirectX::Colors::Lime, 1);
}
