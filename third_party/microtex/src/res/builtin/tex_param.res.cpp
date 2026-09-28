#include "fonts/fonts.h"

#include <cstdlib>
#include <string>

#if __has_include("../../../../../build/generated/newtx-charter-microtex/defs/kvit_newtx_tex_params.def.cpp")
#include "../../../../../build/generated/newtx-charter-microtex/defs/kvit_newtx_tex_params.def.cpp"
#elif __has_include("../font/newtx-generated/kvit_newtx_tex_params.def.cpp")
#include "../font/newtx-generated/kvit_newtx_tex_params.def.cpp"
#endif

/**
 * General parameters used in the TeX algorithms, 
 * specific for the computer modern font family
 */
std::map<std::string, float> tex::DefaultTeXFont::_parameters = {
    {"num1", 0.676508f},
    {"num2", 0.393732f},
    {"num3", 0.443731f},
    {"denom1", 0.685951f},
    {"denom2", 0.344841f},
    {"sup1", 0.412892f},
    {"sup2", 0.362892f},
    {"sup3", 0.288889f},
    {"sub1", 0.15f},
    {"sub2", 0.247217f},
    {"supdrop", 0.386108f},
    {"subdrop", 0.05f},
    {"axisheight", 0.25f},
    {"defaultrulethickness", 0.039999f},
    {"bigopspacing1", 0.111112f},
    {"bigopspacing2", 0.166667f},
    {"bigopspacing3", 0.2f},
    {"bigopspacing4", 0.6f},
    {"bigopspacing5", 0.1f},
};

#define __id(name) FontInfo::__id(#name)

namespace {

// The vendored NewTX/XCharter charter-math port is the default font mode;
// KVIT_MATH_FONT=cm opts back into MicroTeX's Computer Modern tables. The
// historical opt-in value "newtx-charter-generated-prototype" still selects
// the (now default) NewTX mode.
bool kvitNewtxCharterEnabled() {
  const char* value = std::getenv("KVIT_MATH_FONT");
  return value == nullptr || std::string(value) != "cm";
}

}  // namespace

void tex::DefaultTeXFont::__default_general_settings() {
#ifdef KVIT_NEWTX_GENERATED_TEX_PARAMS_AVAILABLE
  // TeX reads the math layout constants from fontdimens of the symbol and
  // extension fonts, so the generated prototype swaps in the NewTX values
  // from ntxsy.tfm/ntxexx.tfm alongside its font tables.
  if (kvitNewtxCharterEnabled())
    kvitNewtxApplyGeneratedTexParams(_parameters);
#endif

  const int muFontId = kvitNewtxCharterEnabled()
      ? __id(kvit_newtx_ntxsy)
      : __id(cmsy10);
  const int spaceFontId = kvitNewtxCharterEnabled()
      ? __id(kvit_newtx_xcharter_roman_tlf_t1)
      : __id(cmr10);

  tex::DefaultTeXFont::_generalSettings = {
      {"mufontid", static_cast<float>(muFontId)},
      {"spacefontid", static_cast<float>(spaceFontId)},
      {"textfactor", 1.f},
      {"scriptfactor", 0.7f},
      {"scriptscriptfactor", 0.5f},
  };
}

#define cf(c, f) new CharFont(c, __id(f))

void tex::DefaultTeXFont::__default_text_style_mapping() {
  std::vector<CharFont*> mathnormal = {
      cf(48, cmr10),
      cf(65, cmmi10),
      cf(97, cmmi10),
      cf(0, cmmi10),
  };
  std::vector<CharFont*> mathfrak = {
      cf(48, eufm10),
      cf(65, eufm10),
      cf(97, eufm10),
      nullptr,
  };
  std::vector<CharFont*> mathcal = {
      nullptr,
      cf(65, cmsy10),
      nullptr,
      nullptr,
  };
  std::vector<CharFont*> mathbb = {
      nullptr,
      cf(65, msbm10),
      nullptr,
      nullptr,
  };
  std::vector<CharFont*> mathscr = {
      nullptr,
      cf(65, rsfs10),
      nullptr,
      nullptr,
  };
  std::vector<CharFont*> mathuscr = mathscr;
  std::vector<CharFont*> varmathbb = mathbb;
  std::vector<CharFont*> vvmathbb = mathbb;

  if (kvitNewtxCharterEnabled()) {
    mathnormal = {
        cf(48, kvit_newtx_xcharter_roman_tlf_t1),
        cf(65, kvit_newtx_zchmi),
        cf(97, kvit_newtx_zchmi),
        cf(0, kvit_newtx_zchmi),
    };
    mathfrak = {
        cf(48, kvit_newtx_zchmia),
        cf(65, kvit_newtx_zchmia),
        cf(97, kvit_newtx_zchmia),
        nullptr,
    };
    mathcal = {
        nullptr,
        cf(65, kvit_newtx_ntxsy),
        nullptr,
        nullptr,
    };
    mathbb = {
        nullptr,
        cf(65, kvit_newtx_ntxsym),
        nullptr,
        nullptr,
    };
    mathscr = {
        nullptr,
        cf(142, kvit_newtx_zchmi),
        cf(168, kvit_newtx_zchmi),
        nullptr,
    };
    mathuscr = {
        nullptr,
        cf(196, kvit_newtx_zchmi),
        cf(222, kvit_newtx_zchmi),
        nullptr,
    };
    varmathbb = {
        cf(43, kvit_newtx_zchmia),
        cf(132, kvit_newtx_zchmia),
        cf(158, kvit_newtx_zchmia),
        nullptr,
    };
    vvmathbb = {
        cf(43, kvit_newtx_zchmia),
        cf(193, kvit_newtx_zchmia),
        cf(225, kvit_newtx_zchmia),
        nullptr,
    };
  }

  tex::DefaultTeXFont::_textStyleMappings = {
      {"mathnormal", mathnormal},
      {"mathfrak", mathfrak},
      {"mathcal", mathcal},
      {"mathbb", mathbb},
      {"mathscr", mathscr},
      {"mathslscr", mathscr},
      {"mathuscr", mathuscr},
      {"varmathbb", varmathbb},
      {"vmathbb", varmathbb},
      {"vvmathbb", vvmathbb},
      {"mathds", {nullptr, cf(65, dsrom10), nullptr, nullptr}},
      {"oldstylenums", {cf(48, cmmi10), nullptr, nullptr, nullptr}},
  };

  tex::DefaultTeXFont::_defaultTextStyleMappings = new std::string[4];
  for (int i = 0; i < 4; i++)
    tex::DefaultTeXFont::_defaultTextStyleMappings[i] = "mathnormal";
}
