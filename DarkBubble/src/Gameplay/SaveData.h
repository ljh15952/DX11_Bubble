// ============================================================================
//  SaveData.h
//    저장 파일 한 장의 모양 + 읽기 · 쓰기 (9).
//
//  ---- ★ 불러오기 = 마지막 화톳불에서 일어나기 ----
//    Hollow Knight 의 벤치와 같다. 껐다 켜면 **마지막으로 쉰 자리**에서 시작한다.
//    그래서 여기 들어가는 것은 「죽어도 남는 것」뿐이다(design.md §3.6.1 의 「남는다」).
//
//      저장한다        : 손 · 가방 · 방어구 · 지문 · 쉰 화톳불 · 열린 상자 · 떨어진 물건
//      저장하지 않는다 : 위치 · HP · 부위 · 적 → **이미 있는 부활 코드가** 되돌린다
//
//    ★ 오른쪽 칸을 안 적으므로 이 파일은 적도 몸 상태도 모른다.
//      칸이 적을수록 틀릴 곳도 적다.
//
//  ---- ★★ 새 게임 = 기본 저장 ----
//    시작 장비(단검 · 천 한 벌 · 初心者)가 PlayerController::Start 에 있다가
//    `NewGame()` 으로 왔다. 이제 새 게임도 이어하기도 **같은 길**
//    (PlayScene::ApplySave)을 지난다 — 초기화와 부활을 같은 함수로 만든 것과
//    같은 이유다. 두 벌이면 언젠가 「이어할 때만 이상하다」가 생긴다.
//
//  ---- ★ 이 파일은 Log 를 안 쓴다 ----
//    실패는 `error` 로 돌려주고 남기는 것은 부르는 쪽(Scene)이 한다.
//    그래서 tools/save_test.cpp 가 Json.cpp 하나만 더 붙여 컴파일된다.
// ============================================================================
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

// 땅에 떨어진 물건 하나.
//   ★ 위치까지 적는다. 동굴에 내려놓은 횃불을 저장하지 않으면 **영원히**
//     사라진다 — 그 상자는 이미 「열림」으로 저장되어 있으니까.
struct SavedDrop
{
    std::string map;    // 어느 맵에
    std::string item;   // items.json 의 키
    float       x = 0.0f;
    float       y = 0.0f;
};

struct SaveData
{
    // ---- 몸에 지닌 것 ----
    //   ★ **이름**(items.json 의 키)으로 적는다. 손 · 가방 · 땅이 이미 「이름
    //     하나가 곧 물건」이라(PlayerController 주석) 그대로 옮겨 적으면 된다.
    //   ★ 가방 · 방어구 · 지문은 칸 수를 여기서 정하지 않는다(vector). 가방이
    //     8칸이 되어도 옛 저장이 읽히고, 몇 칸까지 받을지는 PlayerController 가 정한다.
    std::array<std::string, 2> hands;   // [0] 오른손 · [1] 왼손 — PlayerController 와 같은 순서
    std::vector<std::string>   bag;     // ★ **칸 그대로.** 고정 칸이라 위치도 정보다
    std::vector<std::string>   armor;   // 자리는 물건이 안다(armorSlot). 순서는 보기용
    std::vector<std::string>   rings;

    // ---- 쉰 자리 ----
    //   ★★ 좌표가 아니라 **id** 다. 맵을 고치면 적어 둔 좌표가 낡아 허공이나
    //     벽 속에서 일어난다 — 포탈이 입구 **이름**을 가리키는 것(7-c)과 같은 이유.
    std::string restMap;     // 맵 이름
    std::string restPoint;   // 그 맵의 화톳불 id. 비었다 = 아직 아무 데서도 안 쉬었다(맵의 start)

    // ---- 세상 ----
    std::vector<std::string> openedChests;   // 「맵:id」 (PlayScene::ChestKey 와 같은 모양)
    std::vector<SavedDrop>   drops;

    // 새 게임. ★ 시작 장비가 **여기 하나**에만 있다.
    static SaveData NewGame();
};


namespace SaveIO
{
    // 판(version). ★ 형식이 바뀌면 올린다 — 모르는 판을 아는 척 읽으면
    //   칸이 엇갈린 채로 들어온다.
    inline constexpr int kVersion = 1;

    // ★ `assets/` 옆이다(작업 디렉터리 = 리포지토리 루트). git 에서는 뺀다(.gitignore).
    inline constexpr const wchar_t* kPath = L"saves/save.json";

    // ---- 글 ⇄ 저장 ----
    //   ★ 파일과 **따로** 둔다. PlayScene 은 「지난번에 쓴 글과 같은가」를
    //     **글로** 비교하고(1초마다), 테스트는 파일 없이 왕복을 본다.
    //   ★★ 그래서 **같은 상태면 같은 글**이어야 한다. 순서가 흔들리는 것을 안 쓰고,
    //     소수는 자릿수를 고정한다.
    std::string ToText(const SaveData& s);

    // ★ **빈 저장에서** 출발한다(NewGame 이 아니다). 파일에 없는 칸은 「없다」이지
    //   「시작 장비」가 아니다 — 카탈로그(기본값 위에 파일을 덮는다)와 반대다.
    //   저장은 설정이 아니라 **기록**이기 때문이다.
    //   실패하면 out 을 안 건드린다(다른 로더들과 같은 약속).
    bool FromText(std::string_view text, SaveData& out, std::string* error);

    // ★ 결과가 **셋**이라 bool 이 아니다. 「없다」와 「깨졌다」는 할 일이 다르다 —
    //   없으면 조용히 새 게임, 깨졌으면 옆으로 치우고(SetAside) 새 게임.
    enum class LoadResult { Ok, Missing, Broken };
    LoadResult Load(const wchar_t* path, SaveData& out, std::string* error);

    // ★★ 임시 파일에 **끝까지** 쓴 다음 한 번에 바꿔치기한다.
    //   그냥 덮어쓰다가 도중에 게임이 죽으면 반쯤 쓰인 파일이 남고, 그러면
    //   **저장 전체**를 잃는다. 바꿔치기는 중간이 없다 — 옛 파일이거나 새 파일이다.
    bool Write(const wchar_t* path, const std::string& text, std::string* error);

    // 깨진 파일을 `save.bad.json` 으로 옮긴다.
    //   ★ 지우지 않는다. 그대로 두면 새 게임의 첫 저장이 1초 뒤에 덮어써서
    //     **무엇이 깨졌는지** 영영 볼 수 없게 된다.
    bool SetAside(const wchar_t* path, std::string* error);
}
