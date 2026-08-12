#pragma once

// ============================================================================
// color_animation.h — крутящийся индикатор (солнце — Color) в крайней левой части экрана главного
// меню (drawMenu(), display_logic.cpp) — показывается, когда currentMenuItem
// соответствует этому пункту. См. подробности в bass_volume_high_animation.h —
// тот же шаблон (draw*/animate*Partial), тот же формат кадров.
// ============================================================================

void drawColorAnim(int x, int y);
void animateColorIconPartial(int x, int y);
