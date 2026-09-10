// ============================================================================
//  Component.h
//    컴포넌트의 뿌리. 거의 순수 인터페이스다.
//
//  ---- ★ 상속과 합성은 경쟁하지 않는다. 다른 층에서 각자 일한다 ----
//
//    상속 : 엔진이 **무엇인지 모르는 것을 같은 방식으로 다루기** 위해
//           -> Component 하나를 상속하면 GameObject 가 굴려 준다
//
//    합성 : 엔티티가 **무엇을 할 수 있는지 조립하기** 위해
//           -> GameObject 가 컴포넌트를 "가진다"
//
//    상속으로 엔티티를 만들면(Entity -> Character -> Player / Enemy) 이 게임에서
//    바로 깨진다:
//
//                 부위파괴  구르기  스태미나  강인도   AI
//        Player      X        O        O       O      X
//        Enemy       O        X        X       X      O
//
//    「부위 파괴」를 Character 에 넣으면 플레이어가 안 쓰는 코드를 물려받고,
//    Enemy 에만 넣으면 나중에 「부위 파괴가 있는 보스 플레이어」를 못 만든다.
//    다중 상속으로 가면 다이아몬드가 생긴다.
//
//    합성이면 `Parts` 를 붙이면 끝이다. 계층을 다시 짜지 않는다.
//    Unity 가 상속에서 합성으로 옮긴 이유가 정확히 이것이다.
// ============================================================================
#pragma once

class GameObject;
class Renderer;
struct SceneContext;

class Component
{
public:
    // ★ 가상 소멸자. 없으면 GameObject 가 unique_ptr<Component> 를 파괴할 때
    //   **파생 클래스의 소멸자가 불리지 않는다.** 고전적인 함정이고,
    //   컴파일러가 경고하지 않으며, 리소스를 들고 있는 컴포넌트가 생기는
    //   순간 조용히 새기 시작한다.
    virtual ~Component() = default;

    // ---- 수명 ----
    //   Start : GameObject 에 **전부 붙은 뒤** 한 번.
    //           ★ 생성자가 아니라 여기서 다른 컴포넌트를 찾는다 —
    //             생성자 시점에는 뒤에 붙을 컴포넌트가 아직 없기 때문이다.
    virtual void Start(SceneContext&) {}

    // ---- 매 틱 ----
    //   ★ 고정 타임스텝이라 시간 인자가 없다(Scene::Update 와 같은 이유).
    virtual void Tick(SceneContext&) {}

    // ---- 그리기 ----
    //   Scene 과 같은 두 층. Render 는 카메라를 타고 RenderUI 는 안 탄다.
    virtual void Render(Renderer&) {}
    virtual void RenderUI(Renderer&) {}

    // ---- ★ 디버그 그리기는 **별도 패스**다 ----
    //
    //   왜 Render 와 나누는가:
    //     실행 순서 = 붙인 순서인데, **틱 순서와 그리기 순서의 요구가 다르다.**
    //
    //       틱   : Brain -> Sprite   (이번 틱에 바꾼 클립이 같은 틱에 반영되어야 한다)
    //       그리기: Sprite -> 디버그  (판정 상자가 그림 **위**에 보여야 한다)
    //
    //     하나의 순서로 둘 다 만족시킬 수 없다. 실제로 Parts 를 먼저 붙였더니
    //     판정 상자를 스프라이트가 덮어 「히트박스가 뒤에 있는」 상태가 되었다.
    //
    //   패스를 나누면 **붙인 순서와 무관하게** 디버그가 항상 위에 온다.
    virtual void RenderDebug(Renderer&) {}

    // 로그·디버그 표시용. ★ 순수 가상이라 「이름을 대지 않는 컴포넌트」를
    // 만들 수 없다 — 추상 클래스가 계약을 강제하는 가장 단순한 형태다.
    virtual const char* TypeName() const = 0;

    GameObject& Owner() const { return *m_owner; }

private:
    // ★ 캡슐화: 주인을 꽂을 수 있는 것은 GameObject 하나뿐이다.
    //   public 으로 두면 밖에서 컴포넌트를 다른 오브젝트에 몰래 붙일 수 있고,
    //   그러면 「누가 이 컴포넌트를 소유하는가」가 코드에서 사라진다.
    friend class GameObject;
    GameObject* m_owner = nullptr;
};
