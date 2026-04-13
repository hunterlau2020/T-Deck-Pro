/* Placeholder - compass needle image not available for LVGL v8 */
#include "lvgl.h"

static const uint8_t img_compass_needle_map[] = { 0x00, 0x00 };

const lv_img_dsc_t img_compass_needle = {
  .header.always_zero = 0,
  .header.w = 1,
  .header.h = 1,
  .header.cf = LV_IMG_CF_ALPHA_1BIT,
  .data_size = 1,
  .data = img_compass_needle_map,
};
