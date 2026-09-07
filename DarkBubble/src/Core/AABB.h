// ============================================================================
//  AABB.h
//    Axis-Aligned Bounding Box — 기울어지지 않은 직사각형.
//    2D 액션 게임의 충돌 판정은 거의 전부 이걸로 한다.
// ============================================================================
#pragma once

struct AABB
{
    float left   = 0.0f;
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;

    float Width()  const { return right - left; }
    float Height() const { return bottom - top; }

    // 좌상단 + 크기로 만드는 편의 함수
    static AABB FromXYWH(float x, float y, float w, float h)
    {
        return { x, y, x + w, y + h };
    }

    // 평행이동한 사본
    AABB Offset(float dx, float dy) const
    {
        return { left + dx, top + dy, right + dx, bottom + dy };
    }
};


// ----------------------------------------------------------------------------
//  Intersects — 겹치면 true
//
//  "안 겹침" 조건은 넷 중 하나라도 참이면 성립한다.
//      a.right  <= b.left      a 가 b 의 완전히 왼쪽
//      a.left   >= b.right     a 가 b 의 완전히 오른쪽
//      a.bottom <= b.top       a 가 b 의 완전히 위쪽
//      a.top    >= b.bottom    a 가 b 의 완전히 아래쪽
//
//  겹침은 그 넷이 모두 거짓일 때다. 부정을 뒤집으면 아래 형태가 된다.
//  비교 4 번, 분기 없음. 이래서 2D 의 기본 판정이 되었다.
// ----------------------------------------------------------------------------
inline bool Intersects(const AABB& a, const AABB& b)
{
    return a.left < b.right
        && b.left < a.right
        && a.top  < b.bottom
        && b.top  < a.bottom;
}
