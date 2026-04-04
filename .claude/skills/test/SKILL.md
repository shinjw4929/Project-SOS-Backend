---
name: test
description: Catch2 유닛 테스트를 빌드하고 CTest로 실행하여 결과를 보고합니다.
allowed-tools: Bash, Read, Grep, Glob
---

## 테스트 실행

$ARGUMENTS 파싱:
- 인자 없음: 전체 테스트 실행
- 테스트 필터 (예: `CodecTest`, `Config`): 해당 필터만 실행

### 1단계: 테스트 빌드

```bash
cmake --build build --target unit-tests
```

빌드 실패 시 에러를 파싱하여 보고하고 중단한다.

### 2단계: 테스트 실행

```bash
# 전체 테스트
cd build && ctest --output-on-failure

# 특정 테스트 필터
cd build && ctest --output-on-failure -R "<필터>"
```

- timeout: 120000ms (2분)
- `run_in_background`로 실행

### 3단계: 결과 파싱

CTest 출력에서 결과를 파싱한다:
- 전체 테스트 수, 통과, 실패 카운트 추출
- 실패한 테스트의 이름과 Catch2 에러 메시지 추출

### 4단계: 결과 출력

- **전체 통과**: "테스트 통과 (N개)" 한 줄 출력
- **실패 있음**:
  ```
  테스트 결과: N개 통과 / M개 실패

  실패한 테스트:
  | # | 테스트명 | 에러 메시지 |
  |---|---------|-----------|
  | 1 | ...     | ...       |
  ```

### 주의사항

- Developer Command Prompt for VS 2022 환경이 필요하다.
- 테스트 빌드가 먼저 성공해야 실행 가능하다.
