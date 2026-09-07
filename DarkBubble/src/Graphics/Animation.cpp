#include "Graphics/Animation.h"

void AnimationPlayer::Play(const AnimationClip& clip, bool forceRestart)
{
    // 같은 클립이면 무시한다. row 를 애니메이션의 신원으로 본다.
    if (!forceRestart && m_clip.row == clip.row && m_clip.frameCount == clip.frameCount)
        return;

    m_clip     = clip;
    m_frame    = 0;
    m_tick     = 0;
    m_finished = false;
}


void AnimationPlayer::Tick()
{
    if (m_finished)            return;
    if (m_clip.frameCount <= 1) return;   // 1 프레임짜리는 넘길 것이 없다

    // 이 칸을 아직 충분히 보여주지 않았다면 그대로 둔다.
    if (++m_tick < m_clip.ticksPerFrame)
        return;

    m_tick = 0;

    if (++m_frame >= m_clip.frameCount)
    {
        if (m_clip.loop)
        {
            m_frame = 0;
        }
        else
        {
            // 마지막 프레임에서 멈춘다. 공격 모션 등에 쓰인다.
            m_frame    = m_clip.frameCount - 1;
            m_finished = true;
        }
    }
}


RECT AnimationPlayer::SourceRect(int cellW, int cellH) const
{
    const LONG left = static_cast<LONG>(m_frame * cellW);
    const LONG top  = static_cast<LONG>(m_clip.row * cellH);
    return { left, top, left + cellW, top + cellH };
}
