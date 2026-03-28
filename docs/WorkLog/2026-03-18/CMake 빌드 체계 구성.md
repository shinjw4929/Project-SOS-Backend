# CMake 빌드 체계 구성

> Phase 1 - Step 1-2. Protobuf 스키마(1-1) 완료 후 빌드 시스템 구축.

---

## 개요

C++ 서버(Room, Chat)와 공유 라이브러리(common)를 빌드하기 위한 CMake + vcpkg 기반 빌드 체계를 구성했다.
Protobuf `.proto` 파일에서 C++ 코드를 자동 생성하는 protoc 코드젠 파이프라인을 포함한다.

---

## 도구 환경

| 도구 | 버전 | 설치 방법 | 비고 |
|------|------|----------|------|
| CMake | 4.2.3 | `winget install Kitware.CMake` | PATH 자동 등록 |
| Ninja | - | `winget install Ninja-build.Ninja` | 빌드 제너레이터 |
| vcpkg | 2026-03-04 | `git clone` → `bootstrap-vcpkg.bat` | `C:\vcpkg` |
| MSVC | 19.44 (14.44.35207) | Visual Studio 2022 Community | C++20 컴파일러 |
| protoc | 33.4.0 | vcpkg protobuf 패키지에 포함 | 별도 설치 불필요 |

### 환경변수

| 변수 | 값 | 범위 |
|------|-----|------|
| `VCPKG_ROOT` | `C:\vcpkg` | User |

---

## 파일 구조

```
Project-SOS-Backend/
├── CMakeLists.txt            # 루트: C++20, subdirectory 등록
├── CMakePresets.json         # vcpkg 툴체인 + Ninja 프리셋
├── vcpkg.json                # 의존성 매니페스트
├── proto/
│   ├── room.proto            # → build/src/common/generated/room.pb.{h,cc}
│   └── chat.proto            # → build/src/common/generated/chat.pb.{h,cc}
├── src/
│   ├── common/
│   │   └── CMakeLists.txt    # sos_common STATIC (protoc 코드젠)
│   ├── room/
│   │   ├── CMakeLists.txt    # room-server 실행 파일
│   │   └── main.cpp
│   └── chat/
│       ├── CMakeLists.txt    # chat-server 실행 파일
│       └── main.cpp
└── build/                    # (gitignore) 빌드 산출물
    └── src/common/generated/ # protoc 생성 코드
```

---

## CMake 타겟 구조

```
sos_common (STATIC)
├── protobuf::libprotobuf
├── room.pb.cc / room.pb.h   (protoc 자동 생성)
└── chat.pb.cc / chat.pb.h   (protoc 자동 생성)

room-server (EXECUTABLE)
├── sos_common
└── spdlog::spdlog

chat-server (EXECUTABLE)
├── sos_common
└── spdlog::spdlog
```

---

## vcpkg 의존성

```json
{
  "dependencies": [
    "protobuf",       // Protobuf 직렬화 + protoc 컴파일러
    "spdlog",         // 구조화된 로그 출력
    "nlohmann-json"   // 설정 파일 파싱 (Config 클래스용)
  ]
}
```

Phase 2~3에서 추가 예정: `boost-asio`, `hiredis`, `redis-plus-plus`, `catch2`

---

## Protobuf 코드젠

`src/common/CMakeLists.txt`에서 `add_custom_command`로 protoc를 실행한다.

```
proto/room.proto → protoc --cpp_out → build/src/common/generated/room.pb.{h,cc}
proto/chat.proto → protoc --cpp_out → build/src/common/generated/chat.pb.{h,cc}
```

- protoc 바이너리: `build/vcpkg_installed/x64-windows/tools/protobuf/protoc.exe` (vcpkg 제공)
- CMake 타겟 `protobuf::protoc`를 사용하므로 경로 하드코딩 없음
- `.proto` 파일 변경 시 자동 재생성 (`DEPENDS` 설정)

---

## 빌드 방법

### CLI (Developer Command Prompt for VS 2022)

```bash
cmake --preset=default     # Configure (vcpkg 패키지 자동 설치)
cmake --build build        # Build (Ninja)
```

### CLion

1. Settings > Build, Execution, Deployment > Toolchains
2. `+` > Visual Studio 선택 → VS 2022 Community 자동 감지
3. 기본 툴체인으로 설정
4. CMake 프리셋 `default`가 자동으로 로드됨

---

## 컴파일러 선택: MSVC

MinGW(g++ 15.2.0)도 시스템에 설치되어 있으나 MSVC를 선택했다.

| 기준 | MSVC | MinGW |
|------|------|-------|
| vcpkg 호환 | 기본 triplet (`x64-windows`) | 별도 triplet 필요 (`x64-mingw-static`) |
| 패키지 테스트 범위 | vcpkg 메인 CI | 일부 패키지 미지원/불안정 |
| 빌드 환경 | Developer Command Prompt 필요 | PATH에 있으면 즉시 사용 |

vcpkg 생태계가 MSVC 기준으로 동작하므로 안정성 우선으로 MSVC를 선택했다.
CLI 빌드 시 Developer Command Prompt에서 실행해야 하는 제약이 있으나, CLion에서는 Visual Studio 툴체인 설정으로 자동 처리된다.

---

## 빌드 결과

```
[1/9] Generating C++ from chat.proto
[2/9] Generating C++ from room.proto
[3/9] Building CXX object src/room/CMakeFiles/room-server.dir/main.cpp.obj
[4/9] Building CXX object src/chat/CMakeFiles/chat-server.dir/main.cpp.obj
[5/9] Building CXX object src/common/CMakeFiles/sos_common.dir/generated/chat.pb.cc.obj
[6/9] Building CXX object src/common/CMakeFiles/sos_common.dir/generated/room.pb.cc.obj
[7/9] Linking CXX static library src/common/sos_common.lib
[8/9] Linking CXX executable src/room/room-server.exe
[9/9] Linking CXX executable src/chat/chat-server.exe
```

실행 확인:
```
> build\src\room\room-server.exe
[2026-03-18 21:31:57.961] [info] Room Server starting...

> build\src\chat\chat-server.exe
[2026-03-18 21:31:59.155] [info] Chat Server starting...
```
