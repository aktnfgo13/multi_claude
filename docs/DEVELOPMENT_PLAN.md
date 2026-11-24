# 개발 계획

## 목표
고음질 + 낮은 지연 + 효율적 연산 + 자유로운 파라미터 제어 + AI 기반 모델링 + 하드웨어 확장성.
우선순위: 음질 → 안정성 → 실시간 성능 → 파라미터 정확성 → 모듈화 → 하드웨어 이식성 → 사용 편의성 → AI 연구 확장성.

## 트랙 구조
* **제품 트랙**: DSP 코어 → 엔진 → AU/VST3/Standalone → UI → 프리셋 → NAM → 하드웨어.
* **연구 트랙**: 측정/SPICE → parity → DDSP → 노브 매핑 → Gray-box → Architecture Search.
* 두 트랙은 `mfx_core`에서만 만납니다. 연구 결과는 "새 이펙트"가 아니라 기존 블록의 구조/계수/LUT로 이전됩니다
  (예: D-010 WDF 이전, `ddsp_drive` + KnobLut).

## 우선순위와 진행 상황 (2026-11-24)

| 단계 | 항목 | 상태 |
|---|---|---|
| P0 | 아키텍처, DSP 코어, 엔진, Custom Drive, Gain/EQ/Dynamics, 기본 앰프, Delay/Reverb, IR 로더, Standalone, 파라미터, 프리셋, 벤치마크, 자동 테스트 | 완료(앰프는 단순화 모델) |
| P1 | AU/VST3, UI, NAM 로더, 모듈레이션, 라우팅, 오버샘플링, SPICE, parity, DDSP 프로토타입 | 완료 — **AU는 Mac 빌드 필요** |
| P2 | 실측 기반 DDSP, 노브 매핑, LUT 내보내기, Active Learning, 고급 앰프, 하드웨어 후보 평가 | 노브 매핑·LUT 완료(SPICE), 나머지 대기 |
| P3 | Gray-box, Architecture Search, 임베디드 최적화, 자체 AI 캡처 | 미착수 |

## 다음 마일스톤
1. M1 (Mac): AU 빌드 → auval → Logic에서 팩토리 프리셋 5종 재생, PDC 확인, CPU 측정.
2. M2 (측정): 오디오 인터페이스 루프백 보정 → Jan Ray 복각 노브 3–5점 측정 → `index.json` 데이터셋.
3. M3 (연구): 실측 데이터로 DDSP/NAM/Gray-box 비교, 결과를 EXPERIMENT_LOG에 추가.
4. M4 (제품): ADAA, NAM 리샘플러, 테일 유지, MIDI 매핑 UI, 프리셋 브라우저.
5. M5 (하드웨어): 후보 보드에서 `mfx_core` 크로스 빌드, 블록별 사이클 측정.

세부 상태표는 `docs/COMPLETION_REPORT.md`.
