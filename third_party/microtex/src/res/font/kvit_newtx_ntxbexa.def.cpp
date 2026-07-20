#if __has_include("../../../../../build/generated/newtx-charter-microtex/defs/kvit_newtx_ntxbexa.def.cpp")
#include "../../../../../build/generated/newtx-charter-microtex/defs/kvit_newtx_ntxbexa.def.cpp"
#elif __has_include("newtx-generated/kvit_newtx_ntxbexa.def.cpp")
#include "newtx-generated/kvit_newtx_ntxbexa.def.cpp"
#else
#include "res/font_def.res.h"

#undef DEF_FONT
#define DEF_FONT(name, path, unicode)                                      \
  void __font_reg(kvit_newtx_ntxbexa)() {                                   \
    int id = tex::FontInfo::__id("kvit_newtx_ntxbexa");                     \
    auto info = tex::FontInfo::__create(                                    \
      id, tex::RES_BASE + "/fonts/base/cmex10.ttf");

#include "res/font/cmex10.def.cpp"

#undef DEF_FONT
#endif
