// ============================================================================
//  Camera.h
//    월드를 화면에 어떻게 비출지 정하는 변환.
//
//    ---- 카메라의 정체 ----
//      카메라를 오른쪽으로 100 옮긴다  =  보이는 세계가 왼쪽으로 100 밀린다
//      그래서 뷰 행렬은 카메라 위치의 "부호를 뒤집은 평행이동" 이다.
//      대단해 보이는 이름이지만 실체는 역방향 이동이다.
//
//    ---- 왜 스프라이트마다 좌표를 더하지 않는가 ----
//      모든 Draw 호출을 고쳐야 하고, 게임 로직이 흔들림을 알게 된다.
//      행렬은 SpriteBatch::Begin 에 한 번만 넘기면 배치 전체에 적용되고,
//      게임 좌표는 월드 좌표 그대로 깨끗하게 남는다.
//
//    이 클래스는 DarkBubble 을 모른다. 다른 게임에 그대로 가져다 쓸 수 있다.
// ============================================================================
#pragma once

#include <DirectXMath.h>

class Camera
{
public:
    // 매 틱 호출. 흔들림을 갱신하고 감쇠시킨다.
    // ★ 틱 단위라 어느 PC 에서도 같은 속도로 잦아든다.
    void Tick();

    // strength : 최대 흔들림 픽셀 (캔버스 기준. 640x360 이므로 2~5 면 충분히 세다)
    // ticks    : 지속 시간 (60 = 1 초)
    void Shake(float strength, int ticks);

    void SetPosition(float x, float y) { m_x = x; m_y = y; }
    void SetZoom(float zoom);

    float X()    const { return m_x; }
    float Y()    const { return m_y; }
    float Zoom() const { return m_zoom; }

    // 지금 흔들리고 있는 세기. 디버그 표시용.
    float ShakeAmount() const;

    // SpriteBatch::Begin 에 넘길 행렬.
    DirectX::XMMATRIX ViewMatrix(int canvasWidth, int canvasHeight) const;

private:
    float m_x    = 0.0f;
    float m_y    = 0.0f;
    float m_zoom = 1.0f;

    // 흔들림 : 시작 강도에서 남은 틱 비율만큼 줄어든다
    float m_shakeStrength = 0.0f;
    int   m_shakeTicks    = 0;   // 남은 틱
    int   m_shakeTotal    = 0;   // 처음 지정된 틱 (감쇠 비율 계산용)

    // 이번 틱의 흔들림 오프셋
    float m_offsetX = 0.0f;
    float m_offsetY = 0.0f;
};
