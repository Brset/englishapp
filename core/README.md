# pron_core — общее C++ ядро тренажёра произношения

Чистая логика без нейросетей (C++17, без внешних зависимостей), которую используют
приложения для Windows и Android. Модели (Silero VAD, Whisper, wav2vec2, Piper, espeak-ng)
подключаются позже через интерфейсы из `backends.h`.

## Модули (`include/pron/`)

| Заголовок | Что делает |
|---|---|
| `text.h` | Нормализация и токенизация эталонного текста: смещения слов (байты UTF-8), апострофы (`’` → `'`), дефисы, числа → слова (`42`, `3rd`, `1990`, `3.5`, `1990s`), пропуск реплик-меток `Waiter:` в диалогах |
| `word_align.h` | Выравнивание распознанных слов с эталоном (редакционное расстояние + нечёткое сходство слов): matched / substituted / omitted + вставленные слова |
| `phonemes.h` | Инвентарь фонем ARPAbet ↔ IPA (39 фонем CMUdict + «русские» x, r, e, o, ɨ), разбор `TH IH1 NG K`, ударение |
| `cmudict.h`, `lexicon.h` | Загрузка CMUdict из потока/файла (варианты `(2)`, комментарии); поиск произношения: словарь → притяжательное `'s` → G2P (espeak-ng) |
| `posteriors.h` | Матрица лог-вероятностей T×C, сопоставление словаря модели (IPA-метки wav2vec2) с инвентарём |
| `ctc_align.h` | Принудительное CTC-выравнивание (Витерби): кадры каждой ожидаемой фонемы |
| `gop.h` | GOP по фонеме, перевод в 0–100 (пресеты строгости), наиболее вероятная замена |
| `fluency.h` | Темп (слов/мин), длинные паузы (> 0,5 с, настраивается; на границе предложений допуск больше), повторы, слова-паразиты |
| `advice.h` | Таблица типичных ошибок русскоязычных (θ→s/f/t, ð→z/d, w→v, æ→e, ɪ↔iː, ʊ↔uː, оглушение конечных звонких, r, h→x, ŋ→n/nk) → совет на русском + id звука (как `focus_sounds` в `content/`) |
| `assessment.h` | Сборка всего: оценка каждого слова (балл, цвет), фонем, советы, итоговые accuracy / completeness / fluency / overall |
| `json_writer.h`, `result_json.h` | Собственный JSON-писатель и сериализация результата |
| `pron_c.h` | C API (`extern "C"`, непрозрачные дескрипторы, JSON-строки + `pron_free_string`) для C#/P-Invoke и JNI |
| `backends.h` | Только интерфейсы: `IVad` (Silero VAD), `IAsr` (whisper.cpp), `IPhonemeModel` (wav2vec2 на onnxruntime), `ITts` (Piper), `IG2P` (espeak-ng) |

## Как считается оценка

1. Слова Whisper нормализуются тем же токенизатором и выравниваются с эталоном.
2. Если есть постериоры wav2vec2: для каждого прочитанного слова берётся его отрезок
   (± 80 мс), ожидаемые фонемы выравниваются CTC-Витерби, по каждой считается GOP.
3. Если постериоров нет, а Whisper услышал другое слово (`sink` вместо `think`),
   фонемы сравниваются по словарю — так находится замена θ→s.
4. Балл слова — среднее по фонемам (с ограничением сверху, если была замена);
   цвет: ≥ 80 зелёный, ≥ 60 жёлтый, иначе красный, пропущенное — серый.
5. accuracy — средний балл прочитанных слов, completeness — доля прочитанных слов,
   fluency — из темпа, пауз, повторов и «э-э».

Смещения слов в JSON даны и в байтах UTF-8 (`byte_begin`), и в единицах UTF-16
(`u16_begin`) — последние подходят для строк C# и Kotlin/Java.

## Формат JSON (`pron_assess`, версия 1)

```json
{
  "version": 1,
  "scores": {"accuracy": 94.3, "completeness": 87.5, "fluency": 92, "overall": 92.0},
  "phoneme_level": true,
  "words": [{
    "index": 1, "text": "think", "norm": "think",
    "byte_begin": 2, "byte_end": 7, "u16_begin": 2, "u16_end": 7,
    "status": "matched|substituted|omitted", "recognized": "sink", "similarity": 0.6,
    "start": 0.3, "end": 0.6, "score": 60, "band": "good|fair|poor|omitted", "color": "#F9A825",
    "pron_source": "cmudict|g2p|none", "expected_ipa": "ˈθɪŋk", "stress_syllable": 0,
    "scored_by": "gop|diff|none",
    "phonemes": [{"arpabet": "TH", "ipa": "θ", "stress": -1, "score": 0, "gop": -5.2,
                  "start": 0.3, "end": 0.38, "substituted": true,
                  "actual_arpabet": "S", "actual_ipa": "s", "advice_id": "th_s"}]
  }],
  "inserted": [{"text": "um", "start": 1.0, "end": 1.2, "after_word": 3}],
  "fluency": {"word_count": 7, "speech_seconds": 2.0, "articulation_seconds": 2.0,
              "words_per_minute": 210, "articulation_wpm": 210,
              "long_pauses": [{"after_word": 1, "start": 0.7, "end": 1.5, "duration": 0.8}],
              "repetitions": 0, "hesitations": 1},
  "advice": [{"id": "th_s", "sound_id": "θ", "expected_ipa": "θ", "actual_ipa": "s",
              "title_ru": "θ звучит как «с»", "tip_ru": "…", "count": 1, "words": [1]}],
  "warnings": []
}
```

`start`/`end` равны `null`, если время неизвестно (пропущенное слово); `score` фонемы — `null`,
если она не оценивалась.

## Сборка и тесты

```sh
cd core
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Опции: `-DPRON_BUILD_SHARED=ON` — дополнительно собрать `pron.dll` / `libpron.so`, из которой
экспортируется только C API; `-DPRON_BUILD_TESTS=OFF` — без тестов.
Тесты используют doctest (`third_party/doctest/doctest.h`, MIT); если файла нет,
CMake скачает его через FetchContent. Сборка проверена на GCC 13 и Clang 18
(`-Wall -Wextra -Wpedantic`, без предупреждений), а также с ASan/UBSan.

Для MSVC включены `/W4 /utf-8` (в исходниках есть русские строки в UTF-8).

## TODO

- Реализации бэкендов (onnxruntime, whisper.cpp, Piper, espeak-ng) — отдельные модули.
- Проверка ударения в слове (по постериорам/энергии) — сейчас отдаётся только позиция ударения из словаря.
- Числа: годы читаются как «nineteen ninety» для 1100–2099; деньги, проценты, время — нет.
- Пороги GOP и формулу fluency нужно откалибровать на реальных записях.
- Тёмный l, придыхание, связность речи (есть в `focus_sounds`) пока без правил в таблице советов.
