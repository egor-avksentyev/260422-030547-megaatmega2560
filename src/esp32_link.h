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
//   CMD:J<index>   — новое (KnobControl Фаза 4): прыгнуть сразу на пункт меню <index>
//                    (0..MENU_ITEM_COUNT-1, порядок как в menuItems[]) и войти в его
//                    настройку (inSettingsMode=true) одним действием — кноп так безопасно
//                    "запрыгивает" в произвольный пункт своей (перегруппированной) карусели,
//                    не полагаясь на относительную навигацию R/L и не вычисляя самостоятельно,
//                    на сколько шагов она должна проехать (риск гонки с другим источником
//                    ввода, двигающим ту же Mega-карусель параллельно)
//   CMD:X          — новое (Фаза 4): явно выйти из настройки (inSettingsMode=false), без
//                    второго смысла, которым сейчас перегружен 'E' (тот либо входит, либо
//                    выходит, в зависимости от inSettingsMode — кнопу нужен однозначный выход)
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
//
// Новое (KnobControl Фаза 4) — см. esp32LinkSendMenu()/esp32LinkSendSettingsSnapshot()/
// esp32LinkSendMuteAndStreamer() ниже за тем, когда именно каждая шлётся:
//   MENU:<item>:<inSettingsMode 0|1>:<value...>\n — шлётся БЕЗУСЛОВНО, периодически (не
//                    только пока открыта настройка) — <value...> присутствует и имеет смысл
//                    ТОЛЬКО когда inSettingsMode==1 (кноп реально смотрит на экран настройки
//                    этого пункта, а не на свою собственную карусель/другой пункт); при
//                    inSettingsMode==0 строка заканчивается сразу после него, без значения —
//                    сам переход 1→0 это единственный сигнал, по которому кноп узнаёт "выйти
//                    обратно в карусель" (ESP32-компаньон только перепроброс, ему неоткуда
//                    взять такой сигнал самому). <value...> по типу пункта:
//                      Bass/High   — одно число, дБ, ЖИВОЕ показание потенциометра
//                                    (readBassPotPercent()/readHighPotPercent() —
//                                    motor_position.h), НЕ settings[] (та хранит командную
//                                    цель мотора, не физическое положение ручки, см.
//                                    CLAUDE.md)
//                      Volume      — одно число, %, тем же принципом — живое показание
//                                    (readVolumePotPercent()), не settings[]
//                      VU Meter/Bypass — одно число, 0/1 (settings[currentMenuItem])
//                      Source/EQ/Color — индекс пресета (settings[currentMenuItem])
//                      Dimmer      — четыре числа:
//                                    displayBrightness%:ringDimmer%:dimmerEditingDisplay
//                                    0|1:dimmerRowLocked 0|1
//                      Info        — без значения вообще (температуры/IP кноп и так знает
//                                    через WS82 от ESP32, не от Mega)
//   SETTINGS:<bassDb>:<highDb>:<volumePct>:<sourceIdx>:<eqIdx>:<colorIdx>:<bypass 0|1>:
//            <vu 0|1>\n — для Source/EQ/Color/Bypass/VU это прямое отражение settings[]
//                    (КОМАНДНАЯ цель каждого пункта, не живые показания потенциометров, в
//                    отличие от MENU: выше — это для превью в карусели кнопа, где секундная
//                    задержка за реальным физическим положением ручки не имеет значения).
//                    Bass/High/Volume — ИСКЛЮЧЕНИЕ (найдено живьём 2026-10-21): их слоты
//                    settings[0]/[1]/[2] никогда не присваиваются нигде в прошивке (нет
//                    смысла писать туда — нет отдельного "commanded target" для них, кроме
//                    самого физического положения мотора), поэтому здесь тоже живые
//                    readBassPotPercent()/readHighPotPercent()/readVolumePotPercent(), та же
//                    пара функций что и у MENU: — единственный существующий источник правды
//                    для этих трёх. Независимо от currentMenuItem/inSettingsMode — нужно
//                    кнопу даже когда он не смотрит ни на один конкретный Mega-пункт
//                    (например сидит на Home/Color)
//   MUTE:<isMuted 0|1>\n — сразу при переключении (см. вызовы esp32LinkSendMute() в
//                    remote_control.cpp/esp32_link.cpp), тем же принципом, что уже есть у
//                    POWER: — плюс периодически, тем же таймером, что SETTINGS:/STREAMER:
//                    ниже, на случай если кноп пропустил/переподключился
//   STREAMER:<0|1>\n — независимый глобальный тумблер (см. main.h/streamerRelayOn), НЕ
//                    привязан к текущему пункту меню — только периодически (тем же таймером,
//                    что SETTINGS:), без explicit-отправки на каждое из пяти мест, где
//                    streamerRelayOn меняется (сознательное упрощение — см.
//                    ESP32_LINK_MENU_SEND_INTERVAL_MS в hardware_settings.h за тем же
//                    принципом у MENU:)
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

// --- KnobControl Фаза 4 — см. комментарий в начале файла за полным форматом ---

// Шлёт "MENU:...\n" — вызывать из loop() безусловно, периодически
// (ESP32_LINK_MENU_SEND_INTERVAL_MS), независимо от inSettingsMode — само поле
// inSettingsMode=0|1 внутри телеграммы несёт этот факт, и переход 1→0 это единственный
// сигнал, по которому кноп узнаёт "выйти обратно в свою карусель" (см. комментарий в начале
// файла)
void esp32LinkSendMenu();

// Шлёт "SETTINGS:...\n" — вызывать периодически из loop() (тем же таймером, что
// esp32LinkSendSensors()), независимо от currentMenuItem/inSettingsMode
void esp32LinkSendSettingsSnapshot();

// Шлёт "MUTE:<isMuted 0|1>\n" + "STREAMER:<streamerRelayOn 0|1>\n" — вызывать периодически
// (тем же таймером, что esp32LinkSendSettingsSnapshot()). MUTE: дополнительно шлётся сразу в
// момент переключения — см. esp32LinkSendMute() ниже
void esp32LinkSendMuteAndStreamer();

// Шлёт только "MUTE:<muted 0|1>\n" — вызывать сразу в момент переключения isMuted (тем же
// принципом, что esp32LinkSendPower() у Power), а не дожидаться следующего периодического
// esp32LinkSendMuteAndStreamer()
void esp32LinkSendMute(bool muted);
