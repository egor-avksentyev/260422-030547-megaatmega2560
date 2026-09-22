#include "frame_mirror.h"
#include "hardware_settings.h"
#include "display_logic.h"

static bool pending = false;
static uint8_t pendingIconId = FRAME_MIRROR_ICON_NONE;
static uint8_t pendingScreenId = FRAME_MIRROR_SCREEN_OTHER;
static unsigned long lastSendTime = 0;

void frameMirrorInit() {
  Serial3.begin(FRAME_MIRROR_BAUD);
}

void frameMirrorRequestSend(uint8_t iconId, uint8_t screenId) {
  pending = true;
  pendingIconId = iconId;
  pendingScreenId = screenId;
}

void frameMirrorPoll() {
  if (!pending) {
    return;
  }
  unsigned long now = millis();
  if (lastSendTime != 0 && (unsigned long)(now - lastSendTime) < FRAME_MIRROR_MIN_INTERVAL_MS) {
    return; // ещё не пора — запрос остаётся pending, отправим на следующий вызов
  }
  pending = false;
  lastSendTime = now;

  uint8_t tileW = u8g2.getBufferTileWidth();
  uint8_t tileH = u8g2.getBufferTileHeight();
  uint16_t totalBytes = (uint16_t)tileW * 8 * (uint16_t)tileH;
  const uint8_t* buf = u8g2.getBufferPtr();

  uint8_t checksum = 0;
  for (uint16_t i = 0; i < totalBytes; i++) {
    checksum ^= buf[i];
  }
  checksum ^= pendingIconId;
  checksum ^= pendingScreenId;

  Serial3.write((uint8_t)0xAA);
  Serial3.write((uint8_t)0x55);
  Serial3.write(buf, totalBytes);
  Serial3.write(pendingIconId);
  Serial3.write(pendingScreenId);
  Serial3.write(checksum);
}
