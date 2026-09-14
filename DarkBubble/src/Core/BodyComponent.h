// ============================================================================
//  BodyComponent.h
//    중력 · 수직 속도 · 지면 충돌. 「월드에 있는 몸」이 땅에 붙어 있게 한다.
//
//  ---- ★ 여기서 처음으로 「속도」가 들어온다 ----
//    지금까지 이 게임의 이동은 전부 **「이번 틱에 얼마나 옮길지」**였다.
//
//        tr.x += moveX * speed;                  걷기
//        tr.x += dirX * DecayingStep(...);       구르기 · 넉백
//
//    중력은 다르다. **가속**이라서 「지금 얼마나 빠른지」를 기억해야 한다.
//
//        velocityY += 중력       매 틱 빨라진다
//        y         += velocityY
//        착지하면 velocityY = 0
//
//    ★ 그래서 두 가지 이동 방식이 공존하게 된다.
//      부딪히지 않도록 규칙을 정해 둔다:
//        **구르기는 지상 전용**이므로 구르는 동안 수직 속도는 0이다.
//        넉백은 수평(DecayingStep) + 살짝 위로(여기의 속도) 로 나눠 쓴다.
//
//  ---- ★ 왜 처음부터 컴포넌트인가 ----
//    §9 의 규칙은 「**두 번째 사용자가 생기면** 엔진으로 올린다」인데,
//    중력은 두 번째 사용자가 **처음부터** 있다 — 플레이어도 적도 떨어진다.
//    규칙을 어기는 것이 아니라, 규칙의 조건이 이미 충족된 드문 경우다.
//
//    나중에 투사체·상자·시체가 생겨도 이걸 붙이기만 하면 된다.
//
//  ---- 이 컴포넌트가 **모르는** 것 ----
//    점프 버튼도, 상태 머신도, 「지금 구르는 중인지」도 모른다.
//    그런 판단은 PlayerController / EnemyBrain 의 일이고,
//    여기는 「위로 이만큼 튀어라」(Jump)라는 **명령만** 받는다.
//    그 경계 덕분에 플레이어와 적이 같은 것을 쓴다.
// ============================================================================
#pragma once

#include "Core/Component.h"

class BodyComponent final : public Component
{
public:
    const char* TypeName() const override { return "Body"; }

    void Tick(SceneContext& ctx, bool consumeEdgeInput) override;

    // ---- 조회 ----
    //   ★ 「공중인가」를 상태(PlayerState::Jump)가 아니라 **몸**에게 묻는다.
    //     상태로 물으면 상태를 하나 늘릴 때마다 조건을 다시 고쳐야 한다 —
    //     이 프로젝트가 이미 밟은 함정이다(handoff §8, 「조건을 상태 목록으로 쓰기」).
    bool  Grounded()  const { return m_grounded; }
    float VelocityY() const { return m_velocityY; }

    // ---- 명령 ----
    //   위로 튄다. speed 는 **양수**로 받고 안에서 부호를 뒤집는다 —
    //   y 가 아래로 증가하는 좌표계라서, 부르는 쪽이 부호를 외우게 하면
    //   반드시 한 번은 틀린다.
    void Jump(float speed);

    // 넉백의 「살짝 뜨는」 부분. 기존 속도에 더한다.
    void AddLift(float speed);

    // 부활 · 리셋용. 바닥에 정확히 놓고 속도를 지운다.
    void SnapToGround();

private:
    float m_velocityY = 0.0f;
    bool  m_grounded  = true;
};
