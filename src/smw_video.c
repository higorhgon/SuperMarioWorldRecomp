#include "smw_renderer.h"
#include <math.h>

SmwVideoSettings g_smw_video = {false, true, 0};
SmwViewport g_smw_viewport = {256, 0, 4.0 / 3.0};

int SmwViewOffset(SmwViewport view, int camera, int level_width) {
  int origin = camera - view.extra;
  int last = level_width > view.width ? level_width - view.width : 0;
  if (origin > last) origin = last;
  if (origin < 0) origin = 0;
  int offset = camera - origin;
  if (offset < 0) offset = 0;
  if (offset > view.width - 256) offset = view.width - 256;
  return offset;
}

SmwViewport SmwCalculateViewport(const SmwVideoSettings *s, int w, int h) {
  double aspect = 4.0 / 3.0;
  if (s->enabled) {
    aspect = s->aspect;
    if (!isfinite(aspect) || aspect <= 0)
      aspect = w > 0 && h > 0 ? (double)w / h : 4.0 / 3.0;
    if (aspect < 4.0 / 3.0) aspect = 4.0 / 3.0;
  }
  /* 256x224 has 7:6 pixel aspect when displayed at 4:3. */
  double desired = 192.0 * aspect;
  int width = desired >= SMW_RENDER_MAX_WIDTH ? SMW_RENDER_MAX_WIDTH :
              2 * (int)floor(desired / 2.0 + 0.5);
  if (width < 256) width = 256;
  if (desired > SMW_RENDER_MAX_WIDTH) aspect = width / 192.0;
  return (SmwViewport){width, (width - 256) / 2, aspect};
}

void SmwDestination(SmwViewport view, int w, int h, int *x, int *y, int *dw, int *dh) {
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  *dw = w;
  *dh = (int)floor(w / view.aspect + 0.5);
  if (*dh > h) { *dh = h; *dw = (int)floor(h * view.aspect + 0.5); }
  if (*dw < 1) *dw = 1;
  if (*dh < 1) *dh = 1;
  *x = (w - *dw) / 2;
  *y = (h - *dh) / 2;
}
