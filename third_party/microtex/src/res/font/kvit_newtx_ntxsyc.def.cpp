#if __has_include("../../../../../build/generated/newtx-charter-microtex/defs/kvit_newtx_ntxsyc.def.cpp")
#include "../../../../../build/generated/newtx-charter-microtex/defs/kvit_newtx_ntxsyc.def.cpp"
#elif __has_include("newtx-generated/kvit_newtx_ntxsyc.def.cpp")
#include "newtx-generated/kvit_newtx_ntxsyc.def.cpp"
#else
#include "res/font_def.res.h"

#undef DEF_FONT
#define DEF_FONT(name, path, unicode)                                      \
  void __font_reg(kvit_newtx_ntxsyc)() {                                    \
    int id = tex::FontInfo::__id("kvit_newtx_ntxsyc");                      \
    auto info = tex::FontInfo::__create(                                    \
      id, tex::RES_BASE + "/fonts/maths/msam10.ttf");

#include "res/font/msam10.def.cpp"

#undef DEF_FONT
#endif
