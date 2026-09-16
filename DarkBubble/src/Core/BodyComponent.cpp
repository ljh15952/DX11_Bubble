#include "Core/BodyComponent.h"

#include "Core/Constants.h"
#include "Core/GameObject.h"
#include "Core/Level.h"
#include "Graphics/Renderer.h"

#include <DirectXColors.h>

namespace
{
    // ---- ★ 이 세 숫자가 점프의 「느낌」 전부다 ----
    //
    //   플레이어의 점프 초속 7.0 기준:
    //     정점 ≈ 66픽셀 (틱마다 더한 적분값. 공식 v0²/2g = 70 은 참고값일 뿐이다)
    //     정점까지 ≈ 20틱,  총 체공 ≈ 40틱
    //
    //   ★ 궤적이 적의 머리 높이(발끝에서 37~55)를 **지나간다.**
    //     §3.8.2 의 「높은 데서 뛰어내려 머리를 노린다」가 숫자로 성립한다.
    constexpr float kGravity      = 0.35f;   // 틱당 속도 증가
    constexpr float kMaxFallSpeed = 9.0f;    // 종단 속도. 없으면 긴 낙하에서 바닥을 뚫는다

    // 낭떠러지 판정이 발밑을 훑는 깊이.
    //   ★ 짧아야 한다. 깊게 훑으면 발판 끝에서 **한참 전에** 멈추고,
    //     0 으로 두면 접촉만으로는 안 잡혀 그냥 걸어 나간다.
    constexpr float kLedgeProbe = 6.0f;
}


BodyComponent::BodyComponent(const Level& level, float halfWidth,
                             float standHeight, float crouchHeight, float proneHeight)
    : m_level(level)
    , m_halfWidth(halfWidth)
    , m_standHeight(standHeight)
    , m_crouchHeight(crouchHeight)
    , m_proneHeight(proneHeight)
{
}


AABB BodyComponent::Box() const
{
    const Transform& tr = Owner().transform;

    float h = m_standHeight;
    switch (m_posture)
    {
    case Posture::Crouch: h = m_crouchHeight; break;
    case Posture::Prone:  h = m_proneHeight;  break;
    case Posture::Stand:  break;
    }

    return { tr.x - m_halfWidth, tr.y - h,
             tr.x + m_halfWidth, tr.y };
}


bool BodyComponent::CanStandUp() const
{
    // ★ **선 자세의 상자**로 묻는다. 지금 웅크려 있든 아니든 답은 같아야 한다 —
    //   「여기서 일어설 수 있나」는 현재 자세와 무관한 질문이다.
    const Transform& tr = Owner().transform;
    const AABB standing{ tr.x - m_halfWidth, tr.y - m_standHeight,
                         tr.x + m_halfWidth, tr.y };

    bool blocked = false;
    m_level.ForEachOverlapping(standing, [&blocked](const AABB&) { blocked = true; });
    return !blocked;
}


bool BodyComponent::WouldStepOffLedge(float nextX) const
{
    // 공중에서는 물어봐야 의미가 없다 — 이미 떨어지는 중이다.
    if (!m_grounded)
        return false;

    return !m_level.HasFloorBelow(nextX, Owner().transform.y, kLedgeProbe);
}


void BodyComponent::MoveX(float dx)
{
    if (dx == 0.0f)
        return;

    Transform& tr = Owner().transform;
    tr.x += dx;

    // ---- 벽에서 밀어낸다 ----
    //   ★ 겹친 것들을 훑어 **가장 많이 밀어내는 값**을 고른다.
    //     겹칠 때마다 tr.x 를 고치면 두 번째부터는 이미 옮겨진 몸으로 판정하게 되어
    //     결과가 「어느 것을 먼저 봤는가」에 달라진다.
    float corrected = tr.x;
    m_level.ForEachOverlapping(Box(), [&](const AABB& s)
    {
        if (dx > 0.0f)
        {
            const float stop = s.left - m_halfWidth;    // 오른쪽으로 갔다 -> 왼쪽 면에서 멈춘다
            if (stop < corrected) corrected = stop;
        }
        else
        {
            const float stop = s.right + m_halfWidth;
            if (stop > corrected) corrected = stop;
        }
    });

    tr.x = corrected;
}


void BodyComponent::Jump(float speed)
{
    m_velocityY = -speed;   // ★ y 는 아래로 증가한다. 위로 = 음수
    m_grounded  = false;    // 같은 틱에 「아직 땅에 있다」로 읽히지 않게 즉시 끊는다
}


void BodyComponent::Lift(float speed)
{
    // ★ `-=` 가 아니라 `=` 다. 이유는 헤더 주석 참조 —
    //   더하면 올라가는 중에 맞았을 때 상승 속도에 얹혀 크게 날아오른다.
    m_velocityY = -speed;
    m_grounded  = false;
}


void BodyComponent::PlaceOnFloor(float y)
{
    Owner().transform.y = y;
    m_velocityY = 0.0f;   // 떨어지던 중이었어도 여기서 멎는다
    m_grounded  = true;
}


void BodyComponent::SnapToGround()
{
    // ★ 지면은 **맵이 정한다.** 전역 상수를 쓰면 지면이 다른 맵에서
    //   바닥 속에 놓인다(동굴 600 vs 들판 680).
    PlaceOnFloor(m_level.GroundY());
}


void BodyComponent::Tick(SceneContext&, bool)
{
    Transform& tr = Owner().transform;

    // ---- 가속 → 이동 → 충돌. 순서가 바뀌면 안 된다 ----
    //   먼저 움직이고 나중에 가속하면 첫 틱의 이동량이 빠져 점프가 낮아진다.
    m_velocityY += kGravity;
    if (m_velocityY > kMaxFallSpeed)
        m_velocityY = kMaxFallSpeed;

    const float dy = m_velocityY;
    tr.y += dy;

    // ---- 세로 충돌 ----
    //   ★ 「닿았으면 멈춘다」가 아니라 **「넘어갔으면 되돌린다」**다.
    //     한 틱에 최대 9픽셀씩 움직이므로 「정확히 닿는 순간」은 대부분 없다.
    //     빠르게 움직이는 것의 충돌은 항상 「지나쳤는가」로 물어야 한다.
    m_grounded = false;

    if (dy > 0.0f)
    {
        // 떨어지는 중 -> 뚫고 들어간 것들 중 **가장 높은 면**에 올려놓는다.
        float top = tr.y;
        m_level.ForEachOverlapping(Box(), [&top](const AABB& s)
        {
            if (s.top < top) top = s.top;
        });

        if (top < tr.y)
        {
            tr.y        = top;
            m_velocityY = 0.0f;
            m_grounded  = true;
        }
    }
    else if (dy < 0.0f)
    {
        // 올라가는 중 -> 천장에 머리를 박는다. **상승만 끊고** 떨어지게 둔다.
        const float head = Box().top;

        float bottom = head;
        m_level.ForEachOverlapping(Box(), [&bottom](const AABB& s)
        {
            if (s.bottom > bottom) bottom = s.bottom;
        });

        if (bottom > head)
        {
            tr.y       += (bottom - head);   // 머리가 파고든 만큼 내린다
            m_velocityY = 0.0f;
        }
    }
}


void BodyComponent::RenderDebug(Renderer& renderer)
{
    if (!renderer.DebugDraw())
        return;

    // ★ 피격 상자(금색·보라)와 **다른 색**으로 그린다.
    //   같은 색이면 「왜 상자가 하나 더 있지?」가 되고, 이 상자가 자세를
    //   안 따라가는 것이 버그로 보인다. 실제로는 규칙이다(헤더 주석 참조).
    renderer.DrawRectOutline(Box(), DirectX::Colors::DarkCyan, 1.0f);
}
