# 개발 계획

## 목표
고음질 + 낮은 지연 + 효율적 연산 + 자유로운 파라미터 제어 + AI 기반 모델링 + 하드웨어 확장성.
우선순위: 음질 → 안정성 → 실시간 성능 → 파라미터 정확성 → 모듈화 → 하드웨어 이식성 → 사용 편의성 → AI 연구 확장성.

## 트랙 구조
* **제품 트랙**: DSP 코어 → 엔진 → AU/VST3/Standalone → UI → 프리셋 → NAM → 하드웨어.
* **연구 트랙**: 측정/SPICE → parity → DDSP → 노브 매핑 → Gray-box → Architecture Search.
* 두 트랙은 `mfx_core`에서만 만납니다. 연구 결과는 "새 이펙트"가 아니라 기존 블록의 구조/계수/LUT로 이전됩니다
  (예: D-010 WDF 이전, `ddsp_drive` + KnobLut).

## 필수 요구사항 (2026-11-24 사용자 지시, 상세 규칙은 `AGENTS.md`)
모든 개발에서 유지합니다: R1 모듈형 아키텍처 / R2 JUCE·OS 독립 C++ DSP Core / R3 macOS·Windows 공통 DSP와 프리셋 호환 /
R4 ARM 하드웨어 이식성 / R5 오디오 콜백 실시간 안전성·메모리 관리 / R6 새 Amp·Effect 추가 시 기존 모델·프리셋 보존.

### 필수 요구사항 대비 현황 (2026-11-24 점검, 재설계 없이 차이만 확인)

| 요구 | 현재 상태 | 차이(Gap) | 관리 ID |
|---|---|---|---|
| R1 모듈형 | `Effect` 인터페이스 + `EffectFactory` 등록, 엔진·플러그인에 이펙트별 분기 없음, 공용 블록 재사용 | 없음 | — |
| R2 Core 독립 | core/dsp/engine/presets에 JUCE·OS 헤더 0건, 스레드/뮤텍스 0건. x86 intrinsic은 `#if` 격리 | 없음 | — |
| R3 OS 공통·프리셋 | 단일 DSP 소스, 프리셋 JSON 물리값 저장, 미지 항목은 경고 후 무시 | Windows 빌드·동일 출력 미검증, 플랫폼 간 렌더 일치 테스트 없음 | K-015 |
| R4 ARM 이식 | aarch64 FTZ 지원, 오디오 경로 float, 할당은 prepare 한정 | ① ARMv7/Cortex-M의 FTZ(FPSCR) 경로 없음 ② 블록별 메모리 사용량 측정·보고 없음 ③ NAM(Eigen·예외·힙)과 JSON 파서(std::string)는 Cortex-M에 부적합(Linux ARM은 가능) ④ 연구용 다이오드 클리퍼 일부가 샘플당 double | K-016, K-017, K-018 |
| R5 실시간 안전 | 오디오 스레드 할당 0, 락 없음, seqlock·RtPublisher·그레이브야드, TSan/ASan 경고 0 | 없음(Linux 기준). Mac/Windows 호스트에서 미확인 | K-004 |
| R6 모델·프리셋 보존 | type/param id 안정 규칙, 팩토리 프리셋 로드 테스트, `customDriveMappingV1` 코드 보존 | ① 이펙트별 **모델 버전 필드가 없어** Custom Drive v2가 기존 프리셋 소리를 바꿈(v1 경로는 선택 불가) ② 프리셋 **렌더 회귀(골든 출력) 테스트 없음** ③ 앰프 Model(Choice) 항목 추가 시 호스트 자동화 정규화 값이 바뀜 | K-019, K-020, K-021 |

후속 조치(별도 승인 후 진행): 프리셋 블록에 `model_version` 필드 추가와 v1 매핑 선택 경로, 팩토리 프리셋 골든 렌더 테스트,
Choice 파라미터 확장 정책, 블록별 메모리 보고, ARMv7 FTZ 경로. 모두 기존 구조 안에서 추가 가능한 변경이며 아키텍처 재설계가 필요하지 않습니다.

## 우선순위와 진행 상황 (2026-11-24 통합 고도화 후)

| 단계 | 항목 | 상태 |
|---|---|---|
| P0 | 보존(스냅샷·태그·브랜치), Codex 비교, 회귀, RT 수정, 플러그인 ID 분리, Custom Drive 비교, 벤치마크 | 완료 — **AU 빌드·auval은 사용자 Mac 실행 대기(BLOCKED)** |
| P1 | Custom Drive 게인·Headroom·앨리어싱(v2), 테일, 진단, 프리셋 재조정 | 완료 / 고게인 앨리어싱·어택 압축 OPEN(K-001, K-002), NAM 리샘플링 DEFER, 진단 UI 미구현 |
| P2 | DDSP loss 설정·Envelope loss·분할, 노브 매핑 비교, SPICE 공차 | 완료(시뮬레이션 데이터) |
| P3 | Gray-box, Architecture Search, 임베디드 | 미착수(DEFER) |

## Codex 기능 격차 해소 v1 (2026-11-24, 미커밋)
실시간 분석 탭·파형·스펙트럼, 입력 시스템 보완, 캐비닛 IR 참조 복원 구현 완료(INTEGRATION_REPORT 추가 절).
남은 것: Logic 확인(K-022), Mac 빌드(K-026), IR 번들 내보내기(K-024).

## v0.4 Sound Engine Expansion (2026-11-25)
Custom Drive Model(v1/v2/v2+ADAA), AB763 Normal M1 앰프, 팩토리 프리셋 06 추가. 다음: Mac 1회 검증 → AB763 M2(PI·6V6·Sag·NFB) → 실측 비교.

## v0.5 Premium Audio Engine (2026-11-25)
AB763 M2(PI·6V6·Sag·NFB·OT/스피커 부하, 지연 없는 NFB 루프), Custom Drive ADAA 보정, Premium Plate v1, 저장 호환 훅, 팩토리 프리셋 07,
음질 목표·기술 연구 문서. 다음 사이클 우선순위: ① v0.5 Mac/Logic 확인 ② 측정 인프라(리앰프/DI)와 AB763 SPICE 비교
③ Plate 청취 튜닝·Shimmer/Freeze ④ Delay(BBD/Tape)·Dynamics(FET) ⑤ parity 확장 ⑥ 출시 준비.

## v0.6 Premium Delay & Dynamics (2026-11-25)
Tape Echo(테이프 전송 모델), BBD Analog Delay(클록 샘플링), FET Compressor(피드백 검출), 기존 Delay 스테레오 보강, 프리셋 08/09.
v0.7 우선순위: ① v0.5/v0.6 Mac·Logic 확인과 청취 피드백 반영 ② 측정 인프라(리앰프/DI) — Tape/BBD/FET는 실기 또는 공개 측정과 비교
③ BBD 다중 칩·Tape 하드웨어 메모리 예산 ④ Premium Plate 청취 튜닝·Shimmer/Freeze ⑤ parity 확장(새 블록) ⑥ 출시 준비(설치 패키지·서명).

## v0.7 Premium Gate / EQ / BBD Refinement (2026-11-25)
`gate_premium`(익스팬더·SC HPF·히스테리시스·적응 릴리즈), `eq_parametric_premium`(TPT SVF 5밴드, 응답 곡선 UI), BBD v2(다중 칩·클록 추종 필터·
컴팬더 바닥 정합·루프 레벨 조절), 프리셋 10, 전체 체인 통합 테스트.
v0.8 방향: ① Mac/Logic 청취·자동화 확인과 반영 ② 호스트 파라미터 레이아웃(슬롯당 16개 한계) 설계 ③ 실제 깨끗한 DI·리앰프 측정 인프라
④ EQ 정합 설계·BBD 잡음 바닥 모델 ⑤ 출시 준비(설치 패키지·서명·Windows 빌드 확인).

## v0.8 Premium Modulation & Host Automation (2026-11-25)
새 타입 7종(chorus/vibrato/phaser/univibe/flanger/tremolo/rotary _premium), 공통 LFO·올패스·LR4, 호스트 파라미터 확장(K-048), 프리셋 11–12,
헤드리스 플러그인 호스트 테스트.
v0.9 방향: ① Mac/Logic 확인(v0.5–v0.8, 특히 K-053 오토메이션) ② 청취 기반 튜닝(모듈레이션·Plate·Gate) ③ 실측(LDR·로터리·실제 DI)
④ 출시 준비(설치 패키지·코드 서명·Windows 빌드·사용자 매뉴얼) ⑤ 하드웨어 플랫폼 선정 후 ARM 최적화.

## 다음 마일스톤
1. M1 (Mac, 사용자): `scripts/macos_build_install_verify.sh` → auval → Logic 체크리스트(BUILD_GUIDE §2) → 결과 로그 전달.
2. M1b (청취, 사용자): `audio/compare_drive_v2/abx` 블라인드 청취, 프리셋 02/03/05 확인, Custom Drive v2 노브 감각 피드백.
3. M2 (P1): K-001/K-002 — 1차 ADAA, 바이어스·κ 재조정, 결과를 같은 하니스로 Codex와 재비교.
4. M3 (측정): 실제 DI 녹음과 Jan Ray 복각 루프백 측정 → 하니스·DDSP 재실행.
5. M4 (제품): NAM 리샘플러, 진단 UI, Codex 고유 기능(스펙트럼 시각화, Input System) 이식 검토, Undo/Redo.
6. M5 (하드웨어): 후보 보드에서 `mfx_core` 크로스 빌드, 블록별 사이클 측정.

세부 상태표는 `docs/INTEGRATION_REPORT.md` (v0.1: `docs/COMPLETION_REPORT.md`).
