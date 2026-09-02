#pragma once

// ============================================================================
// eq_animation.h — крутящийся индикатор (эквалайзер — EQ) в крайней левой части экрана
// главного меню (drawMenu(), display_logic.cpp) — показывается, когда currentMenuItem
// соответствует этому пункту. См. подробности в bass_volume_high_animation.h —
// тот же шаблон (draw*/animate*Partial), тот же формат кадров.
// ============================================================================

void drawEqAnim(int x, int y);
void animateEqIconPartial(int x, int y);
