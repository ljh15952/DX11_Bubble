#include "Core/Game.h"
#include "Core/Constants.h"
#include "Core/Log.h"

#include <DirectXColors.h>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
    // ---- 스프라이트시트 배치 ----
    constexpr int kCellW = 64;
    constexpr int kCellH = 64;

    // ---- 애니메이션 클립 ----
    //   ticksPerFrame 이 애니메이션 속도. 60 / n = 애니메이션 fps.
    //   나중에 이 값들이 JSON 으로 빠진다.
    constexpr AnimationClip kIdleClip { /*row*/ 0, /*frames*/ 4, /*ticksPerFrame*/ 10, true };  // 6fps
    constexpr AnimationClip kRunClip  { /*row*/ 1, /*frames*/ 4, /*ticksPerFrame*/  4, true };  // 15fps

    // ---- 플레이어 (임시) ----
    constexpr float kPlayerSpeedPerSec  = 300.0f;                      // 초당 300 픽셀
    constexpr float kPlayerSpeedPerTick = kPlayerSpeedPerSec / 60.0f;  // 틱당 5 픽셀

    // 히트박스 오프셋. 64×64 스프라이트 안의 24×40 영역.
    // 나중에 캐릭터 데이터로 빠질 값들이다.
    constexpr float kHitOffsetX = 20.0f;
    constexpr float kHitOffsetY = 12.0f;
    constexpr float kHitWidth   = 24.0f;
    constexpr float kHitHeight  = 40.0f;

    // 아날로그 스틱은 완전히 0 이 되지 않으므로 여유를 둔다.
    constexpr float kMoveEpsilon = 0.01f;
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

    m_sheet = m_renderer.LoadTexture(L"assets/textures/sheet.png");
    if (!m_sheet)
    {
        MessageBoxW(nullptr, L"리소스 로드에 실패했습니다.\n출력 창을 확인하세요.",
                    L"DarkBubble", MB_OK | MB_ICONERROR);
        return false;
    }

    m_input.Initialize();
    m_playerAnim.Play(kIdleClip);

    Log::Info("[game] 초기화 완료");
    Log::Info("[game] 방향키/WASD/스틱 = 이동   Space = 공격   F1 = 히트박스 표시   ESC = 종료");
    return true;
}


void Game::Shutdown()
{
    m_sheet.Reset();
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
            // 로그 앞에 붙는 틱 번호를 갱신한다.
            // 프레임 데이터를 조정할 때 무슨 일이 몇 틱째에 일어났는지 보려면 필요하다.
            Log::SetTick(++m_tickCount);

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
    const bool moving = (std::abs(move.x) > kMoveEpsilon || std::abs(move.y) > kMoveEpsilon);

    // ---- 상태에 맞는 애니메이션 ----
    //   매 틱 Play() 를 불러도 괜찮다. 같은 클립이면 내부에서 무시하므로
    //   프레임이 0 으로 되돌아가지 않는다. (AnimationPlayer::Play 주석 참조)
    m_playerAnim.Play(moving ? kRunClip : kIdleClip);
    m_playerAnim.Tick();

    m_player.x += move.x * kPlayerSpeedPerTick;
    m_player.y += move.y * kPlayerSpeedPerTick;

    // 화면 밖으로 나가지 않게
    m_player.x = std::clamp(m_player.x, 0.0f,
                            static_cast<float>(Config::kClientWidth)  - kCellW);
    m_player.y = std::clamp(m_player.y, 0.0f,
                            static_cast<float>(Config::kClientHeight) - kCellH);

    // ---- 충돌 판정 ----
    //   스프라이트 전체가 아니라 히트박스로 판정한다.
    m_touching = Intersects(PlayerHitbox(), m_obstacle);

    // ---- 엣지 입력: 프레임의 첫 틱에서만 소비 ----
    if (consumeEdgeInput)
    {
        if (m_input.QuitPressed())
            m_window.Close();

        if (m_input.DebugTogglePressed())
        {
            m_showDebug = !m_showDebug;
            Log::Info("[debug] 히트박스 표시 {}", m_showDebug ? "ON" : "OFF");
        }

        if (m_input.AttackPressed())
            Log::Info("[input] 공격 (나중에 여기에 상태머신이 들어간다)");
    }
}


AABB Game::SpriteBounds() const
{
    return AABB::FromXYWH(m_player.x, m_player.y,
                          static_cast<float>(kCellW), static_cast<float>(kCellH));
}


AABB Game::PlayerHitbox() const
{
    return AABB::FromXYWH(m_player.x + kHitOffsetX, m_player.y + kHitOffsetY,
                          kHitWidth, kHitHeight);
}


void Game::Render()
{
    m_renderer.BeginFrame();

    // ---- 장애물 ---- 겹치면 붉게 변한다
    m_renderer.DrawFilledRect(
        m_obstacle,
        m_touching ? DirectX::Colors::Crimson : DirectX::Colors::DimGray);

    // ---- 플레이어 ----
    //   지금 프레임에 해당하는 칸만 잘라 그린다.
    //   이 RECT 가 nullptr 이었던 자리다.
    const RECT src = m_playerAnim.SourceRect(kCellW, kCellH);
    m_renderer.Sprites().Draw(
        m_sheet.Get(), DirectX::XMFLOAT2(m_player.x, m_player.y), &src);

    // ---- 디버그 표시 (F1) ----
    if (m_showDebug)
    {
        // 회색 = 스프라이트 범위(64×64) / 초록 = 실제 히트박스 / 노랑 = 장애물
        m_renderer.DrawRectOutline(SpriteBounds(),  DirectX::Colors::SlateGray);
        m_renderer.DrawRectOutline(PlayerHitbox(),  DirectX::Colors::Lime, 2.0f);
        m_renderer.DrawRectOutline(m_obstacle,      DirectX::Colors::Yellow);
    }

    m_renderer.EndFrame();
}
