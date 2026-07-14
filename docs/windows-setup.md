# Windows 설치 + Claude Desktop 연동 가이드

qBuildingDims 플러그인을 Windows에서 CloudCompare에 얹고, Claude Desktop이 이를
MCP로 호출하도록 연결하는 전체 절차.

---

## 1. CloudCompare 설치 (두 갈래)

### A. 공식 바이너리 (플러그인 없이 우선 확인용)
1. https://cloudcompare.org/release/ 에서 Windows 설치본 다운로드·설치.
2. 기본 경로: `C:\Program Files\CloudCompare\CloudCompare.exe`.
3. **주의**: 공식 바이너리에는 우리 `qBuildingDims`가 없다. `-BUILDING_DIMS`를 쓰려면
   아래 B(직접 빌드)가 필요하다.

### B. 플러그인 포함 직접 빌드 (필수 경로)
사전 준비:
- **Visual Studio 2019/2022** (Desktop C++)
- **CMake ≥ 3.10**
- **Qt 6** (예: `C:\Qt\6.7.2\msvc2019_64`)
- **Git**

```powershell
# 1) CloudCompare 소스 (submodule 포함)
git clone --recursive https://github.com/CloudCompare/CloudCompare.git
cd CloudCompare
git submodule update --init --recursive

# 2) 이 플러그인 배치
git clone <this-repo-url> C:\src\cloud-compare-plugin
xcopy /E /I C:\src\cloud-compare-plugin\plugin plugins\core\Standard\qBuildingDims

# 3) CMake 구성 (Qt 경로 + 플러그인 활성화)
mkdir build; cd build
cmake -DCMAKE_PREFIX_PATH="C:\Qt\6.7.2\msvc2019_64" `
      -DPLUGIN_STANDARD_QBUILDINGDIMS=ON `
      -DPLUGIN_IO_QLAS=ON `
      ..

# 4) 빌드
cmake --build . --config Release
```

빌드 산출물의 `CloudCompare.exe` 경로를 기록해 둔다(예: `...\build\qCC\Release\CloudCompare.exe`
또는 install 경로).

> `.las/.laz`를 다루려면 `PLUGIN_IO_QLAS=ON`(또는 qLASIO) 도 켜는 것을 권장.

---

## 2. 빌드 검증 체크리스트

이 MVP는 첫 컴파일에서 다음을 확인해야 한다(환경마다 심볼명이 다를 수 있음).

- [ ] `AddPlugin`이 `qCC_db`를 자동 링크하는지 — 아니면 `plugin/CMakeLists.txt`의
      `target_link_libraries`에 `QCC_DB_LIB` 추가.
- [ ] DXF 저장이 되는지 — CloudCompare가 `CC_DXF_SUPPORT` 정의로 빌드돼야 DXF 필터가 켜진다.
      비활성 시 `BuildingDims::exportDxf`가 에러를 반환한다.
- [ ] `FileIOFilter::SaveToFile(..., "_DXF Filter")` 의 필터 ID가 현재 버전과 일치하는지
      (`libs/qCC_io/src/DxfFilter.cpp`의 생성자 첫 문자열로 확인).
- [ ] `ccPointCloud::toGlobal3d / toLocal3d`, `setGlobalShift/Scale` 시그니처 확인
      (`libs/qCC_db/include/ccShiftedObject.h`).

CLI 스모크 테스트:

```powershell
& "C:\...\CloudCompare.exe" -SILENT -O sample.las `
    -BUILDING_DIMS -METHOD OBB -JSON out.json -DXF out.dxf
type out.json
```

---

## 3. MCP 브리지 설치

```powershell
cd C:\src\cloud-compare-plugin\mcp
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt

# 스모크 테스트
$env:CLOUDCOMPARE_BIN = "C:\...\CloudCompare.exe"
python server.py    # 정상 기동 시 stdio 대기 (Ctrl+C로 종료)
```

---

## 4. Claude Desktop 연동

1. Claude Desktop 설정 파일 위치:
   `%APPDATA%\Claude\claude_desktop_config.json`
2. `mcp/claude_desktop_config.example.json` 내용을 병합하고 경로를 실제 값으로 수정:

```json
{
  "mcpServers": {
    "cloudcompare-building-dims": {
      "command": "C:\\src\\cloud-compare-plugin\\mcp\\.venv\\Scripts\\python.exe",
      "args": ["C:\\src\\cloud-compare-plugin\\mcp\\server.py"],
      "env": {
        "CLOUDCOMPARE_BIN": "C:\\src\\CloudCompare\\build\\qCC\\Release\\CloudCompare.exe"
      }
    }
  }
}
```

> `command`는 venv의 `python.exe` 절대경로를 쓰는 것이 안전하다(전역 python 의존 회피).

3. Claude Desktop 재시작 → 🔌(도구) 아이콘에 `cloudcompare-building-dims`의
   `extract_building_dims` / `cloudcompare_info` 툴이 보이면 성공.

4. 대화 예시:
   - "`cloudcompare_info` 실행해서 연결 확인해줘"
   - "`C:\scans\bldg.las` 건물 규격 OBB로 뽑고 `C:\out\bldg.dxf`에 풋프린트 저장해줘"

---

## 트러블슈팅

| 증상 | 원인 / 조치 |
|---|---|
| 툴이 안 보임 | config 경로 오타, python 경로 오류. Claude Desktop 로그 확인 |
| `CloudCompare executable not found` | `CLOUDCOMPARE_BIN`를 빌드 산출 exe 절대경로로 지정 |
| JSON 없음 | 플러그인 미포함 바이너리 사용 중. B(직접 빌드) 필요 |
| DXF 실패 | `CC_DXF_SUPPORT` 미정의 빌드. CMake 옵션/의존성 확인 |
| las 로드 실패 | qLAS/qLASIO IO 플러그인 미빌드. `-DPLUGIN_IO_QLAS=ON` |
