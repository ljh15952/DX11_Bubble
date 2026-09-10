// ============================================================================
//  DeathScene.h
//    사망 화면.
//
//    ★ PauseScene 과 **똑같은 구조**다. 한 줄도 새로 설계하지 않았다.
//
//        DrawsBelow   = true    -> 죽은 세계가 뒤에 그대로 보인다
//        UpdatesBelow = false   -> 그 세계는 멈춰 있다
//
//      소울류가 실제로 하는 연출이 정확히 이것이다 — 쓰러진 자리를 보여준 채
//      화면이 서서히 어두워지고 글자가 뜬다. 화면을 갈아치우면 「어디서 죽었는지」가
//      사라져서 플레이어가 배울 것이 없어진다.
//
//    ★ 이 Scene 은 PlayScene 을 모른다.
//      부활은 PlayScene 이 Scene::Resume 에서 스스로 한다.
//      여기는 「보여주고 닫는다」만 한다.
//
//  ---- 기획서 3.6 과의 관계 ----
//    기획서는 「`You died` 대신 부활하면서 멋진 대사, 한 줄 고정이 아니라 테이블」
//    이라고 되어 있다. 지금은 **보류**다:
//      ① BitmapFont 가 ASCII 전용이라 한국어·일본어 대사를 화면에 못 쓴다
//         (design.md 6.5 의 「런타임 글리프 아틀라스」가 먼저 필요하다)
//      ② 대사는 이 파일의 **문자열 한 줄**이므로, 폰트가 생기면 그 자리에
//         테이블을 끼우면 된다. 지금 안 만드는 것이 손해가 아니다.
// ============================================================================
#pragma once

#include "Core/Scene.h"

class DeathScene final : public Scene
{
public:
    const char* Name() const override { return "Death"; }

    bool Enter(SceneContext& ctx) override;
    void Update(SceneContext& ctx, bool consumeEdgeInput) override;

    // 어두운 판과 글자는 UI 레이어. 화면이 흔들려도 흔들리면 안 된다.
    void Render(Renderer&) override {}
    void RenderUI(Renderer& renderer) override;

    bool DrawsBelow()   const override { return true;  }   // 죽은 자리는 보이게
    bool UpdatesBelow() const override { return false; }   // 하지만 멈춰 있게

private:
    // ★ 페이드는 틱을 세는 것으로 끝난다.
    //   고정 타임스텝이라 「n 틱에 걸쳐 0 -> 1」이 프레임 레이트와 무관하게 같다.
    //   가변 타임스텝이었다면 경과 시간을 누적하고 보정해야 했다.
    int m_ticks = 0;

    // Pop 요청은 한 번만. 한 프레임에 틱이 여러 번 돌면 두 번 요청되어
    // SceneManager 가 「전환이 두 번 요청됨」 경고를 낸다.
    bool m_leaving = false;
};
