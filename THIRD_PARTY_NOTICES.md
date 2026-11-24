# Third-party components and licence review

검토일: 2026-11-24. 법률 자문이 아닙니다 — 상용 출시 전 각 라이선스 원문(EULA)을 직접 확인하세요.

| 구성요소 | 사용 위치 | 라이선스 | 상업적 사용 | 재배포/수정 | 표기 | 소스 공개 의무 | 상태 |
|---|---|---|---|---|---|---|---|
| **JUCE 8.0.4** | `src/plugin`, `src/ui` (플러그인/UI만, 코어는 미사용) | AGPLv3 **또는** JUCE 8 상용 EULA (Starter/Indie/Pro) | 상용 EULA 필요(아래) | EULA 조건 | EULA 조건 | AGPLv3 선택 시 **전체 소스 공개** | ⚠ 출시 전 결정 필요 |
| VST3 SDK (JUCE 8.0.4 동봉 버전) | VST3 래퍼 | JUCE 8.0.4에 포함된 3.7.x는 GPLv3/Steinberg 계약 이중 라이선스. **VST 3.8(2025-10)부터 MIT** | 3.8+ MIT: 가능 | 가능 | MIT 표기 | 없음(3.8+) | ⚠ 3.8 이상 SDK를 쓰는 JUCE 버전으로 업그레이드 권장 |
| Apple AudioUnit SDK | AU 래퍼 (macOS) | Apple SDK 조건 | 가능 | — | — | 없음 | 일반적 |
| NeuralAmpModelerCore (commit d3189ed) | `src/neural` (선택 빌드) | MIT | 가능 | 가능 | 저작권 표기 필요 | 없음 | ✅ |
| nlohmann/json (NAM 동봉) | NAM 내부 | MIT | 가능 | 가능 | 표기 | 없음 | ✅ |
| Eigen 3.4 | NAM 내부 (헤더) | MPL-2.0 | 가능 | 가능 | 표기 | **수정한 Eigen 파일만** 공개 의무(파일 단위) | ✅ (수정 없음) |
| PyTorch, NumPy, SciPy, Matplotlib | 연구/도구(제품 미포함) | BSD 계열 | 가능 | — | — | 없음 | ✅ |
| ngspice | 연구 도구(제품 미포함, 외부 실행) | BSD-3-Clause(대부분) | — | — | — | 없음 | ✅ |
| pluginval | 검증 도구(제품 미포함) | GPLv3 | 도구로만 사용 | — | — | 제품에 링크하지 않음 | ✅ |

## JUCE 8 상용 라이선스 요약 (확인 필요)

* JUCE 모듈은 AGPLv3와 상용 JUCE 8 EULA의 이중 라이선스입니다(저장소 LICENSE.md).
* JUCE 8 출시 공지 기준 티어: Starter(무료), Indie($800 영구 / $40 월), Pro($3,500 영구 / $175 월).
  공지 당시 연 매출·투자 한도는 Starter $20,000, Indie $300,000, Pro 무제한이었고, 이후 포럼 공지에서 Indie 한도가
  변경되었다는 언급이 있습니다. **최신 수치는 EULA 원문으로 확인해야 합니다.**
* 개인 개발자의 경우 한도는 JUCE 기반 제품에서 발생한 수익 기준이라는 JUCE 팀 답변이 있습니다(포럼).
* 권장: 출시 전까지는 AGPLv3 조건(비공개 배포 금지)을 지키거나 Starter 티어로 등록, 매출 발생 후 티어 상향.

출처: JUCE 저장소 LICENSE.md, JUCE 포럼 "JUCE 8 Is Available Now" / "Amendments to the JUCE EULA for JUCE 8",
Steinberg VST 3 License 페이지 및 VST 3.8 보도자료(2025-10-29).

## 콘텐츠

* 제품 기본 콘텐츠로 **외부 NAM 캡처, IR, 오디오 파일을 포함하지 않았습니다.**
* `research/`의 SPICE 데이터와 DDSP 결과물은 이 프로젝트에서 직접 생성한 시뮬레이션 데이터입니다.
* NAM 예제 모델(NAM 저장소의 `example_models/`)은 테스트 실행에만 사용했고 저장소/제품에 포함하지 않았습니다.
* UI 자산은 코드로 그린 것으로 외부 이미지·폰트를 포함하지 않습니다(시스템 기본 폰트 사용).

## 특허·상표

* 사용한 알고리즘(RBJ 바이쿼드, Kaiser 하프밴드, 분할 컨볼루션, WDF, Wright-ω 다이오드 해, FDN 리버브, YIN)은
  공개 문헌 기법입니다. 특허 조사는 수행하지 않았습니다(출시 전 검토 권장).
* Fractal Audio, Neural DSP, Line 6, Universal Audio, Klon 등 상표는 코드·UI·프리셋 이름에 사용하지 않았습니다.
  "VST"/"Audio Units" 로고·명칭 사용 시 각 회사의 가이드라인을 따라야 합니다.
