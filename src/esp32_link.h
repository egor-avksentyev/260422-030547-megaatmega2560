#pragma once

// ============================================================================
// esp32_link.h/.cpp — связь по UART с ESP32-компаньоном (отдельный репозиторий
// esp32-audio-web-control), аппаратный Serial2, 115200 бод, фиксированные пины RX2=17/TX2=16.
//
// Изначально линия была односторонней (ESP32 TX -> Mega RX2, "Mega только слушает" — см.
// историю в README.md обоих репозиториев) — Mega TX2 (пин 16) физически ничего не отправлял.
// С 2026-09-21 линия двусторонняя: Mega тоже шлёт (см. esp32LinkSendPower()/
// esp32LinkSendSensors() ниже) — на стороне ESP32 это уходит на GPIO16 (MEGA_LINK_RX_PIN,
// раньше был объявлен, но физически не подключён — см. config.h того репозитория) через
// готовый модуль level shifter (Mega — 5V логика, ESP32 GPIO не 5V-толерантны).
//
// Протокол — простые текстовые строки, `\n`-терминированные (см. те же константы в
// config.h ESP32-проекта):
//
// Принимается Mega (от ESP32):
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
//
// Отправляется Mega (в сторону ESP32) — см. esp32LinkSendPower()/esp32LinkSendSensors():
//   POWER:0 / POWER:1 — реальное состояние питания Mega (выключено/включено), шлётся сразу
//                    в момент запроса выключения/включения (не дожидаясь многосекундной
//                    анимации POWER OFF/ON) — по этому ESP32 гасит веб-страницу и ставит
//                    Spotify на паузу синхронно с реальным пультом, а не только со своей
//                    собственной кнопки на веб-странице (см. applyWebPowerState() в
//                    web_control.cpp того репозитория)
//   TEMP:<t1>:<t2>:<t3> — температуры трёх ламп (°C, один знак после запятой; -127.0 —
//                    сентинел "датчик не отвечает", как TEMP_SENSOR_INVALID), шлётся
//                    периодически (см. ESP32_LINK_SENSOR_SEND_INTERVAL_MS в
//                    hardware_settings.h) независимо от того, открыт ли на самой Mega экран
//                    Info — веб-странице эти значения нужны всегда
//   VOLT:<value>   — напряжение сети (целое число вольт), тем же таймером, что и TEMP:
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

// Шлёт "POWER:1\n" (включено) или "POWER:0\n" (выключено) — вызывать сразу в момент запроса
// включения/выключения (см. remote_control.cpp/IR_POWER и esp32_link.cpp/executeWebCommand()
// case 'P'), а не после блокирующей анимации POWER OFF/ON, иначе ESP32 узнает об этом на
// несколько секунд позже реального нажатия
void esp32LinkSendPower(bool poweredOn);

// Шлёт "TEMP:<t1>:<t2>:<t3>\n" и "VOLT:<value>\n" — вызывать периодически из loop()
// (см. ESP32_LINK_SENSOR_SEND_INTERVAL_MS), независимо от того, какой экран сейчас открыт
// на самой Mega. temps[i] — как из readAllTemperatures() (TEMP_SENSOR_INVALID, если датчик
// не отвечает), voltage — как из readMainsVoltage()
void esp32LinkSendSensors(const float temps[3], int voltage);
