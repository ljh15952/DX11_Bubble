# 인수 문서 — 다음 세션에서 여기서 이어가기

> 이 문서는 대화 세션이 바뀔 때 맥락을 잃지 않기 위한 것이다.
> 새 세션에서 **가장 먼저 이 파일과 `docs/design.md` 를 읽으면** 바로 이어갈 수 있다.
>
> 최종 갱신: 2026-09-09 / 커밋 `bad58ae`

---

## 1. 이 프로젝트가 무엇인가

`D:\イジュンハ\3.DX\DX11_Bubble` — 개인 **학습용** 게임 프로젝트 **DarkBubble(가칭)**.

- 버블보블 같은 **고정화면 2D** 맵에서 싸우는 액션, 분위기는 **다크소울** 계열
- **외부 엔진을 쓰지 않는다.** Win32 + DirectX 11 + DirectXTK 로 직접 만든다
- 원격: https://github.com/ljh15952/DX11_Bubble

기획 내용은 **`docs/design.md`** 에 있다. 확정 사항과 미결정 사항이 구분되어 있다.

---

## 2. ★ 작업 방식 (중요 — 이걸 지켜야 한다)

| 항목 | 규칙 |
|---|---|
| **답변 언어** | **한국어 고정.** 사용자가 일본어로 써도 한국어로 답한다. 「英語でOK」같은 말은 게임 내 텍스트에 대한 지시이지 답변 언어가 아니다 |
| **진행 리듬** | **개념 설명 → 사용자가 「코드로」 → 구현 → 사용자 확인 → 커밋·푸시** |
| **한 번에 하나** | 2026-09-09 요청: 「一つづつゆっくり勉強しながら」. 한 턴에 시스템 여러 개를 묶지 말고 **작은 주제 하나씩**, 개념을 충분히 설명하며 |
| **커밋·푸시** | Claude 가 대신 하는 것을 선호한다. 커밋 메시지에 **왜 그렇게 했는지**를 남긴다 |
| **코드 통째 작성** | 원칙적으로 피하되, 보일러플레이트(Win32 창 등)나 사용자가 막힌 곳은 주석을 촘촘히 달아 제공한다 |

### ⚠️ UTF-8 BOM 필수

일본어 로캘 VS 는 BOM 없는 파일을 **CP932(Shift-JIS)** 로 읽는다. 한국어 주석이 깨질 뿐 아니라
**개행까지 먹혀 코드가 사라진다** (실제로 `CreateWindowExW` 인자 2개가 사라져
「関数に 10 個の引数を指定できません」에러가 났다).

`Write` 툴은 BOM 없이 쓰므로 **파일을 만들거나 고친 뒤 반드시** 이걸 실행한다:

```powershell
Get-ChildItem -LiteralPath "D:\イジュンハ\3.DX\DX11_Bubble\DarkBubble\src" -Recurse -File -Include *.cpp,*.h | ForEach-Object {
    $t = [System.IO.File]::ReadAllText($_.FullName, (New-Object System.Text.UTF8Encoding $false))
    [System.IO.File]::WriteAllText($_.FullName, $t, (New-Object System.Text.UTF8Encoding $true))
}
```

컴파일러 쪽은 프로젝트 옵션의 `/utf-8` 이 담당한다(이미 설정됨).

### 빌드 명령

```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
& $msbuild "D:\イジュンハ\3.DX\DX11_Bubble\DarkBubble\DarkBubble.vcxproj" /p:Configuration=Debug /p:Platform=x64 /v:minimal /nologo
```

빌드가 끝나면 사용자에게 **「VS 다시 로드 → F5」** 라고 알린다.

---

## 3. 환경

| | |
|---|---|
| VS | Professional 2026 (18.8.2), MSVC 14.51, PlatformToolset **v145** |
| SDK | Windows SDK 10.0.26100.0 |
| DirectXTK | NuGet `directxtk_desktop_win10` 2026.5.8.1 |
| 프로젝트 | x64 전용 / C++20 / SubSystem=Windows / `/utf-8` / `NOMINMAX` / `WIN32_LEAN_AND_MEAN` |
| 디버깅 작업 디렉터리 | `$(SolutionDir)` — 그래서 `assets/...` 상대 경로가 동작한다 |
| git 신원 | **`--local` 로만** `ljh15952 / ljh15952@gmail.com` (전역은 회사 계정이므로 건드리지 말 것) |
| 없는 것 | gh CLI, vcpkg |

---

## 4. 현재 코드 구조

```
DarkBubble/src/
├── main.cpp              엔트리포인트 + 디버그 콘솔(AllocConsole)
├── Core/
│   ├── Constants.h       캔버스 640x360, 창 x2, 틱 1/60, 프레임 상한 0.25초
│   ├── Window.h/.cpp     Win32 창. WndProc 을 GWLP_USERDATA 로 멤버 함수에 연결
│   ├── Game.h/.cpp       고정 타임스텝 루프, 시스템 소유, F3 오버레이
│   ├── Scene.h           Scene 인터페이스 + SceneContext
│   ├── SceneManager.h/.cpp  스택 + 지연 전환
│   ├── AABB.h            충돌 판정
│   └── Log.h/.cpp        콘솔 + VS 출력 창 동시 출력, 틱 번호 포함
├── Graphics/
│   ├── Renderer.h/.cpp   D3D11, 2패스 캔버스, SpriteBatch, 도형, 텍스트
│   ├── Camera.h/.cpp     뷰 행렬, 줌, 화면 흔들림
│   ├── Animation.h/.cpp  AnimationClip(데이터) + AnimationPlayer(재생 상태)
│   ├── BitmapFont.h/.cpp 폰트 시트에서 글리프를 잘라 그린다 (ASCII 전용)
│   └── Assets.h/.cpp     경로 키 텍스처 캐시
├── Audio/
│   └── Audio.h/.cpp      DirectXTK Audio. 이름 키 캐시, 장치 분실 복구
├── Input/
│   └── Input.h/.cpp      키보드/패드 → 의도. 엣지 입력을 틱까지 붙잡아 둔다
└── Scenes/
    ├── TitleScene.h/.cpp
    ├── PlayScene.h/.cpp  ★ 게임 내용 전부가 여기 (플레이어 + 적)
    └── PauseScene.h/.cpp
```

`SceneContext = { renderer, input, scenes, assets, audio, camera }` 가 **엔진의 표면적**이다.
Scene 은 이 여섯 개만 안다.

### 에셋 (전부 코드로 생성했다)

| 파일 | 내용 |
|---|---|
| `assets/textures/player.png` | 384x256. 6열 x 4행 = idle(4) / run(6) / attack(6) / roll(6) |
| `assets/textures/enemy.png` | 384x128. 6열 x 2행 = idle(4) / crawl(4) |
| `assets/textures/font_8x14.png` | 128x84. ASCII 32~126, 8x14 셀, 16열 |
| `assets/textures/sheet.png` | 초기 진단용(현재 미사용) |
| `assets/sounds/*.wav` | ui_confirm / ui_cancel / hit / swing / step — 전부 PowerShell 로 합성 |

스프라이트·음원 생성 스크립트는 대화에만 있고 저장돼 있지 않다. 다시 만들 일이 있으면
git log 의 해당 커밋 메시지를 참고하거나 새로 작성한다.

---

## 5. 조작

| 키 | 동작 |
|---|---|
| 방향키 / WASD / 좌스틱 | 이동 |
| Space / 패드 A | 공격 |
| Shift / 패드 B | 구르기 |
| Esc / 패드 Start | 일시정지 |
| **F1** | 히트박스 표시 |
| **F3** | FPS / TPS / 틱 / Scene / 텍스처 캐시 오버레이 |
| **`,`** | 프레임 정지 |
| **`.`** | 1틱 전진 |
| **`/`** | 슬로우 모션 (1/8) |

> **TPS 는 항상 60 이어야 한다.** 어긋나면 시간 처리가 깨진 것이다.
> 실제로 accumulator 를 두 번 더해 2배속이던 버그가 있었다.

---

## 6. 진행 상황

### 완료

| 단계 | 내용 |
|---|---|
| 1 | Win32 창 + D3D11 초기화 + 게임 루프 |
| 2 | SpriteBatch 2D 스프라이트 |
| 3 | 고정 타임스텝(accumulator) + 키보드/패드 입력 |
| 4 | 스프라이트시트 애니메이션 + AABB |
| 엔진 | Log / 저해상도 캔버스(2패스·레터박스·리사이즈) / Scene 스택 / BitmapFont / Assets 캐시 / Audio / Camera(흔들림) / 월드·UI 레이어 분리 / 프레임 스테핑 |
| 5-a | 상태 머신 (Idle / Run / Attack) |
| 5-b | 프레임 데이터 (startup / active / recovery) + 공격 히트박스 |
| 5-c | 스태미나 + 고갈 경직 (Exhausted) |
| 5-d | 구르기 + 무적 프레임 (windup / invincible / recovery) |
| 5-e-1 | 적 + 부위 파괴 (머리 20 / 몸통 100 / 다리 40) |
| 5-e-2 | 적 상태 머신 + 추격 + 다리 파괴 시 기어오기 + 자세별 판정 상자 |

### 다음

| # | 내용 |
|---|---|
| **5-e-3** | **적 공격 + 플레이어 HP + 무적 프레임으로 회피** ← 여기서 이어간다 |
| 5-e-4 | 사망 → DeathScene → 부활 |
| 6 | JSON 데이터 외부화 + **무브셋**(웅크려 공격 등) |
| 7 | Room 그래프 맵 관리 |
| 8 | 콘텐츠 확장 |

**5-e-3 의 핵심**: 적 공격도 `startup/active/recovery` 를 쓰고, 그 `active` 구간이
플레이어의 `invincible` 구간과 겹치면 회피 성공. 5-d 의 무적 프레임이 드디어 의미를 갖는다.

---

## 7. ★ 이미 내린 설계 결정 (다시 논의하지 말 것)

`docs/design.md` 에 상세히 있다. 요약:

| 항목 | 결정 |
|---|---|
| 내부 해상도 | **640x360 을 x2 확대.** 좌표·속도·히트박스는 전부 640x360 기준 |
| 전투 | **완전 스태미나제**(다크소울식) |
| 스태미나 고갈 | **방식 B** — 부족해도 행동이 나가고, 0 미만이면 경직 |
| 부위 파괴 | **핵심 시스템.** 몸통=격파, 다리=이동 불가, 머리=시야 상실 |
| 다리 | 좌/우로 나누지 않고 **하나** |
| 부위 선택 | **겹침 면적이 가장 큰 부위**에 데미지 |
| 주인공 | **1명** (마을 유일 생존자인 아이). 스타팅 캐릭터 복수안은 폐기 |
| 지문 슬롯 | **2개** (기회비용을 만들기 위해) |
| 맵 | **고정화면 방들을 그래프로 연결** (카메라 불필요) |
| 서사 전달 | **컷신 없음.** 환경·기도문·임종대사·환영으로 |
| 상태 머신 | **enum + switch.** 상태는 6개에서 안 늘어남. 다양성은 데이터에서 |
| 상태 길이 | **프레임 데이터가 정한다.** 애니메이션의 `Finished()` 로 판정하지 않는다 |

---

## 8. 이미 밟은 함정 (반복하지 말 것)

| 함정 | 내용 |
|---|---|
| BOM 없는 UTF-8 | CP932 로 오독 → 개행까지 먹혀 코드가 사라진다 |
| `/utf-8` 없이 좁은 문자열 | 실행 문자셋이 CP932 라 한국어가 컴파일 시점에 깨진다 |
| `DrawText` | `windows.h` 가 매크로로 정의 → `DrawTextW` 로 치환된다. `DrawString` 으로 명명 |
| `min`/`max` | `NOMINMAX` 필수 (이미 설정됨) |
| accumulator 이중 가산 | 리팩터링에서 옮길 줄을 지우지 않고 복사 → 2배속 |
| `m_playerAnim.Tick()` 누락 | 리팩터링에서 옮기지 않음 → 애니메이션 정지 + Attack 에서 못 빠져나옴 |
| 프레임 경계 D3D 해저드 | `SpriteBatch::End()` 가 SRV 를 풀지 않아 다음 프레임 RTV 바인딩과 충돌. `EndFrame` 끝에서 `PSSetShaderResources(nullptr)` |
| `ResizeBuffers` 전 참조 | RTV 와 **컨텍스트 바인딩** 둘 다 놓아야 한다 |
| 엣지 입력 소실 | 틱이 0회 도는 프레임에서 사라진다. `Input` 이 `ConsumeEdges` 까지 붙잡아 둔다 |
| `SceneManager::Replace` | 먼저 비우고 Enter 가 실패하면 게임이 조용히 종료. 새 Scene 을 먼저 올려 본다 |
| `AnimationPlayer::Play` 비교 | 모든 필드를 비교해야 한다. row/frameCount 만 보면 속도 변경이 무시된다 |
| AABB 좌우 반전 | `facing = -1` 이면 `left > right` 가 되어 `Intersects` 가 절대 참이 안 된다. min/max 정규화 필수 |
| 소수 좌표로 그리기 | 시트의 옆 칸을 물어와 1픽셀 선이 생긴다. **계산은 소수, 그리기는 정수** |
| 부위 파괴 중복 히트 | active 3틱이면 데미지 3번. 「이번 휘두르기에 이미 맞췄나」 표시 필수 |
| 자세별 판정 상자 | 엎드렸는데 상자가 서 있는 위치면 공중에 뜬다. 자세마다 상자를 따로 |

**「정수로 맞춘다」는 픽셀아트의 반복 규칙**: 화면 확대(정수 배율) / 폰트 좌표 / 카메라 흔들림 오프셋 / 스프라이트 위치.

---

## 9. 리팩터링 시 체크리스트

2배속 버그와 애니메이션 정지 버그가 **둘 다 큰 리팩터링에서 한 줄이 어긋난** 경우였다.
문법이 멀쩡해 컴파일러가 못 잡는다.

> **옮기기 전후의 호출 목록을 `Grep` 으로 비교한다.**
>
> ```
> before:  Play / Tick / Finished / SourceRect
> after:   Play / Finished / SourceRect      ← Tick 이 없다
> ```
>
> 30초면 잡힌다. 빌드 성공만 보고 넘어가지 말 것.
