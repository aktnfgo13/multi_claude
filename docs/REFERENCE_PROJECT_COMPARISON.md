# 참고 프로젝트(Codex) 비교 (REFERENCE_PROJECT_COMPARISON)

* 참고(Reference): Codex 버전 `~/multi_effects` — 브랜치 feature/custom-drive-v08, 커밋 63개(2026-09-29 ~ 11-24),
  사용자 청취 승인 이력 있음. **읽기만 했고 수정·복사하지 않았습니다.**
* 메인(Main): 이 저장소(MFX Claude). 기준선 태그 `baseline-v0.1`, 작업 브랜치 `integration/reference-project-review`,
  Mac 스냅샷 `_snapshots/baseline-v0.1_20261124-0503.tar.gz`.
* 비교 작성자(Claude)는 메인 쪽 작성자입니다. 그래서 **같은 하니스·같은 신호로 측정한 수치만 판단 근거**로 씁니다.
  청감 우열은 판단하지 않습니다(청취 불가). 측정은 클라우드 Linux x86_64, Release 빌드(E-013).

## 1. 기능별 비교와 분류

| 기능 | Codex | Claude 기존(v0.1) | 최종(v0.2) | 분류 · 이유 |
|---|---|---|---|---|
| 라우팅 | 직렬, 스테레오 레인 독립 | 직렬 + 병렬 A/B, 지연 정렬, 락프리 교체 | 유지 | KEEP — Codex에 없는 기능, 동시성 테스트 추가 |
| 호스트 파라미터 | 고정 파라미터 | 16슬롯×16 동적 바인딩(호스트 스레드가 Effect* 접근) | seqlock 메타데이터, 메시지 스레드 적용 | IMPROVE — 수명 경쟁 제거, pluginval 재통과 |
| Custom Drive 게인 구조 | D0 거의 클린, 완만한 상승 | D0부터 강한 포화(+6.5 dB, makeup +12) | v2: −6…+30 dB, 부분 보상 | REWRITE — 측정상 과포화(E-013) |
| Custom Drive Headroom | 포화점 + 레벨 함께 이동 | 포화점만 이동(완전 보상) | 50 % 보상 | ADOPT(개념) — Codex처럼 레벨과 포화가 함께 |
| 클리핑 곡선 | 소프트 곡선 + 바이어스/대칭 | |z|^p 시그모이드(0점 비해석) | 해석적 니 + κ 잔여 기울기 + β | REWRITE — 고차 배음·압축 감소 |
| Grain/비대칭 | Bias·Symmetry 노브 | Vp/Vn 분리 | 동작점 오프셋 β | IMPROVE |
| 오버샘플러 | 64·factor+1 Blackman, 지연 64 | 하프밴드 63/33/17탭, 지연 31/39/41 | 유지 | KEEP — CPU·지연 우위, 앨리어싱 원인은 구동 레벨(E-014). K-001 해결 시 재평가 |
| ADAA | 없음 | 없음 | 없음 | DEFER (K-001 후보) |
| 앰프 | tanh 2단 + EQ, 저역 컷 double | 3종(Clean/Crunch/High Gain) 블록 모델, float | 유지 | KEEP — float 오차 ≤ 0.17 dB(E-015) |
| 캐비닛/IR | 합성 IR, preset에 IR 내장 | IR 로더 + 분할 컨볼루션 | 유지 | KEEP / Codex preset 내장 IR은 DEFER(ADOPT 후보) |
| 딜레이·리버브 테일 | — | 바이패스 시 잘림 | 트레일 | IMPROVE |
| 튜너 | — | 링버퍼 레이스 | atomic | IMPROVE |
| NAM | 없음 | core 로딩, SR 불일치 시 바이패스 | 유지 | DEFER(리샘플러) |
| 시각화(파형/스펙트럼) | 있음 | 없음 | 없음 | DEFER — ADOPT 후보 |
| Input System / Pick 로딩 | 있음 | 없음 | 없음 | DEFER — ADOPT 후보 |
| 테스트 | CTest 30개 | C++ 65 + parity + pluginval | C++ 70 + 동시성 + Sanitizer + 공통 하니스 | IMPROVE |
| 연구(SPICE/DDSP/LUT) | Python 레퍼런스 분석 | 구현 | loss 설정·분할·공차·매핑 비교 추가 | IMPROVE |
| 플러그인 ID | Multi Effects / Mefx / Mfx6 | MultiEffects / Mfxp / Mfx1 | MFX Claude / MfxL / McL1 | IMPROVE — 충돌·덮어쓰기 방지 |
| 청취 검증 | 사용자 승인 | 없음 | 없음(ABX 세트만 생성) | BLOCKED(사용자) |
| 코드 이식 | — | — | Codex 코드 복사 없음 | REJECT — 참고 프로젝트는 읽기 전용 |

## 2. Custom Drive 측정 요약 (48 kHz, Headroom 50, OS 2x, 입력 피크 −7.2 dBFS)

| 설정 | 엔진 | 소신호 | onset | THD | 앨리어싱 | 프레이즈 잔차 | 클린업 잔차 | 어택 |
|---|---|---|---|---|---|---|---|---|
| D0 | Codex | −2.9 dB | −6 | 2.7 % | −83.6 dB | 0.4 % | 0.1 % | −0.1 dB |
| D0 | Claude v1 | +6.5 dB | — | 22.4 % | −51.8 dB | 23.9 % | — | −8.0 dB |
| D0 | Claude v2 | −7.0 dB | −6 | 6.1 % | −92.9 dB | 2.7 % | 0.4 % | −0.2 dB |
| D50 | Codex | +7.2 dB | −27 | 14.5 % | −74.4 dB | 11.3 % | 0.5 % | −3.3 dB |
| D50 | Claude v1 | — | — | 32.7 % | −22.8 dB | 60 % | 37.9 % | −30.6 dB |
| D50 | Claude v2 | +8.3 dB | −24 | 26.6 % | −51.3 dB | 22.5 % | 3.1 % | −5.8 dB |
| D100 | Codex | +27.5 dB | −48 | 35.5 % | −52.3 dB | 38.6 % | 15.7 % | −16.9 dB |
| D100 | Claude v2 | +23.6 dB | −42 | 33.6 % | −27.4 dB | 49.7 % | 24.7 % | −20.6 dB |

매칭 동작점(Codex 각 설정 ↔ 소신호 이득·onset이 가장 가까운 Claude v2 설정)에서:
저게인(Codex D0–25)은 Claude v2 앨리어싱이 같거나 낮음(−82…−93 vs −71…−94 dB), 중·고게인(Codex D50–100)은 Claude v2가
20–35 dB 높고 어택 압축이 1.5–2배 → **고게인 Custom Drive는 아직 Codex가 측정상 우위**(K-001, K-002).

그림: `docs/images/compare_drive/{thd_vs_input, gain_vs_input, summary_bars}.png`. 데이터: `tests/results/compare_drive/`.

## 3. 성능 (같은 기계, Custom Drive 모노 ns/샘플; 1x / 2x / 4x)

| | Codex | Claude v1 | Claude v2 |
|---|---|---|---|
| CPU | 157 / 668 / 1366 | 74 / 207 / 395 | 71 / 189 / 353 |
| 지연(샘플) | 0 / 64 / 64 | 0 / 31 / 39 | 0 / 31 / 39 |

반복 간 ±15 % 변동. 이 수치는 Linux 클라우드 기준이며 Mac(Apple Silicon)·Logic CPU 미터와 직접 비교하지 않습니다.

## 4. 권장 (다음 단계)
1. 청취: ABX 세트와 프리셋 02/03/05로 v2 저·중게인 감각 확인(사용자).
2. 고게인: ADAA 또는 Codex 방식(더 긴 오버샘플링 필터) 실험 후 같은 하니스로 재비교.
3. Codex 고유 기능 중 시각화·Input System·preset 내장 IR을 이식 후보로 검토(코드 복사가 아닌 재구현, 사용자 승인 후).
