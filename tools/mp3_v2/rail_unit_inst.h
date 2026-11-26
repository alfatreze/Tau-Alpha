/* One renamed copy of the window-unit golden model (sim/mp3_poly_model_core.h, B-292) per include: define K (0..5) before including.
 * The model keeps its ring and head in file-level statics, so several gains can run side by side (one unit "instance" per gain) only by giving
 * each copy its own names. Not an include-once header on purpose. Own code (Tau Alpha, MIT). */
#define RU_CAT2(a, b) a##b
#define RU_CAT(a, b) RU_CAT2(a, b)
#define ring RU_CAT(ring_, K)
#define head RU_CAT(head_, K)
#define clip16 RU_CAT(clip16_, K)
#define word RU_CAT(word_, K)
#define push RU_CAT(push_, K)
#define window RU_CAT(window_, K)
#undef MP3_POLY_MODEL_CORE_H
#include "mp3_poly_model_core.h"
#undef ring
#undef head
#undef clip16
#undef word
#undef push
#undef window
