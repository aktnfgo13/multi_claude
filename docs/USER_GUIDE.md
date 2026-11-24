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

## Custom Drive 노브
| 노브 | 하는 일 |
|---|---|
| Drive | 클리핑 단으로 들어가는 이득. 올리면 소리가 커지고 더 일찍 일그러짐 |
| Headroom | 클리핑이 시작되는 레벨(출력 상한). 낮추면 작게 쳐도 압축·일그러짐, 높이면 클린 여유 |
| Tight | 클리핑 전 저역 컷(30–900 Hz). 저음 뭉침과 상호변조 감소 |
| Crunch | 클리핑 곡선의 니(부드러움↔딱딱함)와 2단 압축 |
| Grain | 일그러짐의 질감: 비대칭(짝수 배음)과 고역 배음 분포 |
| Mid Character | 클리핑 전 중역 강조/감쇠(+ 클리핑 후 일부) |
| Tone | 클리핑 후 밝기(틸트)와 고역 거칠기(fizz) 필터 |
| Mix / Level / Oversampling | 병렬 클린 / 출력 / 앨리어싱 억제(높을수록 CPU 증가, 지연 31–41 샘플) |

## 팩토리 프리셋
Clean Studio, Crunch Rhythm, High Gain Lead, Ambient Swells(병렬 A: 테이프 딜레이 / B: 앰비언트 리버브),
Custom Drive Showcase. 레벨은 합성 기타 신호로 약 −14 ~ −20 dBFS RMS가 되도록 맞췄습니다.

## 알아둘 점
* 앰프·캐비닛·드라이브 모델은 특정 제품의 복제가 아니며 실제 장비와 비교 검증하지 않았습니다.
* NAM 모델이 엔진 샘플레이트와 다르면 바이패스되고 상태 문구가 표시됩니다(실시간 리샘플링 미구현).
* 외부 IR/NAM 파일의 사용 권한은 사용자 책임입니다.
