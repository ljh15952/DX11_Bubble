// ============================================================================
//  GameObject.h
//    컴포넌트를 담는 그릇. **합성**이 여기서 일어난다.
//
//      플레이어 = GameObject + [Sprite] [Stamina] [Health] [PlayerController]
//      적       = GameObject + [Sprite] [Parts]   [EnemyBrain]
//
//    「부위 파괴가 있는 플레이어」가 필요하면 Parts 를 붙이면 끝이다.
//
//  ---- 실행 순서 = 붙인 순서 ----
//    Add 한 순서대로 Tick / Render 가 불린다.
//    ★ 그래서 **조립 코드가 곧 실행 순서**이고 따로 외울 것이 없다.
//      Unity 의 Script Execution Order 처럼 별도 설정 화면을 두면
//      「왜 이 순서지?」를 코드에서 알 수 없게 된다.
// ============================================================================
#pragma once

#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "Core/Component.h"
#include "Core/Log.h"
#include "Core/Transform.h"

class GameObject
{
public:
    explicit GameObject(std::string name = "object") : m_name(std::move(name)) {}

    // ★ 컴포넌트가 아니다. 이유는 Transform.h 참조.
    Transform transform;

    const std::string& Name() const { return m_name; }

    // ------------------------------------------------------------------------
    //  Add — 컴포넌트를 만들어 붙이고 참조를 돌려준다
    //
    //    가변 템플릿 + 완벽 전달이라 생성자 인자를 그대로 넘길 수 있다.
    //      obj.Add<SpriteComponent>(texture, 64, 64);
    // ------------------------------------------------------------------------
    template <class T, class... Args>
    T& Add(Args&&... args)
    {
        static_assert(std::is_base_of_v<Component, T>,
                      "T 는 Component 를 상속해야 한다");

        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *owned;

        // Component::m_owner 는 private 이지만 GameObject 가 friend 다.
        ref.m_owner = this;

        m_components.push_back(std::move(owned));
        return ref;
    }

    // ------------------------------------------------------------------------
    //  Get — 없으면 nullptr. 「있으면 쓰고 없으면 만다」에 쓴다
    //
    //    ★ dynamic_cast 를 쓴다. RTTI 가 실제로 하는 일이 그대로 보이기 때문이다 —
    //      「이 Component* 가 정말 T 인가」를 실행 시점에 묻는다.
    //      실제 엔진은 정적 타입 ID 로 바꿔 더 빠르게 만들지만, 그러면
    //      마법처럼 보이고 배울 것이 사라진다. 엔티티가 둘뿐이라 비용도 없다.
    // ------------------------------------------------------------------------
    template <class T>
    T* Get() const
    {
        static_assert(std::is_base_of_v<Component, T>,
                      "T 는 Component 를 상속해야 한다");

        for (const auto& c : m_components)
        {
            if (T* p = dynamic_cast<T*>(c.get()))
                return p;
        }
        return nullptr;
    }

    // ------------------------------------------------------------------------
    //  Require — 없으면 즉시 죽는다
    //
    //    ★ Get 과 나눠 둔 것이 요점이다. 이름이 곧 **계약**이다:
    //        Get     "있을 수도 있다"      -> 부르는 쪽이 검사한다
    //        Require "반드시 있어야 한다"  -> 없으면 조립이 잘못된 것이다
    //
    //      nullptr 을 돌려주고 나중에 터지면 **원인에서 멀리 떨어진 곳에서**
    //      죽는다. 여기서 죽으면 로그에 「무엇이 무엇을 요구했는지」가 남는다.
    // ------------------------------------------------------------------------
    template <class T>
    T& Require() const
    {
        T* p = Get<T>();
        if (!p)
        {
            Log::Error("[go] '{}' 에 {} 가 없다 — Require 는 "
                       "「반드시 있어야 한다」는 계약이다. 조립을 확인할 것",
                       m_name, typeid(T).name());
            std::abort();
        }
        return *p;
    }

    // ---- 엔진이 부른다 ----
    //   ★ 다형성이 실제로 값을 하는 곳.
    //     이 네 함수는 PlayerController 도, EnemyBrain 도, 나중에 만들
    //     BodyComponent 도 굴린다. 컴포넌트가 몇 종이 되든 **여기는 안 늘어난다.**
    void Start(SceneContext& ctx);
    void Tick(SceneContext& ctx);
    void Render(Renderer& renderer);
    void RenderDebug(Renderer& renderer);   // ★ 모든 Render 가 끝난 뒤에 부른다
    void RenderUI(Renderer& renderer);

    size_t ComponentCount() const { return m_components.size(); }

private:
    std::string m_name;

    // ★ unique_ptr 이라 소유권이 하나뿐이고, GameObject 가 사라지면
    //   컴포넌트도 전부 사라진다. 수명 관리가 코드에 드러나 있다.
    std::vector<std::unique_ptr<Component>> m_components;
};
