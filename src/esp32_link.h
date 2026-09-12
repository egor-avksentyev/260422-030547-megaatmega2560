#pragma once

// ============================================================================
// esp32_link.h/.cpp — приёмник UART от ESP32-компаньона (отдельный репозиторий
// esp32-audio-web-control). Mega ТОЛЬКО слушает — своего TX на эту линию не заведено (см.
// README.md проекта Mega и того ESP32-репозитория, "Mega только слушает" — level shifter
// поэтому не нужен, линия одна: ESP32 TX -> Mega RX2, аппаратный Serial2, фиксированный
// пин 17, ничего не настраивается).
//
// Протокол — простые текстовые строки, `\n`-терминированные, 115200 бод (см. те же
// константы в config.h ESP32-проекта):
//   CMD:<letter>   — команда с веб-страницы управления (R/L/E/U/D/M/P/S), исполняется через
//                    executeWebCommand() (esp32_link.cpp) — третий, независимый от ИК-пульта
//                    и энкодера источник ввода, с той же логикой действий, продублированной
//                    так же, как между пультом и энкодером (см. CLAUDE.md)
//   META:<text>    — метадата Arylic ("Artist - Title"), актуальна только пока playing==true
//   IP:<a.b.c.d>   — текущий IP ESP32 в реальной сети (не во время его собственной настройки)
//   PLAY:0 / PLAY:1 — играет ли Arylic прямо сейчас (используется для авто-переключения
//                    Source на Streamer и для полноэкранного "Now Playing", см. main.cpp)
//   ARYLIC:OK / ARYLIC:FAIL — видит ли ESP32 Arylic по сети (для экрана Info)
//   SRC:<name>     — источник воспроизведения ("Spotify"/"AirPlay"/... или пусто, см.
//                    computeSourceName() в arylic_metadata.cpp того репозитория) — НЕ то же
//                    самое, что пункт меню "Source" на самой Mega (тот про физическое реле
//                    AUX/CD/DAT/Streamer); показывается на экране Now Playing
//   POS:<pos>:<len> — позиция/длительность трека в мс НА МОМЕНТ этого сообщения (те же
//                    curpos/totlen, что уходят на веб-страницу) — Mega сама досчитывает
//                    прогресс между кадрами по millis(), как веб-страница по Date.now()
// ============================================================================

#include <Arduino.h>

void esp32LinkInit();

// Вызывать из loop() каждую итерацию — не блокирует, разбирает то, что Serial2 успел
// накопить за этот тик, ждёт '\n' на следующих, если строка ещё не пришла целиком
void esp32LinkPoll();

bool esp32LinkIsPlaying();

// "Artist - Title" (или пусто, если META ещё не приходила) — валидно смотреть только пока
// esp32LinkIsPlaying() истинно, содержимое не чистится при остановке воспроизведения
const char* esp32LinkNowPlayingText();

// "Spotify"/"AirPlay"/... (или пусто, источник не распознан/ещё не приходил SRC:) — та же
// оговорка о валидности, что у esp32LinkNowPlayingText() выше: смотреть только пока
// esp32LinkIsPlaying() истинно. Не путать с пунктом меню "Source" (settings[] на Mega) —
// это про стриминг-протокол внутри Arylic, а не про физическое реле AUX/CD/DAT/Streamer
const char* esp32LinkStreamingSource();

// Позиция/длительность трека (мс) НА МОМЕНТ последнего полученного POS: — валидно смотреть
// только пока esp32LinkIsPlaying() истинно. Для AirPlay устройство не отдаёт живую позицию
// вообще (см. project_arylic_airplay_no_metadata в памяти) — там lenMs всё ещё приходит
// корректным, но posMs просто не будет двигаться; экран Now Playing (display_logic.cpp)
// поэтому прячет прогресс-бар отдельно по esp32LinkStreamingSource() == "AirPlay", не по
// самим этим значениям
long esp32LinkTrackPosMs();
long esp32LinkTrackLenMs();

// Сколько миллисекунд прошло с момента получения POS: — прибавляется к esp32LinkTrackPosMs()
// для живого досчёта позиции между кадрами (тот же приём, что arylicTrackAgeMs() на ESP32
// для веб-страницы, только тут таймер — millis() Mega, а не Date.now() браузера)
unsigned long esp32LinkTrackPosAgeMs();

// "" пока ESP32 ни разу не прислал свой IP (например ещё не подключился к сети)
const char* esp32LinkControlIp();

// false, пока ни одного статуса ARYLIC: ещё не пришло — esp32LinkArylicOk() в этом случае
// смотреть бессмысленно (Info должен показать что-то вроде "?", не "OK"/"Disconnected")
bool esp32LinkArylicKnown();
bool esp32LinkArylicOk();
