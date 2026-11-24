# 사용자 가이드

## 화면
* **상단**: 프리셋 선택 · Save/Load(JSON) · Parallel A/B 토글 · 입력 모드(Auto / Mono L / Mono L+R / Stereo)
  · IN/OUT 게인 · A/B 밸런스 · 입출력 미터(피크·RMS·클립 LED) · CPU · 지연.
* **신호 체인**: PRE → (CHAIN 또는 LANE A / LANE B) → POST.
  * `+` : 해당 레인 끝에 이펙트 추가(카테고리 메뉴)
  * 클릭: 선택(아래 파라미터 패널 표시) · 더블클릭: 바이패스 · 드래그: 순서 변경/레인 이동 · 우클릭: 메뉴(바이패스, 삭제, 이동)
  * 블록 하단 숫자는 해당 이펙트의 지연(샘플).
* **파라미터 패널**: 노브(더블클릭 = 기본값, 값 직접 입력 가능), 선택 상자, 토글.
  Cabinet은 *Load IR*, NAM Amp는 *Load .nam*, DDSP Drive는 *Load knob LUT* 버튼이 있습니다. Tuner는 음정·센트를 표시합니다.

## 자동화 · MIDI
* 호스트 파라미터: `Input Gain`, `Output Gain`, `Parallel A/B Mix`, 슬롯 1–16 각각 `Bypass` + 파라미터 16개.
  이름은 슬롯의 이펙트를 따라갑니다(예: `S2 Drive`).
* MIDI CC 매핑은 프리셋 JSON의 `midi` 배열로 정의합니다(예: CC 64 → 슬롯 바이패스). UI 편집기는 아직 없습니다.

## Custom Drive 노브 (매핑 v2)
| 노브 | 하는 일 |
|---|---|
| Drive | 클리퍼로 들어가는 이득(−6…+30 dB). 0 근처는 보통 험버커 레벨에서 거의 클린, 올릴수록 일찍·많이 일그러짐 |
| Headroom | 포화가 시작되는 레벨(−18…0 dBFS). 낮추면 작게 쳐도 압축·일그러짐, 높이면 클린 여유. 출력 레벨도 절반쯤 함께 변함 |
| Tight | 클리핑 전 저역 컷(30–900 Hz). 저음 뭉침과 상호변조 감소 |
| Crunch | 포화 곡선의 단단함. 낮으면 포화 뒤에도 피킹 강약이 많이 남고, 높으면 단단하고 압축된 느낌 |
| Grain | 질감: 비대칭(짝수 배음)과 클리핑 전 고역 강조 |
| Mid Character | 클리핑 전 중역 강조/감쇠(+ 클리핑 후 일부) |
| Tone | 클리핑 후 밝기(틸트)와 고역 거칠기(fizz) 필터 |
| Mix / Level / Oversampling | 병렬 클린 / 출력 / 앨리어싱 억제(높을수록 CPU 증가, 지연 31–41 샘플) |

* **Drive 60 이상에서는 Oversampling 4x를 권장합니다.** 2x에서는 고음 연주 시 금속성 앨리어싱이 남을 수 있습니다(KNOWN_ISSUES K-001).
* v0.1에서 저장한 프리셋의 Custom Drive는 v2에서 소리가 달라집니다(더 깨끗하고 작게). 팩토리 프리셋은 v2에 맞춰 다시 조정했습니다.

## 팩토리 프리셋
Clean Studio, Crunch Rhythm, High Gain Lead, Ambient Swells(병렬 A: 테이프 딜레이 / B: 앰비언트 리버브),
Custom Drive Showcase. 레벨은 합성 기타 신호로 약 −14 ~ −20 dBFS RMS가 되도록 맞췄습니다.

## 알아둘 점
* 앰프·캐비닛·드라이브 모델은 특정 제품의 복제가 아니며 실제 장비와 비교 검증하지 않았습니다.
* NAM 모델이 엔진 샘플레이트와 다르면 바이패스되고 상태 문구가 표시됩니다(실시간 리샘플링 미구현).
* 외부 IR/NAM 파일의 사용 권한은 사용자 책임입니다.
