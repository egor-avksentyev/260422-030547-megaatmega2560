#pragma once

// ============================================================================
// source_animation.h — крутящийся индикатор (коннект — Source) в крайней левой части экрана главного
// меню (drawMenu(), display_logic.cpp) — показывается, когда currentMenuItem
// соответствует этому пункту. См. подробности в bass_volume_high_animation.h —
// тот же шаблон (draw*/animate*Partial), тот же формат кадров.
// ============================================================================

void drawSourceAnim(int x, int y);
void animateSourceIconPartial(int x, int y);
