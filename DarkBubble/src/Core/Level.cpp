#include "Core/Level.h"

bool Level::HasFloorBelow(float x, float feetY, float reach) const
{
    // 발밑을 훑는 **가느다란 세로 막대**를 만들어 겹치는 것이 있는지 본다.
    //   ★ 폭을 0 으로 두면 안 된다. Intersects 가 엄격 부등호라
    //     넓이 0 인 사각형은 **무엇과도 안 겹친다.**
    const AABB probe{ x - 1.0f, feetY, x + 1.0f, feetY + reach };

    bool found = false;
    ForEachOverlapping(probe, [&found](const AABB&) { found = true; });
    return found;
}
