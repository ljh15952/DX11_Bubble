# 인수 문서 — 다음 세션에서 여기서 이어가기

> 이 문서는 대화 세션이 바뀔 때 맥락을 잃지 않기 위한 것이다.
> 새 세션에서 **가장 먼저 이 파일과 `docs/design.md` 를 읽으면** 바로 이어갈 수 있다.
>
> 최종 갱신: 2026-09-10 / 커밋 `0340ef2` (5-e-3) + 전체 점검

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
| 경고 | **`/W4`** + `TreatAngleIncludeAsExternal` + `ExternalWarningLevel=TurnOffAllWarnings`. 내 코드는 /W4 로 보고 DirectXTK 헤더 경고는 끈다. **현재 Debug·Release 모두 경고 0개** — 새 경고가 나면 그건 내가 만든 것이다 |
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
| `assets/textures/enemy.png` | 384x256. 6열 x 4행 = idle(4) / crawl(4) / **swing(5) / bite(6)** |
| `assets/textures/font_8x14.png` | 128x84. ASCII 32~126, 8x14 셀, 16열 |
| `assets/textures/sheet.png` | 초기 진단용(**현재 미사용**. `test.png` 도 미사용 — 지울지는 미결정) |
| `assets/sounds/*.wav` | ui_confirm / ui_cancel / hit / swing / step — 전부 PowerShell 로 합성 |

### 에셋 생성 스크립트

| 스크립트 | 하는 일 |
|---|---|
| `tools/gen_enemy_attack.ps1` | `enemy.png` 의 공격 2행(swing / bite) 생성 |
| `tools/strip_frame_labels.ps1` | 시트 각 셀의 디버그 프레임 번호 제거. **지우기 전에 셀마다 검사하고, 그림 본체가 걸리면 중단한다** |

둘 다 **멱등**하다 — 몇 번 돌려도, 어느 순서로 돌려도 결과가 같다.
(`gen` 은 원본 2행만 옮기고, `strip` 은 이미 깨끗하면 아무것도 안 한다)

★ **초기 에셋(player.png · font · wav)의 생성 스크립트는 대화에만 있고 저장되지 않았다.**
다시 만들 수 없다. 5-e-3 부터는 `tools/` 에 남긴다. 새 에셋을 코드로 만들면 반드시 여기에 저장할 것.

`.ps1` 도 **UTF-8 BOM 이 필요하다** — 한국어 주석이 CP932 로 오독되면 파싱까지 깨진다.
C++ 파일과 같은 함정이다.

---

## 5. 조작

| 키 | 동작 |
|---|---|
| 방향키 / WASD / 좌스틱 | 이동 |
| Space / 패드 A | 공격 |
| Shift / 패드 B | 구르기 |
| Esc / 패드 Start | 일시정지 |
| **F1** | 히트박스 표시. **Game 이 프레임당 1회 처리** (플래그는 `Renderer::DebugDraw`) — 프레임 정지 중에도 즉시 듣는다 |
| **F2** | 갑옷 갈아입기 (CLOTH poise 10 ↔ PLATE poise 24). **임시** — 6단계에서 버린다 |
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
| 5-e-3 | 적 공격(swing / bite) + 예고 + 플레이어 HP + **강인도(poise)** + 피격 경직 + 넉백 + 무적 회피 + 머리 파괴 = 즉사 + 적 공격 애니메이션 2행 |
| 점검 | 전체 소스(34파일)를 훑어 버그 7건 수정. `/W4` 로 올림. 에셋의 디버그 번호 제거. 위 §8 에 함정 6개 추가 |

### 다음

| # | 내용 |
|---|---|
| **5-e-4** | **사망 → DeathScene → 부활** ← 여기서 이어간다. 기획서 3.6 의 「대사 테이블」 |
| 6 | JSON 데이터 외부화 + **무브셋**(웅크려 공격 등) |
| 7 | Room 그래프 맵 관리 |
| 8 | 콘텐츠 확장 |

**5-e-4 의 핵심**: 지금은 HP 0 에서 `YOU DIED` 를 찍고 입력만 멈춘다.
기획서 3.6 은 「`You died` 대신 부활하면서 멋진 대사, 한 줄 고정이 아니라 테이블」이므로
DeathScene + 진행도·사인(死因)별 대사 테이블이 들어간다.

### 5-e-3 에서 정해진 것 (design.md §3.1.1~3.1.3 / §3.2 참조)

| 항목 | 결정 |
|---|---|
| 경직 여부 | **강인도(poise) ≥ 충격력(impact)** 이면 경직 없이 데미지만. 문턱 방식 |
| 무적 처리 | **(b)** 무적인 틱은 없었던 일. 휘두르기를 소진시키지 않는다 (소진 = 나중의 패링) |
| 스턴락 방지 | **피격 무적(24) > 경직(18)**. 6틱의 여유가 필수 |
| 머리 파괴 | **즉사.** 시야 상실 + 청각 시스템은 폐기 |
| 기어다니는 적 | **무해하지 않다.** 사거리 짧고 후딜 긴 물어뜯기를 가진다 |
| 예고 | 애니메이션(누구나) + `!`(初心者の指輪) **두 겹** |

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
| 원형 거리로 사거리 판정 | 히트박스는 **가로로 뻗는다.** 위아래로 34px 떨어지면 거리는 사거리 안인데 상자가 안 닿아 적이 허공을 영원히 후려친다. 가로·세로를 따로 볼 것 |
| 「멈추는 거리」≠「공격 사거리」 | 두 숫자로 두면 「멈췄는데 못 때리는」 적이 생긴다. **같은 조건 하나**를 쓸 것 |
| 공격 종류를 매 틱 판정 | 휘두르는 도중에 다리가 부서지면 프레임 데이터가 통째로 바뀌어(60→54틱) active 를 건너뛴다. **시작 시점에 고정**(`attackIsBite`) |
| 피격 무적 == 경직 길이 | 경직이 풀리는 그 틱에 다시 맞는다 = 스턴락. **무적을 더 길게** |
| 고갈 경직 중 피격 | 경직이 풀려 **피격이 이득**이 된다. 들어오기 전 상태를 기억할 것(`m_stateBeforeHurt`) |
| 팔 테두리가 몸통 안에 | 도트 그림에서 1픽셀 검은 선은 「팔」이 아니라 「몸에 난 금」으로 읽힌다. 팔 끝을 몸통 테두리에 정확히 붙일 것 |
| 애니메이션과 판정 틱 불일치 | `startup / ticksPerFrame` 이 정수가 아니면 타격 그림과 판정이 어긋나 **플레이어가 학습할 수 없다.** 데이터를 2~3틱 반올림하는 편이 싸다 |
| ★ 조건을 「상태 목록」으로 쓰기 | `state == Crawl` 로 자세를 골랐더니, 상태를 하나(`Attack`) 추가한 순간 엎드린 적의 판정 상자가 공중에 떴다. **「몸의 성질」로 쓸 것** — `EnemyLegsBroken()`. 그러면 상태를 더 늘려도 다시 안 고친다 |
| ★ 디버그 키를 틱 안에서 읽기 | 엣지 입력은 틱이 0회 도는 프레임에서 사라진다. F1 이 **프레임 정지 중 100% 무시**되고 60Hz 초과 모니터에서 절반이 씹혔다. **바꾸는 대상으로 자리를 정한다** — 표시만 바꾸면 Game 이 프레임당 1회, 게임 상태를 바꾸면 누적 엣지 |
| 같은 일을 두 곳에서 | `UpdateMovement` 가 발소리 쿨다운을 채우고 `ChangeState(Run)` 이 0으로 되돌려, 달릴 때마다 발소리가 1/60초 간격으로 두 번 났다. 「살짝 두꺼운 한 번」으로 들려서 눈치채기 어렵다 |
| 에셋에 디버그 표시 굽기 | 초기 생성 스크립트가 각 셀에 프레임 번호를 그려 넣었고, 그것이 **게임 화면에 색 점으로 렌더**됐다. 원점이 발밑이라 캐릭터 왼쪽 위에 떴다. 디버그 표시는 코드로 그릴 것 |
| 크기 상수 흩어놓기 | `Window.cpp` 가 캔버스 크기를 `640, 360` 으로 하드코딩. 내부 해상도를 바꾸면 최소 창 크기만 옛 값으로 남는다. `Constants.h` 를 include 할 것 |
| 부호 없는 인덱스 계산 | `m_stack.size() - 1` 은 빈 스택에서 `SIZE_MAX`. 호출자가 막아 주더라도 **함수 자신이 안전해야** 세 번째 호출자가 생겼을 때 안 죽는다 |

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
