#pragma once

// ============================================================================
// remote_control.h — ИК-пульт: декодирование сигнала по прерыванию (NecDecoder)
// и обработка команд (IR_RIGHT/LEFT/ENTER/MUTE/POWER) в loop(). Дублирует по
// смыслу часть логики энкодера — при изменении поведения пункта меню правь
// оба места (см. encoder.cpp).
// ============================================================================

#include <Arduino.h>
#include <NecDecoder.h>

extern NecDecoder necDecoder;
extern volatile bool irReceived;
extern volatile uint8_t irCommand;

void IR_ISR();
void handleRemoteInput();
