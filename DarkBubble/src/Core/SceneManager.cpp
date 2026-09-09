#include "Core/SceneManager.h"
#include "Core/Log.h"

namespace
{
    const char* OpName(int op)
    {
        switch (op)
        {
        case 1:  return "Replace";
        case 2:  return "Push";
        case 3:  return "Pop";
        case 4:  return "Clear";
        default: return "None";
        }
    }
}


void SceneManager::SetPending(Op op, std::unique_ptr<Scene> scene)
{
    // 한 프레임에 두 번 요청되면 나중 것만 남는다.
    // 조용히 덮어쓰면 원인을 못 찾으니 경고를 남긴다.
    if (m_pending.op != Op::None)
    {
        Log::Warn("[scene] 한 프레임에 전환이 두 번 요청됨 ({} -> {}). 나중 것만 적용된다",
                  OpName(static_cast<int>(m_pending.op)), OpName(static_cast<int>(op)));
    }
    m_pending.op    = op;
    m_pending.scene = std::move(scene);
}


void SceneManager::Replace(std::unique_ptr<Scene> scene) { SetPending(Op::Replace, std::move(scene)); }
void SceneManager::Push(std::unique_ptr<Scene> scene)    { SetPending(Op::Push,    std::move(scene)); }
void SceneManager::Pop()                                 { SetPending(Op::Pop,     nullptr); }
void SceneManager::Clear()                               { SetPending(Op::Clear,   nullptr); }


void SceneManager::PushNow(SceneContext& ctx, std::unique_ptr<Scene> scene)
{
    if (!scene)
        return;

    // Name() 은 문자열 리터럴을 돌려주므로 move 후에도 유효하다.
    const char* name = scene->Name();

    if (!scene->Enter(ctx))
    {
        // Enter 가 실패하면 스택을 건드리지 않는다.
        // 이전 Scene 이 그대로 남아 있어서 게임이 죽지 않는다.
        Log::Error("[scene] {} 진입 실패 — 전환 취소", name);
        return;
    }

    m_stack.push_back(std::move(scene));
    Log::Info("[scene] + {} (깊이 {})", name, m_stack.size());
}


void SceneManager::PopNow()
{
    if (m_stack.empty())
    {
        Log::Warn("[scene] 빈 스택에서 Pop 요청");
        return;
    }

    const char* name = m_stack.back()->Name();
    m_stack.back()->Exit();
    m_stack.pop_back();
    Log::Info("[scene] - {} (깊이 {})", name, m_stack.size());
}


void SceneManager::ApplyPending(SceneContext& ctx)
{
    if (m_pending.op == Op::None)
        return;

    // ★ 먼저 꺼내고 비운다.
    //   아래에서 부르는 Enter() 안에서 또 전환을 요청할 수 있기 때문이다.
    //   비우지 않으면 그 요청이 여기서 덮어써져 사라진다.
    const Op op = m_pending.op;
    std::unique_ptr<Scene> incoming = std::move(m_pending.scene);
    m_pending.op    = Op::None;
    m_pending.scene = nullptr;

    switch (op)
    {
    case Op::Replace:
        while (!m_stack.empty())
            PopNow();
        PushNow(ctx, std::move(incoming));
        break;

    case Op::Push:
        PushNow(ctx, std::move(incoming));
        break;

    case Op::Pop:
        PopNow();
        break;

    case Op::Clear:
        Log::Info("[scene] 전체 비우기");
        while (!m_stack.empty())
            PopNow();
        break;

    default:
        break;
    }
}


void SceneManager::UpdateStack(SceneContext& ctx, bool consumeEdgeInput)
{
    if (m_stack.empty())
        return;

    // 맨 위에서부터 내려가며 「아래도 갱신」 이 끊기는 지점을 찾는다.
    // Pause(UpdatesBelow = false) 가 위에 있으면 first 는 Pause 자신이 되어
    // 아래 Play 는 갱신되지 않는다 = 게임이 멈춘다.
    size_t first = m_stack.size() - 1;
    while (first > 0 && m_stack[first]->UpdatesBelow())
        --first;

    for (size_t i = first; i < m_stack.size(); ++i)
        m_stack[i]->Update(ctx, consumeEdgeInput);
}


size_t SceneManager::FirstVisibleIndex() const
{
    size_t first = m_stack.size() - 1;
    while (first > 0 && m_stack[first]->DrawsBelow())
        --first;
    return first;
}


void SceneManager::RenderStack(Renderer& renderer)
{
    if (m_stack.empty())
        return;

    // 그리기는 아래부터 위로. 위에 있는 것이 나중에 그려져 위에 덮인다.
    const size_t first = FirstVisibleIndex();
    for (size_t i = first; i < m_stack.size(); ++i)
        m_stack[i]->Render(renderer);
}


void SceneManager::RenderUIStack(Renderer& renderer)
{
    if (m_stack.empty())
        return;

    // 월드와 같은 규칙으로 훑는다. 월드는 다 그린 뒤에 UI 를 한꺼번에 얹으므로
    // Pause 의 어두운 판이 Play 의 UI 문구까지 덮는다 — 의도한 순서다.
    const size_t first = FirstVisibleIndex();
    for (size_t i = first; i < m_stack.size(); ++i)
        m_stack[i]->RenderUI(renderer);
}
