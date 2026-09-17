// ============================================================================
//  Input.h
//    키보드와 게임패드를 "게임이 이해하는 의도"로 번역한다.
//
//    게임 로직은 「오른쪽 방향키가 눌렸나」가 아니라 「오른쪽으로 가고 싶나」만
//    알면 된다. 그래야 나중에 키 재설정이나 새 입력 장치를 넣을 때
//    게임 로직을 한 줄도 안 고쳐도 된다.
// ============================================================================
#pragma once

#include <windows.h>
#include <memory>
#include <Keyboard.h>
#include <GamePad.h>
#include <Mouse.h>

class Input
{
public:

    // ★ 마우스는 창을 알아야 한다(좌표계 · 캡처). 키보드·패드는 필요 없다.
    //   장치마다 초기화 요구가 다른 것을 Input 이 흡수한다 —
    //   Game 은 「입력을 초기화해라」만 알면 된다.
    void Initialize(HWND hwnd);

    // 창 프로시저에서 호출한다.
    // DirectXTK Keyboard 는 스스로 메시지를 받을 수 없어 배달이 필요하다.
    static void ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // 프레임당 정확히 1회 호출. "방금 눌렸다" 판정의 기준이 여기서 갱신된다.
    void Poll();

    // ★ 틱이 실제로 돈 뒤에 Game 이 호출한다. 붙잡아 둔 엣지 입력을 비운다.
    //
    //   왜 필요한가:
    //     엣지 입력("방금 눌림")은 Poll 시점에만 참이다. 그런데 프레임 정지 중이거나
    //     프레임이 아주 빨라 이번 프레임에 틱이 0 회 도는 경우, 그 입력을 아무도
    //     읽지 못하고 사라진다.
    //     그래서 Poll 에서 엣지를 누적해 두고, 틱이 실제로 돌았을 때만 비운다.
    //
    //   이 덕분에 "정지 → Space → 스텝" 순서로 공격을 처음부터 관찰할 수 있다.
    void ConsumeEdges();

    // ---- 지속 입력 (여러 틱에 걸쳐 여러 번 처리해도 되는 것) ----
    // ------------------------------------------------------------------------
    //  MoveX — 이동 의도. -1 ~ +1.
    //
    //    ★ 전에는 `MoveIntent { x, y }` 였다. 벨트스크롤이던 시절의 모양이고,
    //      길이가 1 을 넘으면 정규화했다.
    //
    //      플랫포머가 되면서 y 는 **아무것도 움직이지 않게** 되었는데
    //      정규화에는 계속 참여했다. 그래서 →와 ↑를 같이 누르면
    //      길이가 √2 가 되어 **가로 속도가 0.707 배로 떨어졌다.**
    //      「아무 일도 안 하는 입력이 속도를 훔쳐 가는」 상태였다.
    //
    //    ★★ 그래서 값을 고치지 않고 **개념을 지웠다.** 축이 하나뿐인 게임에
    //      2차원 벡터를 두면 언젠가 두 번째 축이 몰래 끼어든다.
    //
    //    ※ ↑↓ 는 나중에 **사다리**에 쓴다(§3.8.3). 그때는 이동 벡터가 아니라
    //      `CrouchHeld` 처럼 **지속 입력**으로 따로 받는다 — 오르내리기는
    //      「이동」이 아니라 「지형과의 상호작용」이기 때문이다.
    float MoveX() const;

    // ★ 웅크리기. **누르고 있는 동안** 참인 지속 입력이라 여기에 있다.
    //
    //   엣지(Edges)에 넣지 않는 이유:
    //     엣지는 「방금 눌렸다」라서 한 번만 소비되어야 하는 것들이다(공격·구르기).
    //     웅크리기는 「지금 누르고 있나」이므로 매 틱 물어봐도 되고,
    //     틱이 0회 도는 프레임이 있어도 잃어버릴 것이 없다.
    //
    //   ★ 플랫포머가 되면서 ↓ 가 비긴 했지만 Ctrl 을 유지한다(design.md §3.8.3).
    //     ↓ 는 나중에 **아래 발판으로 내려가기**에 쓸 자리로 남겨 둔다.
    bool CrouchHeld() const;                                  // Ctrl / 패드 LB

    // ---- 게임플레이 엣지 입력 ----
    //   Scene::Update 안(= 틱 안)에서 읽힌다. 그래서 ConsumeEdges 까지 붙잡아 둔다.
    //   메뉴용(Confirm/Cancel)과 게임플레이용(Attack)을 나눠 둔다.
    //   같은 물리 키를 쓰더라도 이름이 다르면 Scene 마다 의미가 분명해진다.
    //   메뉴용과 게임플레이용을 나눠 둔다. 같은 물리 키를 공유해도
    //   이름이 다르면 Scene 마다 의미가 분명해지고, 나중에 키 재설정이 쉽다.
    bool ConfirmPressed() const { return m_edges.confirm; }   // Enter / Space / 패드 A  (메뉴)
    bool CancelPressed()  const { return m_edges.cancel;  }   // Esc / 패드 B            (메뉴)

    // ★ Space 가 공격에서 **점프로** 옮겨 갔다(6-c-8).
    //   공격은 이미 좌클릭이 주 입력이라 자리를 비워 줄 수 있었다.
    //   패드도 같이 옮긴다 — A = 점프는 액션 게임의 관례다.
    //
    //   ※ 줍기는 **E**(상호작용)다. 한때 공격 키가 겸했는데, 그러면
    //     무기를 밟고 선 동안 휘두를 수가 없었다. Space 로 두지 않은
    //     이유는 그대로다 — **뛰어넘기만 해도 주워지면** 안 된다.
    //     그래서 E 도 **땅 위에서만** 듣는다.
    // ★★ 버튼이 **행동**이 아니라 **손**을 가리킨다(2026-09-15).
    //
    //   전에는 「좌클릭 = 공격 / 우클릭 = 물기」였다. 그러면 무기를 어느 손에
    //   들었든 조작이 같아서, **손이 둘이라는 사실이 조작에 안 나타났다.**
    //   §3.10 의 슬롯 구조(오른손 무기 · 왼손 보조)가 화면에만 있고
    //   손끝에는 없었던 셈이다.
    //
    //   이제 버튼이 손이고, **그 손에 무엇이 들려 있는지가 결과를 정한다** —
    //   무기가 있으면 휘두르고, 없으면(빈손이든 잘렸든) 문다.
    //   「무엇을 하는 버튼인가」를 Input 이 정하지 않는다.
    bool LeftHandPressed()  const { return m_edges.leftHand;  }   // 좌클릭 / 패드 X
    bool RightHandPressed() const { return m_edges.rightHand; }   // 우클릭 / 패드 Y
    bool JumpPressed()      const { return m_edges.jump;      }   // Space / 패드 A

    // ★ E — **상호작용.** 포탈로 넘어가고, 나중에 상자를 열고 사람과 말한다.
    //   「무엇을 하는가」는 **발밑에 무엇이 있는가**가 정한다 — 버튼이 손을
    //   가리키게 만든 것과 같은 발상이다(§3.2.1.1).
    bool InteractPressed()  const { return m_edges.interact;  }   // E / 패드 A(길게)
    bool RollPressed()    const { return m_edges.roll;    }   // Shift / 패드 B          (게임)
    bool PausePressed()   const { return m_edges.pause;   }   // Esc / 패드 Start        (게임)

    // ★ F2 는 디버그 키인데 **여기**에 있다.
    //   바꾸는 대상(장착 중인 방어구)이 게임 상태라서 틱 안에서 처리해야 하고,
    //   그러면 틱이 0회 도는 프레임에서 사라지므로 누적이 필요하다.
    //
    //   「어느 키인가」가 아니라 **「무엇을 바꾸는가」**로 자리가 정해진다.
    //     게임 상태를 바꾼다  -> 여기(누적 엣지). 틱 안에서 소비
    //     표시만 바꾼다       -> 아래(엔진 키). Game 이 프레임당 1회
    bool ArmorSwapPressed() const { return m_edges.armorSwap; }   // F2 (임시)

    // ★ F4 — 다리를 부러뜨렸다 되돌린다 (임시).
    //   엎드린 자세를 **바로** 만들어 판정 상자와 회피를 확인하기 위한 것이다.
    //   실제로 부러지려면 잡몹의 물기를 네 번 맞아야 해서 확인에만 한참 걸린다.
    bool LegBreakPressed() const { return m_edges.legBreak; }      // F4 (임시)

    // ★ F7 — **주손(오른팔)**을 부러뜨렸다 되돌린다 (임시).
    //   F4 와 같은 이유인데, 이쪽은 확인할 것이 하나 더 있다:
    //   **팔이 잘리면 무기를 떨군다**(§3.2.2). 그래서 이 키 하나로
    //   「떨군다 → E 로 다시 줍는다 → 반대 손에 들린다」가 전부 확인된다.
    //
    //   ★ 무기를 **일부러 버리는 키는 없다.** 버리기는 8단계 인벤토리의
    //     몫이고, 지금 무기가 땅에 떨어지는 경우는 **팔이 잘렸을 때뿐**이다.
    bool ArmBreakPressed() const { return m_edges.armBreak; }      // F7 (임시)

    // ★ F8 / F9 — 그 손에 카탈로그의 **다음 무기를** 든다 (임시).
    //   무기를 얻는 길이 아직 「떨어진 것을 줍기」뿐이라 두 번째 무기를
    //   손에 넣을 방법이 없다. 8단계 장비 화면이 오면 버린다 — F2(갑옷)와 같다.
    //
    //   ★★ **손마다 따로**다. 한 키뿐이면 두 손에 서로 다른 것을 들 수가
    //     없어서 「슬롯이 둘」이라는 것을 확인할 방법이 없다.
    bool WeaponSwapRightPressed() const { return m_edges.swapRight; }  // F8 (임시)
    bool WeaponSwapLeftPressed()  const { return m_edges.swapLeft;  }  // F9 (임시)

    // ★ F5 — 어둠 껐다 켜기.
    //   밝기는 **비교해 봐야** 정할 수 있다. 끄고 켤 수 없으면
    //   「지금이 어두운 건지 원래 그런 건지」를 알 수가 없다.
    bool DarkTogglePressed() const { return m_edges.darkToggle; }

    // ★ F6 — 무브셋(weapons.json)을 다시 읽는다.
    //   게임 상태(공격 수치)를 바꾸므로 **누적 엣지**다 —
    //   「어느 키인가」가 아니라 **「무엇을 바꾸는가」**로 자리가 정해진다.
    bool DataReloadPressed() const { return m_edges.dataReload; }

    // ---- 디버그 / 엔진 키 ----
    //   Game 이 틱 밖에서 프레임당 1회 읽는다. 붙잡아 둘 필요가 없다.
    bool DebugTogglePressed()  const;  // F1  — 히트박스 표시
    bool StatsTogglePressed()  const;  // F3  — FPS / 틱 오버레이
    bool FreezeTogglePressed() const;  // ,   — 프레임 정지
    bool StepPressed()         const;  // .   — 1 틱 전진
    bool SlowTogglePressed()   const;  // /   — 슬로우 모션

private:
    std::unique_ptr<DirectX::Keyboard> m_keyboard;
    std::unique_ptr<DirectX::GamePad>  m_gamePad;

    // Tracker 는 직전 프레임의 상태를 들고 있다가
    // 「지금 눌려 있다」와 「방금 눌렸다」를 구분해 준다.
    std::unique_ptr<DirectX::Mouse> m_mouse;

    DirectX::Keyboard::KeyboardStateTracker m_kbTracker;
    DirectX::GamePad::ButtonStateTracker    m_padTracker;
    DirectX::Mouse::ButtonStateTracker      m_mouseTracker;

    DirectX::Keyboard::State m_kb  = {};
    DirectX::GamePad::State  m_pad = {};
    DirectX::Mouse::State    m_ms  = {};

    // 틱이 돌 때까지 붙잡아 두는 게임플레이 엣지 입력
    struct Edges
    {
        bool confirm = false;
        bool cancel  = false;
        bool leftHand  = false;
        bool rightHand = false;
        bool jump      = false;
        bool interact  = false;
        bool roll      = false;
        bool pause   = false;

        // ★ 임시. 강인도(poise)가 경직을 막는 것을 눈으로 비교하기 위한 키.
        //   6단계에서 진짜 장비 시스템이 오면 버린다.
        bool armorSwap = false;
        bool legBreak  = false;
        bool armBreak  = false;
        bool swapRight = false;
        bool swapLeft   = false;
        bool darkToggle = false;
        bool dataReload = false;
    };
    Edges m_edges;
};
