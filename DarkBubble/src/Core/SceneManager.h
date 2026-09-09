// ============================================================================
//  SceneManager.h
//    Scene 스택을 관리한다. Cocos2d 의 Director 에 해당하는 부분.
//
//  ---- 왜 스택인가 ----
//    Replace (교체)                  Push / Pop (쌓기)
//    [Title]                         [Play]            ← 살아 있음
//       ↓ Replace                       ↓ Push
//    [Play]   Title 은 소멸           [Play][Pause]     ← Play 데이터 유지
//                                       ↓ Pop
//                                    [Play]            ← 그대로 복귀
//
//    Pause 를 Replace 로 하면 플레이어 위치·HP·스태미나가 전부 날아간다.
//    사망 후 부활도 같은 이유로 Push/Pop 이다.
//
//  ---- ★ 전환을 지연시키는 이유 ----
//    Scene 이 자기 Update() 안에서 자기를 교체하는 일이 흔하다.
//
//        void PlayScene::Update(...) {
//            if (dead) ctx.scenes.Push(...);   // 여기서 즉시 삭제되면
//            m_player.Update();                // 해제된 메모리를 만진다 -> 크래시
//        }
//
//    그래서 요청은 큐에 담아두고, 틱 루프가 전부 끝난 뒤에 적용한다.
//    Cocos2d 도 내부적으로 같은 방식이다. 이걸 모르고 만들면
//    재현이 어려운 랜덤 크래시를 반드시 만난다.
// ============================================================================
#pragma once

#include <memory>
#include <vector>

#include "Core/Scene.h"

class SceneManager
{
public:
    // ---- 전환 요청 (즉시 처리되지 않는다) ----
    void Replace(std::unique_ptr<Scene> scene);   // 스택을 비우고 새로 올림
    void Push(std::unique_ptr<Scene> scene);      // 위에 쌓음 (아래는 유지)
    void Pop();                                   // 맨 위를 뺌
    void Clear();                                 // 전부 비움 = 게임 종료 신호

    // 미뤄둔 전환을 실제로 적용한다. 틱 루프가 끝난 뒤 Game 이 호출.
    void ApplyPending(SceneContext& ctx);

    // UpdatesBelow / DrawsBelow 를 따라 어디까지 처리할지 스스로 판단한다.
    void UpdateStack(SceneContext& ctx, bool consumeEdgeInput);
    void RenderStack(Renderer& renderer);

    bool   Empty() const { return m_stack.empty(); }
    size_t Depth() const { return m_stack.size(); }

    // 맨 위 Scene 의 이름. 비어 있으면 "-". 디버그 표시용.
    const char* TopName() const
    {
        return m_stack.empty() ? "-" : m_stack.back()->Name();
    }

private:
    enum class Op { None, Replace, Push, Pop, Clear };

    struct Pending
    {
        Op                     op = Op::None;
        std::unique_ptr<Scene> scene;
    };

    void SetPending(Op op, std::unique_ptr<Scene> scene);
    void PushNow(SceneContext& ctx, std::unique_ptr<Scene> scene);
    void PopNow();

    // 스택의 뒤쪽이 위(=화면에 가까운 쪽)다.
    std::vector<std::unique_ptr<Scene>> m_stack;
    Pending m_pending;
};
