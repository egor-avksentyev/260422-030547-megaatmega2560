#pragma once

// ============================================================================
// dimmer_animation.h — крутящийся индикатор (лампочка — Dimmer) в крайней левой части экрана главного
// меню (drawMenu(), display_logic.cpp) — показывается, когда currentMenuItem
// соответствует этому пункту. См. подробности в bass_volume_high_animation.h —
// тот же шаблон (draw*/animate*Partial), тот же формат кадров.
// ============================================================================

void drawDimmerAnim(int x, int y);
void animateDimmerIconPartial(int x, int y);
