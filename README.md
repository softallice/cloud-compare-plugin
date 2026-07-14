# qBuildingDims — CloudCompare 건물 규격 추출 플러그인 + MCP 브리지

포인트클라우드에서 건물의 **가로·세로·높이**를 추출하고, **DXF(CAD) 풋프린트**로 내보내며,
**Claude Desktop 등 AI 에이전트**와 대화하며 사용할 수 있는 [CloudCompare](https://cloudcompare.org) 플러그인.

두 개의 구성요소로 이루어진다.

| 구성요소 | 위치 | 역할 |
|---|---|---|
| **플러그인 (C++)** | `plugin/` | CloudCompare Standard 플러그인. GUI 액션 + `-BUILDING_DIMS` CLI 커맨드. 규격 계산 → JSON + DXF |
| **MCP 브리지 (Python)** | `mcp/` | CloudCompare CLI를 감싸 Claude Desktop과 연동. `extract_building_dims` 툴 제공 |

```
Claude Desktop ──MCP──▶ mcp/server.py ──CLI──▶ CloudCompare(-SILENT -BUILDING_DIMS)
                                                      └─ plugin/qBuildingDims
                                                         → dims JSON + footprint DXF
```

## 동작 방식

- **높이(height)** = Z 범위
- **가로/세로(length/width)** = XY 풋프린트의 **OBB(방향성 경계상자, PCA)** 로 산출 (축정렬 `AABB`도 선택 가능)
- **좌표 정밀도** = Global Shift/Scale를 반영해 실측 좌표로 환산 (측량 좌표 대응)
- 결과 JSON 스키마:

```json
{
  "source": "building.las",
  "ok": true,
  "unit": "m",
  "method": "OBB",
  "dimensions": { "length": 24.31, "width": 11.87, "height": 9.42 },
  "footprint": [[x,y],[x,y],[x,y],[x,y]],
  "center": [cx, cy, cz],
  "global_shift": [sx, sy, sz],
  "global_scale": 1.0,
  "point_count": 1843221,
  "warnings": []
}
```

## 빌드 (플러그인)

플러그인은 CloudCompare 소스 트리 안에서 빌드된다. 이 레포의 `plugin/` 디렉터리를
CloudCompare의 `plugins/core/Standard/qBuildingDims`로 넣는다(복사 또는 git submodule).

```bash
# 1) CloudCompare 소스 준비 (submodule 필수)
git clone --recursive https://github.com/CloudCompare/CloudCompare.git
cd CloudCompare
git submodule update --init --recursive      # CCCoreLib 등

# 2) 이 플러그인을 트리에 배치
cp -r /path/to/cloud-compare-plugin/plugin plugins/core/Standard/qBuildingDims
#   또는 submodule로:
#   git submodule add <this-repo-url> plugins/core/Standard/qBuildingDims-src
#   (그 경우 plugin/ 내용을 해당 위치에 두도록 구조 조정)

# 3) 빌드 (플러그인 활성화)
mkdir build && cd build
cmake -DPLUGIN_STANDARD_QBUILDINGDIMS=ON ..     # Windows는 -DCMAKE_PREFIX_PATH=C:\Qt\6.x\...
cmake --build . --config Release
```

> **Windows 상세 설치 + Claude Desktop 연동**은 [`docs/windows-setup.md`](docs/windows-setup.md) 참고.

## CLI 사용법

```bash
CloudCompare -SILENT -O building.las \
  -BUILDING_DIMS -METHOD OBB -UNIT m -JSON out.json -DXF out.dxf
```

| 서브옵션 | 의미 |
|---|---|
| `-METHOD OBB\|AABB` | 풋프린트 계산 방식 (기본 OBB) |
| `-UNIT <str>` | 결과 단위 라벨 (기본 m) |
| `-JSON <path>` | 규격 JSON 출력 경로 |
| `-DXF <path>` | 풋프린트 DXF 출력 경로 (CloudCompare 폴리라인) |
| `-PLANES` | 벽·바닥·지붕 평면 + 층수/층고 분석(L2)을 JSON에 추가 |
| `-OPENINGS` | 창문·문 개구부 탐지(L3) → `openings[]` JSON + 입면도 OPENINGS 레이어 |
| `-PLAN <path>` | **평면도** DXF 생성 (풋프린트 + 벽 세그먼트 + 치수선). 평면 분석 자동 수행 |
| `-ELEV <path>` | **입면도** DXF 생성 (벽별 사각형 + 가로/높이 치수선). 평면 분석 자동 수행 |

로드된 모든 클라우드에 대해 순차 적용된다. 평면도/입면도는 자체 DXF 라이터로
레이어(FOOTPRINT/WALLS/DIMENSIONS/TEXT)와 치수선·텍스트를 직접 생성한다.

예:
```bash
CloudCompare -SILENT -O bldg.las -BUILDING_DIMS \
  -JSON out.json -PLAN plan.dxf -ELEV elev.dxf
```

## MCP 브리지 사용법

```bash
cd mcp
pip install -r requirements.txt
CLOUDCOMPARE_BIN="C:/Program Files/CloudCompare/CloudCompare.exe" python server.py
```

Claude Desktop 설정(`claude_desktop_config.json`)에 `mcp/claude_desktop_config.example.json` 내용을 병합.
이후 Claude에게 이렇게 요청할 수 있다:

> "이 las 파일에서 건물 규격 뽑아줘: C:\scans\bldg.las, 그리고 풋프린트를 out.dxf로 저장해줘"

L4 의미 해석은 `interpret_building` 툴이 측정→해석을 한 번에 수행하고,
`interpret_building` 프롬프트로 Claude가 더 깊게 추론한다:

> "C:\scans\bldg.las 분석하고 창문/문 종류랑 건물 용도까지 해석해줘"

## 라이선스

CloudCompare와 링크·배포되므로 **GPL-3.0-or-later**. 자세한 내용은 [`LICENSE`](LICENSE).
클로즈드 소스로 재배포할 수 없다.

## 상태 / 로드맵

- [x] **L1** MVP: OBB/AABB 규격, JSON, DXF 풋프린트, `-BUILDING_DIMS` CLI, MCP 브리지
- [x] **L2** 벽/바닥/지붕 평면(자체 RANSAC) + 층수/층고 분석 (`-PLANES`)
- [x] **도면** 평면도/입면도 + 치수선 (자체 DXF 라이터, `-PLAN`/`-ELEV`)
- [x] **L3** 창문·문 개구부 탐지 (벽 평면 점유격자 → 프레임된 void, 문/창 휴리스틱) (`-OPENINGS`)
- [x] **L4** 의미 해석 — 규칙 기반 1차 분류(문/창/차고/점포) + fenestration 패턴 + 용도 가설 + Claude 추론 프롬프트 (MCP `interpret_building`)
- [ ] 지면 제거(qCSF) 연동, DXF DIMENSION 연관 엔티티, 상시 세션(qJSonRPCPlugin)

> **주의**: 이 코드는 Windows/Qt6 환경에서의 첫 빌드 검증이 필요한 MVP 스캐폴드다.
> 첫 빌드 시 확인할 지점은 `docs/windows-setup.md`의 "빌드 검증 체크리스트" 참고.
