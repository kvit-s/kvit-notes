#if __has_include("../../../../../build/generated/newtx-charter-microtex/defs/kvit_newtx_xcharter_bolditalic_tlf_t1.def.cpp")
#include "../../../../../build/generated/newtx-charter-microtex/defs/kvit_newtx_xcharter_bolditalic_tlf_t1.def.cpp"
#elif __has_include("newtx-generated/kvit_newtx_xcharter_bolditalic_tlf_t1.def.cpp")
#include "newtx-generated/kvit_newtx_xcharter_bolditalic_tlf_t1.def.cpp"
#else
#include "res/font_def.res.h"

#undef DEF_FONT
#define DEF_FONT(name, path, unicode)                                      \
  void __font_reg(kvit_newtx_xcharter_bolditalic_tlf_t1)() {               \
    int id = tex::FontInfo::__id("kvit_newtx_xcharter_bolditalic_tlf_t1");  \
    auto info = tex::FontInfo::__create(                                    \
      id, tex::RES_BASE + "/fonts/latin/optional/cmbxti10.ttf");

#include "res/font/cmbxti10.def.cpp"

#undef DEF_FONT
#endif
