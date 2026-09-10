#include "Core/GameObject.h"

// ----------------------------------------------------------------------------
//  ★ 이 네 함수가 다형성의 전부다.
//
//    범위 기반 for 하나가 모든 컴포넌트를 굴린다.
//    GameObject 는 PlayerController 도 EnemyBrain 도 모른다 —
//    아는 것은 「Component 라는 것에 Tick 이 있다」뿐이다.
//
//    컴포넌트를 열 개 더 만들어도 이 파일은 한 줄도 안 바뀐다.
//    지금 PlayScene::Update 가 플레이어와 적을 **이름으로** 알고 있어서
//    적이 3종이 되면 그 함수가 늘어나는 것과 정반대다.
//
//    ※ 인덱스가 아니라 범위 기반 for 를 쓰는 이유:
//      Tick 안에서 컴포넌트를 추가하면 벡터가 재할당되어 반복자가 무효가 된다.
//      지금은 그런 코드가 없고, 필요해지면 「지연 추가 큐」를 둔다 —
//      SceneManager 가 Scene 전환을 미루는 것과 같은 해법이다.
// ----------------------------------------------------------------------------

void GameObject::Start(SceneContext& ctx)
{
    for (auto& c : m_components)
        c->Start(ctx);
}

void GameObject::Tick(SceneContext& ctx)
{
    for (auto& c : m_components)
        c->Tick(ctx);
}

void GameObject::Render(Renderer& renderer)
{
    for (auto& c : m_components)
        c->Render(renderer);
}

void GameObject::RenderDebug(Renderer& renderer)
{
    for (auto& c : m_components)
        c->RenderDebug(renderer);
}

void GameObject::RenderUI(Renderer& renderer)
{
    for (auto& c : m_components)
        c->RenderUI(renderer);
}
