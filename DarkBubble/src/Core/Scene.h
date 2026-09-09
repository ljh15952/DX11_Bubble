// ============================================================================
//  Scene.h
//    화면 하나 = 상태 하나. Title / Play / Pause / Death 가 각각 Scene 이다.
//
//  ---- Cocos2d 와 일부러 다르게 만든 곳 ----
//
//  ① 전역 싱글턴을 쓰지 않는다
//     Cocos2d 는 Director::getInstance() 로 어디서든 접근한다.
//     우리는 필요한 것을 SceneContext 로 받는다.
//       - 의존 관계가 함수 시그니처에 드러난다 (숨은 의존이 없다)
//       - 초기화 순서 문제가 생기지 않는다
//       - 나중에 테스트할 때 가짜 객체로 갈아끼울 수 있다
//
//  ② dt(float) 를 넘기지 않는다
//     Cocos2d 의 update(float dt) 는 가변 타임스텝이다.
//     우리는 1 틱 = 1/60 초 고정이라 시간 인자가 필요 없다.
//     프레임 데이터(「공격 발생 8틱」) 기반 액션 게임에는 이쪽이 맞다.
//
//  ③ Node 트리를 만들지 않는다
//     addChild / 부모-자식 변환 상속은 고정화면 2D + 카메라 없음 환경에서
//     거의 값을 내지 못한다. 좌표를 직접 다루는 편이 정확하다.
//
//  ④ 소유권이 unique_ptr 로 명확하다
//     Cocos2d 의 retain/release + autorelease 는 실수하기 쉽다.
//
//  ⑤ 「아래 Scene 을 어떻게 취급할까」 를 Scene 자신이 선언한다
//     Cocos2d 는 pushScene 하면 아래가 완전히 멈춘다.
//     Pause 오버레이는 "아래를 그리되 갱신은 멈춤" 이 필요한데,
//     Cocos2d 에서는 이걸 우회해서 구현해야 한다. 아래 DrawsBelow 참조.
// ============================================================================
#pragma once

class Renderer;
class Input;
class SceneManager;
class Assets;


// Scene 이 일할 때 필요한 것들을 한 다발로 묶어 넘긴다.
// 참조로 들고 있으므로 Game 보다 오래 살아서는 안 된다.
//
// 새 시스템(사운드 등)이 늘어나면 여기에 한 줄 추가하면 된다.
// 전역 싱글턴이었다면 "어디서 누가 쓰는지" 를 알 수 없지만,
// 이 구조에서는 이 목록이 곧 Scene 이 접근할 수 있는 것의 전부다.
struct SceneContext
{
    Renderer&     renderer;
    const Input&  input;
    SceneManager& scenes;
    Assets&       assets;
};


class Scene
{
public:
    virtual ~Scene() = default;

    // 로그에 찍히는 이름. 전환이 저절로 기록되어 디버깅이 쉬워진다.
    virtual const char* Name() const = 0;

    // ---- 수명 ----
    //   Enter 는 실패할 수 있다(리소스 로드 실패 등). false 면 전환이 취소되고
    //   이전 Scene 이 그대로 유지된다. Cocos2d 의 onEnter 는 void 라서
    //   실패했을 때 깨끗하게 되돌릴 방법이 없다.
    virtual bool Enter(SceneContext&) { return true; }
    virtual void Exit() {}

    // ---- 매 틱 ----
    //   정확히 1/60 초 분량. 시간 인자가 없는 것에 주의.
    //
    //   consumeEdgeInput:
    //     Update 는 한 프레임에 0~N 회 불릴 수 있다.
    //     「지금 눌려 있다」(이동) 는 여러 번 처리해도 되지만
    //     「방금 눌렸다」(공격/전환) 는 프레임의 첫 틱에서만 소비해야 한다.
    virtual void Update(SceneContext&, bool consumeEdgeInput) = 0;

    // ---- 그리기 ----
    //   ★ SceneContext 가 아니라 Renderer 만 받는다. 의도적이다.
    //     그리는 중에 입력을 읽거나 Scene 을 전환하는 것은 버그의 씨앗이라
    //     (Update 와 Render 의 순서에 따라 결과가 달라진다)
    //     타입 수준에서 아예 불가능하게 막았다.
    virtual void Render(Renderer&) = 0;

    // ---- 스택 위에 올라갔을 때 아래 Scene 을 어떻게 취급할까 ----
    //   Pause 오버레이  : DrawsBelow = true,  UpdatesBelow = false
    //                     (게임 화면은 보이지만 멈춰 있다)
    //   완전히 새 화면  : 둘 다 false (기본값)
    virtual bool DrawsBelow()   const { return false; }
    virtual bool UpdatesBelow() const { return false; }
};
