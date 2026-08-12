#pragma once

// ============================================================================
// vu_meter_animation.h — крутящийся индикатор (гистограмма нормального распределения — VU Meter) в крайней левой части экрана главного
// меню (drawMenu(), display_logic.cpp) — показывается, когда currentMenuItem
// соответствует этому пункту. См. подробности в bass_volume_high_animation.h —
// тот же шаблон (draw*/animate*Partial), тот же формат кадров.
// ============================================================================

void drawVuMeterAnim(int x, int y);
void animateVuMeterIconPartial(int x, int y);
