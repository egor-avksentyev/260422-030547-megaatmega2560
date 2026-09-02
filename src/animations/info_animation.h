#pragma once

// ============================================================================
// info_animation.h — крутящийся индикатор пункта меню "Info" в крайней левой части
// экрана главного меню (drawMenu(), display_logic.cpp) — показывается, когда
// currentMenuItem соответствует этому пункту. См. подробности в
// bass_volume_high_animation.h — тот же шаблон (draw*/animate*Partial), тот же формат кадров.
// ============================================================================

void drawInfoAnim(int x, int y);
void animateInfoIconPartial(int x, int y);
