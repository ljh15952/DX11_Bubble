#include "Core/Level.h"

bool Level::HasFloorBelow(float x, float feetY, float reach) const
{
    // 발밑을 훑는 **가느다란 세로 막대**를 만들어 겹치는 것이 있는지 본다.
    //   ★ 폭을 0 으로 두면 안 된다. Intersects 가 엄격 부등호라
    //     넓이 0 인 사각형은 **무엇과도 안 겹친다.**
    const AABB probe{ x - 1.0f, feetY, x + 1.0f, feetY + reach };

    // ★ 발판도 **밟을 것**이다. 적이 발판 끝에서 멈추는 판정이 이것을 쓰므로,
    //   발판을 빼면 적이 발판 위에서 걷다가 끝에서 **안 멈추고** 떨어진다.
    bool found = false;
    ForEachSolid   (probe, [&found](const AABB&) { found = true; });
    ForEachPlatform(probe, [&found](const AABB&) { found = true; });
    return found;
}
