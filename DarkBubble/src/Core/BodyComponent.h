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
//    여기의 상자는 「어디를 지나갈 수 있는가」를 정하고,
//    PartsComponent 의 상자는 「어디를 맞는가」를 정한다.
//
//    다만 **높이는 자세를 따라간다.** 웅크리면 낮은 틈을 지나갈 수 있어야
//    하기 때문이다. 원점이 발밑이라 상자는 **위에서** 줄어든다 — 발은 그대로다.
//
//    부위별로 나뉘지 않는 것이 피격 상자와의 차이다. 지형은 「머리가 맞았나」를
//    묻지 않는다. 그래서 상자 하나면 되고, 자세는 **높이 하나**로 표현된다.
//
//    ★ 높이가 변하면 「일어설 수 없는 자리」가 생긴다(CanStandUp 참조).
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
#include "Core/Posture.h"

class Level;

class BodyComponent final : public Component
{
public:
    // halfWidth + 자세마다의 높이 = **지형 충돌용** 몸 상자.
    // 발밑이 원점이라 높이는 위로 자란다.
    BodyComponent(const Level& level, float halfWidth,
                  float standHeight, float crouchHeight, float proneHeight);

    const char* TypeName() const override { return "Body"; }

    void Tick(SceneContext& ctx, bool consumeEdgeInput) override;
    void RenderDebug(Renderer& renderer) override;

    // ---- 조회 ----
    //   ★ 「공중인가」를 상태(PlayerState::Jump)가 아니라 **몸**에게 묻는다.
    //     상태로 물으면 상태를 하나 늘릴 때마다 조건을 다시 고쳐야 한다 —
    //     이 프로젝트가 이미 밟은 함정이다(handoff §8, 「조건을 상태 목록으로 쓰기」).
    bool  Grounded()  const { return m_grounded; }
    float VelocityY() const { return m_velocityY; }

    AABB Box() const;   // 지금 지형 충돌 상자 (자세에 따라 높이가 다르다)

    // ★ 지금 자리에서 **일어설 수 있는가.**
    //   천장이 낮으면 false — 부르는 쪽은 웅크린 채로 둬야 한다.
    //   없으면 낮은 틈에서 Ctrl 을 떼는 순간 몸이 천장 속에 박힌다.
    //   세로 충돌은 「움직이는 중」에만 해결되므로, 가만히 커진 몸은 아무도 밀어내지 않는다.
    bool CanStandUp() const;

    // 자세를 알려 준다.
    //   ★ 전에는 `SetCrouching(bool)` 이었다. 자세가 둘일 때는 맞았는데,
    //     엎드리기가 생기자 **bool 로 말할 수 없는 세 번째 값**이 되었고
    //     엎드린 몸이 「서 있는 크기」로 남았다. bool 은 언제나 둘 중 하나를
    //     정확히 답하므로 아무도 눈치채지 못한다.
    void SetPosture(Posture p) { m_posture = p; }

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

    float m_halfWidth    =  9.0f;
    float m_standHeight  = 44.0f;
    float m_crouchHeight = 28.0f;
    float m_proneHeight  = 26.0f;
    Posture m_posture    = Posture::Stand;

    float m_velocityY = 0.0f;
    bool  m_grounded  = true;
};
