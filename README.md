# DarkBubble (가칭)

버블보블 같은 **고정화면 2D 맵**에서 싸우는 액션 게임. 분위기·컨셉은 다크소울 계열.
외부 게임 엔진을 쓰지 않고 Win32 + DirectX 11 로 직접 만드는 **학습용 프로젝트**입니다.

## 기술 스택

| 항목 | 내용 |
|---|---|
| 언어 | C++20 (MSVC v145) |
| 개발 환경 | Visual Studio 2026 |
| 그래픽 API | DirectX 11 |
| 보조 라이브러리 | DirectXTK (`directxtk_desktop_win10`, NuGet) |
| 데이터 | JSON (무기·적 데이터 외부화) |
| 플랫폼 | Windows x64 |

## 빌드 방법

1. `DarkBubble.slnx` 를 Visual Studio 2026 으로 연다
2. 솔루션 우클릭 → **NuGet 패키지의 복원** (`packages/` 는 저장소에 없음)
3. 구성 `Debug` / 플랫폼 `x64` 로 빌드

디버깅 시 작업 디렉터리는 `$(SolutionDir)` 로 설정되어 있어, 코드에서 `"assets/..."`,
`"data/..."` 같은 상대 경로를 그대로 쓸 수 있습니다.

## 폴더 구성

```
DX11_Bubble/
├── DarkBubble.slnx
├── DarkBubble/
│   ├── DarkBubble.vcxproj
│   └── src/            소스 (src 가 include 경로에 등록되어 있음)
├── assets/             textures, sounds, shaders
└── data/               weapons.json, enemies.json
```

## 로드맵

- [ ] 1. Win32 윈도우 생성 + D3D11 디바이스/스왑체인 초기화 (Hello Window)
- [ ] 2. DirectXTK `SpriteBatch` 로 2D 스프라이트 그리기
- [ ] 3. 고정 타임스텝 게임 루프 + 키보드/게임패드 입력
- [ ] 4. 스프라이트시트 애니메이션 + AABB 충돌 판정
- [ ] 5. 전투 시스템 (상태머신: Idle/Attack/Roll/Hurt, 프레임 데이터 기반 히트박스)
- [ ] 6. 무기·적 데이터 JSON 외부화 (데이터 주도 설계)
- [ ] 7. Room 단위 맵 관리 시스템
- [ ] 8. 콘텐츠 확장 (무기 종류, 적 패턴, 방어구, 마법)
