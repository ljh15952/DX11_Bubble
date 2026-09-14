// ============================================================================
//  BodyComponent.h
//    중력 · 수직 속도 · 지형 충돌. 「월드에 있는 몸」이 땅에 붙어 있게 한다.
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
//  ---- ★★ 지형 상자와 피격 상자는 **다른 것**이다 ----
//    여기의 상자는 「어디에 설 수 있는가」를 정하고,
//    PartsComponent 의 상자는 「어디를 맞는가」를 정한다.
//
//    같게 만들면 **웅크릴 때마다 지형을 뚫는다** — 피격 상자는 자세를 따라
//    낮아지는데, 그걸 지형에 쓰면 몸이 발판 속으로 내려앉기 때문이다.
//    그래서 여기의 상자는 **자세와 무관하게 고정**이다.
//
//  ---- 이 컴포넌트가 **모르는** 것 ----
//    점프 버튼도, 상태 머신도, 「지금 구르는 중인지」도 모른다.
//    그런 판단은 PlayerController / EnemyBrain 의 일이고,
//    여기는 「위로 이만큼 튀어라」(Jump) 같은 **명령만** 받는다.
//    그 경계 덕분에 플레이어와 적이 같은 것을 쓴다.
// ============================================================================
#pragma once

#include "Core/AABB.h"
#include "Core/Component.h"

class Level;

class BodyComponent final : public Component
{
public:
    // halfWidth / height = **지형 충돌용** 몸 상자. 발밑이 원점이다.
    BodyComponent(const Level& level, float halfWidth, float height);

    const char* TypeName() const override { return "Body"; }

    void Tick(SceneContext& ctx, bool consumeEdgeInput) override;
    void RenderDebug(Renderer& renderer) override;

    // ---- 조회 ----
    //   ★ 「공중인가」를 상태(PlayerState::Jump)가 아니라 **몸**에게 묻는다.
    //     상태로 물으면 상태를 하나 늘릴 때마다 조건을 다시 고쳐야 한다 —
    //     이 프로젝트가 이미 밟은 함정이다(handoff §8, 「조건을 상태 목록으로 쓰기」).
    bool  Grounded()  const { return m_grounded; }
    float VelocityY() const { return m_velocityY; }

    AABB Box() const;   // 지금 지형 충돌 상자

    // ★ 이 x 로 한 걸음 옮기면 발밑이 비는가.
    //   적이 발판 끝에서 멈추는 데 쓴다. **움직이기 전에** 물어야 멈출 수 있다.
    //   지형을 아는 것이 이 컴포넌트뿐이므로 질문도 여기서 받는다 —
    //   EnemyBrain 에게 Level 을 또 쥐여 주면 지형을 아는 곳이 둘이 된다.
    bool WouldStepOffLedge(float nextX) const;

    // ---- 명령 ----
    //   가로로 옮긴다. **벽에 막히면 거기까지만** 간다.
    //   ★ 걷기·구르기·넉백이 전부 이 문을 지난다. 하나라도 tr.x 를 직접 쓰면
    //     그 이동만 벽을 통과한다.
    void MoveX(float dx);

    // 위로 튄다. speed 는 **양수**로 받고 안에서 부호를 뒤집는다 —
    // y 가 아래로 증가하는 좌표계라서, 부르는 쪽이 부호를 외우게 하면
    // 반드시 한 번은 틀린다.
    void Jump(float speed);

    // 넉백의 「살짝 뜨는」 부분.
    //
    //   ★ **더하지 않고 덮어쓴다.** 더했더니 점프로 올라가는 중에 맞으면
    //     상승 속도에 얹혀서 **훨씬 높이 날아올랐다.**
    //     「띄운다」는 「이만큼 뜨게 만든다」이지 「이만큼 더 뜬다」가 아니다.
    //
    //   덮어쓰면 맞는 순간 상승이 끊기고 작게 튄 뒤 떨어진다 —
    //   「한 대 맞고 점프가 끊겼다」가 눈에 보이므로 규칙으로도 맞다.
    void Lift(float speed);

    // 부활 · 리셋용. 바닥에 정확히 놓고 속도를 지운다.
    void SnapToGround();

private:
    const Level& m_level;

    float m_halfWidth = 9.0f;
    float m_height    = 44.0f;

    float m_velocityY = 0.0f;
    bool  m_grounded  = true;
};
