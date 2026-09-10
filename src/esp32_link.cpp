#include "esp32_link.h"
#include "hardware_settings.h"
#include <string.h>

static char lineBuf[64];
static uint8_t lineLen = 0;

static char nowPlayingText[ESP32_LINK_META_MAX_LEN + 1] = "";
static char controlIp[16] = ""; // "255.255.255.255\0" — максимум для IPv4-строки
static bool playing = false;
static bool arylicKnown = false;
static bool arylicOk = false;

static void handleLine(char* line) {
  if (strncmp(line, "META:", 5) == 0) {
    strncpy(nowPlayingText, line + 5, ESP32_LINK_META_MAX_LEN);
    nowPlayingText[ESP32_LINK_META_MAX_LEN] = '\0';
  } else if (strncmp(line, "IP:", 3) == 0) {
    strncpy(controlIp, line + 3, sizeof(controlIp) - 1);
    controlIp[sizeof(controlIp) - 1] = '\0';
  } else if (strncmp(line, "PLAY:", 5) == 0) {
    playing = (line[5] == '1');
  } else if (strncmp(line, "ARYLIC:", 7) == 0) {
    arylicKnown = true;
    arylicOk = (strcmp(line + 7, "OK") == 0);
  }
  // "CMD:" — см. hardware_settings.h/esp32_link.h: сознательно не исполняется пока,
  // распознаётся молча, чтобы не засорять Serial "неизвестной командой" на каждое нажатие
  // кнопки на веб-странице
}

void esp32LinkInit() {
  Serial2.begin(ESP32_LINK_BAUD);
}

void esp32LinkPoll() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        handleLine(lineBuf);
      }
      lineLen = 0;
      continue;
    }
    if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
    // Переполнение строки (не должно случаться при нормальной работе) — просто отбрасывает
    // лишние символы, ждём '\n', чтобы разобраться со следующей строкой с чистого листа
  }
}

bool esp32LinkIsPlaying() {
  return playing;
}

const char* esp32LinkNowPlayingText() {
  return nowPlayingText;
}

const char* esp32LinkControlIp() {
  return controlIp;
}

bool esp32LinkArylicKnown() {
  return arylicKnown;
}

bool esp32LinkArylicOk() {
  return arylicOk;
}
