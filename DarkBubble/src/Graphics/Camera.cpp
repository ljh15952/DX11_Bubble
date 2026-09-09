#include "Graphics/Camera.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace
{
    // -1.0 ~ +1.0 난수
    float RandomUnit()
    {
        static std::mt19937 rng{ 9876 };
        static std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        return dist(rng);
    }
}


void Camera::SetZoom(float zoom)
{
    m_zoom = std::max(0.01f, zoom);   // 0 이나 음수면 화면이 사라진다
}


float Camera::ShakeAmount() const
{
    if (m_shakeTicks <= 0 || m_shakeTotal <= 0)
        return 0.0f;

    // 남은 틱 비율만큼 선형 감쇠.
    // 지수 감쇠로 하면 꼬리가 길게 남아 "완전히 멎지 않는" 느낌이 든다.
    return m_shakeStrength * (static_cast<float>(m_shakeTicks) /
                              static_cast<float>(m_shakeTotal));
}


void Camera::Shake(float strength, int ticks)
{
    if (ticks <= 0 || strength <= 0.0f)
        return;

    // ★ 지금 더 세게 흔들리는 중이면 무시한다.
    //   약한 흔들림이 강한 흔들림을 덮어써서 갑자기 잦아드는 것을 막는다.
    //   (연속 타격 중에 흔들림이 뚝 끊기면 매우 어색하다)
    if (strength < ShakeAmount())
        return;

    m_shakeStrength = strength;
    m_shakeTicks    = ticks;
    m_shakeTotal    = ticks;
}


void Camera::Tick()
{
    if (m_shakeTicks <= 0)
    {
        m_offsetX = 0.0f;
        m_offsetY = 0.0f;
        return;
    }

    const float s = ShakeAmount();
    m_offsetX = RandomUnit() * s;
    m_offsetY = RandomUnit() * s;

    --m_shakeTicks;
}


DirectX::XMMATRIX Camera::ViewMatrix(int canvasWidth, int canvasHeight) const
{
    using namespace DirectX;

    const float cx = canvasWidth  * 0.5f;
    const float cy = canvasHeight * 0.5f;

    // 카메라를 오른쪽으로 옮기면 세계는 왼쪽으로 → 부호 반전.
    //
    // ★ 흔들림 오프셋은 정수로 반올림한다.
    //   640x360 캔버스에서 0.5 픽셀을 흔들면 도트가 픽셀 격자 사이에 놓여
    //   뭉개진다. 스프라이트 원점을 정수로 맞춘 것과 같은 이유다.
    const float tx = -m_x + std::round(m_offsetX);
    const float ty = -m_y + std::round(m_offsetY);

    // DirectXMath 는 행벡터 규약이라 A * B 는 "A 를 먼저, 그다음 B" 다.
    //
    //   ① 화면 중심을 원점으로 옮기고
    //   ② 확대하고
    //   ③ 다시 중심으로 되돌린다
    //
    // ①③ 이 없으면 화면 좌상단을 기준으로 확대되어 그림이 오른쪽 아래로 쏠린다.
    // 스프라이트의 origin 이야기와 정확히 같은 구조다 — 무엇을 중심으로 변형하는가.
    return XMMatrixTranslation(tx - cx, ty - cy, 0.0f)
         * XMMatrixScaling(m_zoom, m_zoom, 1.0f)
         * XMMatrixTranslation(cx, cy, 0.0f);
}
