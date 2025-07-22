#ifndef ZEPHYR_MODULES_MICROUI_RENDERER_H_
#define ZEPHYR_MODULES_MICROUI_RENDERER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "microui.h"

void r_init(void);
void r_draw_rect(mu_Rect rect, mu_Color color);
void r_draw_text(mu_Font font, const char *text, mu_Vec2 pos, mu_Color color);
void r_draw_icon(int id, mu_Rect rect, mu_Color color);
int r_get_text_width(mu_Font font, const char *text, int len);
int r_get_text_height(mu_Font font);
void r_set_clip_rect(mu_Rect rect);
void r_clear(mu_Color color);
void r_present(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_MICROUI_RENDERER_H_ */
