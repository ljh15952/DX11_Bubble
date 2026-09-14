#include "Core/BodyComponent.h"

#include "Core/Constants.h"
#include "Core/GameObject.h"

namespace
{
    // ---- ★ 이 세 숫자가 점프의 「느낌」 전부다 ----
    //
    //   정점 높이 = v0² / (2g) = 6.2² / 0.7 ≈ 55 픽셀
    //   정점까지  = v0 / g     = 6.2 / 0.35 ≈ 18 틱 (0.30초)
    //   총 체공   ≈ 35 틱 (0.6초)
    //
    //   ★ 55픽셀은 우연히 고른 값이 아니다. 캐릭터 키가 62픽셀이고
    //     적의 머리 상자가 「발끝에서 37~55 위」에 있다.
    //     즉 점프 궤적의 중간쯤에서 **머리 높이를 지나간다** —
    //     §3.8.2 의 「높은 데서 뛰어내려 머리를 노린다」가 숫자로 성립한다.
    constexpr float kGravity      = 0.35f;   // 틱당 속도 증가
    constexpr float kMaxFallSpeed = 9.0f;    // 종단 속도. 없으면 긴 낙하에서 바닥을 뚫는다
}


void BodyComponent::Jump(float speed)
{
    m_velocityY = -speed;   // ★ y 는 아래로 증가한다. 위로 = 음수
    m_grounded  = false;    // 같은 틱에 「아직 땅에 있다」로 읽히지 않게 즉시 끊는다
}


void BodyComponent::AddLift(float speed)
{
    m_velocityY -= speed;
    m_grounded   = false;
}


void BodyComponent::SnapToGround()
{
    Owner().transform.y = Config::kGroundY;
    m_velocityY = 0.0f;
    m_grounded  = true;
}


void BodyComponent::Tick(SceneContext&, bool)
{
    Transform& tr = Owner().transform;

    // ---- 가속 → 이동 → 충돌. 순서가 바뀌면 안 된다 ----
    //   먼저 움직이고 나중에 가속하면 첫 틱의 이동량이 빠져 점프가 낮아진다.
    m_velocityY += kGravity;
    if (m_velocityY > kMaxFallSpeed)
        m_velocityY = kMaxFallSpeed;

    tr.y += m_velocityY;

    // ---- 지면 ----
    //   ★ 「넘어갔으면 되돌린다」다. 「닿았으면 멈춘다」로 쓰면 한 틱에
    //     9픽셀씩 움직이므로 바닥을 **그냥 지나쳐 버린다.**
    //     빠르게 움직이는 것의 충돌은 항상 「지나쳤는가」로 물어야 한다.
    if (tr.y >= Config::kGroundY)
    {
        tr.y        = Config::kGroundY;
        m_velocityY = 0.0f;
        m_grounded  = true;
    }
    else
    {
        m_grounded = false;
    }
}
