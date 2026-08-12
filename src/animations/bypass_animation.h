#pragma once

// ============================================================================
// bypass_animation.h — крутящийся индикатор (поток — Bypass) в крайней левой части экрана главного
// меню (drawMenu(), display_logic.cpp) — показывается, когда currentMenuItem
// соответствует этому пункту. См. подробности в bass_volume_high_animation.h —
// тот же шаблон (draw*/animate*Partial), тот же формат кадров.
// ============================================================================

void drawBypassAnim(int x, int y);
void animateBypassIconPartial(int x, int y);
