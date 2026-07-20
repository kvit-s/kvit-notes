#if __has_include("../../../../../build/generated/newtx-charter-microtex/defs/kvit_newtx_zchbmi.def.cpp")
#include "../../../../../build/generated/newtx-charter-microtex/defs/kvit_newtx_zchbmi.def.cpp"
#elif __has_include("newtx-generated/kvit_newtx_zchbmi.def.cpp")
#include "newtx-generated/kvit_newtx_zchbmi.def.cpp"
#else
#include "res/font_def.res.h"

#undef DEF_FONT
#define DEF_FONT(name, path, unicode)                                      \
  void __font_reg(kvit_newtx_zchbmi)() {                                    \
    int id = tex::FontInfo::__id("kvit_newtx_zchbmi");                      \
    auto info = tex::FontInfo::__create(                                    \
      id, tex::RES_BASE + "/fonts/base/cmmib10.ttf");

#include "res/font/cmmib10.def.cpp"

#undef DEF_FONT
#endif
