---
name: test
description: Catch2 유닛 테스트를 빌드하고 실행하여 결과를 보고합니다.
allowed-tools: Bash, Read, Grep, Glob
---

## 테스트 실행

### 1단계: 테스트 빌드

```bash
cmake --build build --target unit-tests
```

빌드 실패 시 에러를 파싱하여 보고하고 중단한다.

### 2단계: 테스트 실행

```bash
cd build && ctest --output-on-failure
```

$ARGUMENTS에 필터가 있으면 특정 테스트만 실행:

```bash
# Catch2 태그 필터
cd build && ctest --output-on-failure -R "TestName"
```

타임아웃: 2분.

### 3단계: 결과 파싱

**성공 시**: "전체 테스트 통과 (N개)" 한 줄 출력.

**실패 시**: 실패한 테스트를 테이블로 출력한다.

```
## 테스트 실패

| # | 테스트명 | 에러 메시지 |
|---|---------|------------|
| 1 | CodecTest::OversizedMessage | REQUIRE failed: decoded.has_value() |

통과: X개 / 실패: Y개 / 건너뜀: Z개
```
