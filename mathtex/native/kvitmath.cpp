// The MicroTeX back end that records drawing instead of painting, and the C
// interface kvitmath.h declares over it.
//
// MicroTeX lays a formula out into boxes and draws them through the abstract
// tex::Graphics2D in graphic/graphic.h. The back end here keeps the
// transform, colour, stroke and font the boxes set, and writes each character,
// line and rectangle they draw into a byte stream with the transform and
// colour in force. The Go package mathtex reads the stream and draws it with
// its own copies of the same font files.
//
// Nothing here throws across the C interface: every entry point catches
// everything and returns the message instead.

#include "kvitmath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "atom/atom_char.h"
#include "core/formula.h"
#include "core/macro.h"
#include "graphic/graphic.h"
#include "latex.h"

namespace {

// The foreground colour handed to MicroTeX. TeXRender::draw sets it before
// drawing and \color replaces it inside its group, so a command drawn in
// exactly this colour takes the caller's foreground and every other colour is
// one the formula chose. MicroTeX only sets and restores colours and never
// computes one from another, so the value only has to be one no formula
// asks for; its alpha is not zero because MicroTeX treats a transparent
// foreground as "use black".
constexpr tex::color kForeground = 0x01020304;

KvitMathMeasure g_measure = nullptr;

// What the last call returned; the caller copies it before the next call.
std::string g_out;

// The font files MicroTeX has opened, by id; FontInfo keeps one tex::Font per
// file for the life of the process, so the ids stay valid.
std::vector<std::string> g_fontPaths;

// ---- UTF-8 and wide strings -------------------------------------------------

// MicroTeX works on std::wstring. wchar_t is 32 bits on Linux and macOS and 16
// on Windows, so a character outside the Basic Multilingual Plane is one
// wchar_t on the first and a surrogate pair on the second.
std::wstring toWide(const char *s, int64_t n) {
    std::wstring out;
    out.reserve(static_cast<size_t>(n));
    int64_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0xFFFD;
        int len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07;
        } else {
            len = 0;
        }
        if (len == 0 || i + len > n) {
            cp = 0xFFFD;
            len = 1;
        } else {
            for (int k = 1; k < len; k++) {
                const unsigned char cc = static_cast<unsigned char>(s[i + k]);
                if ((cc & 0xC0) != 0x80) {
                    cp = 0xFFFD;
                    len = k;
                    break;
                }
                cp = (cp << 6) | (cc & 0x3F);
            }
        }
        i += len;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000))
            cp = 0xFFFD;
        if (sizeof(wchar_t) == 2 && cp > 0xFFFF) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

void appendUtf8(std::string &out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Wide strings can end in a NUL MicroTeX left there, which ends the text.
std::string toUtf8(const std::wstring &w) {
    std::string out;
    for (size_t i = 0; i < w.size(); i++) {
        uint32_t cp = static_cast<uint32_t>(w[i]);
        if (cp == 0)
            break;
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < w.size()) {
            const uint32_t lo = static_cast<uint32_t>(w[i + 1]);
            if (lo >= 0xDC00 && lo < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        appendUtf8(out, cp);
    }
    return out;
}

// The generated NewTX fonts repeat every TeX slot below 33, the codes of
// control characters, at U+E000 + slot. Whenever the NewTX fonts are in use
// the alias is recorded here, so the Go side finds the glyph through that
// map entry.
wchar_t remapLowSlot(wchar_t c) {
    static const bool generated = [] {
        const char *value = std::getenv("KVIT_MATH_FONT");
        return value == nullptr || std::string(value) != "cm";
    }();
    if (generated && c < 33)
        return static_cast<wchar_t>(0xE000 + c);
    return c;
}

// ---- the byte stream --------------------------------------------------------

struct Writer {
    std::string &b;

    void u8(uint8_t v) { b.push_back(static_cast<char>(v)); }
    void u16(uint16_t v) {
        u8(v & 0xFF);
        u8(v >> 8);
    }
    void u32(uint32_t v) {
        u16(v & 0xFFFF);
        u16(v >> 16);
    }
    void f32(float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        u32(u);
    }
    void bytes(const std::string &s) { b.append(s); }
};

}  // namespace

namespace tex {

// ---- fonts ------------------------------------------------------------------

// A font is either a file MicroTeX has metrics for (its math and text fonts,
// always at size 1, which the boxes scale), or a family name and style for
// text MicroTeX measures through TextLayout.
class Font_rec : public Font {
public:
    std::string file;
    int id = -1;
    std::string family;
    int style = PLAIN;
    float size = 1;

    Font_rec(const std::string &f, float s) : file(f), size(s) {
        auto it = std::find(g_fontPaths.begin(), g_fontPaths.end(), f);
        id = static_cast<int>(it - g_fontPaths.begin());
        if (it == g_fontPaths.end())
            g_fontPaths.push_back(f);
    }

    Font_rec(const std::string &fam, int st, float s) : family(fam), style(st), size(s) {}

    float getSize() const override { return size; }

    sptr<Font> deriveFont(int st) const override {
        auto f = sptrOf<Font_rec>(*this);
        f->style = st;
        return f;
    }

    bool operator==(const Font &other) const override {
        const auto &o = static_cast<const Font_rec &>(other);
        return file == o.file && family == o.family && style == o.style && size == o.size;
    }

    bool operator!=(const Font &other) const override { return !(*this == other); }
};

Font *Font::create(const std::string &file, float size) { return new Font_rec(file, size); }

sptr<Font> Font::_create(const std::string &name, int style, float size) {
    return sptrOf<Font_rec>(name, style, size);
}

// ---- recording --------------------------------------------------------------

class Graphics2D_rec : public Graphics2D {
public:
    explicit Graphics2D_rec(std::string &out) : _w{out} { setFont(&_default); }

    void setColor(color c) override { _color = c; }
    color getColor() const override { return _color; }
    void setStroke(const Stroke &s) override { _stroke = s; }
    const Stroke &getStroke() const override { return _stroke; }
    void setStrokeWidth(float w) override { _stroke.lineWidth = w; }
    const Font *getFont() const override { return _font; }
    void setFont(const Font *font) override { _font = static_cast<const Font_rec *>(font); }

    // Each operation applies in the coordinates the ones before it set up,
    // so it multiplies the transform on the right.
    void translate(float dx, float dy) override {
        _m[4] += _m[0] * dx + _m[2] * dy;
        _m[5] += _m[1] * dx + _m[3] * dy;
    }

    void scale(float sx, float sy) override {
        _sx *= sx;
        _sy *= sy;
        _m[0] *= sx;
        _m[1] *= sx;
        _m[2] *= sy;
        _m[3] *= sy;
    }

    void rotate(float angle) override {
        const float c = std::cos(angle), s = std::sin(angle);
        const float a = _m[0], b = _m[1], cc = _m[2], d = _m[3];
        _m[0] = a * c + cc * s;
        _m[1] = b * c + d * s;
        _m[2] = cc * c - a * s;
        _m[3] = d * c - b * s;
    }

    void rotate(float angle, float px, float py) override {
        translate(px, py);
        rotate(angle);
        translate(-px, -py);
    }

    void reset() override {
        _m[0] = _m[3] = 1;
        _m[1] = _m[2] = _m[4] = _m[5] = 0;
        _sx = _sy = 1;
    }

    float sx() const override { return _sx; }
    float sy() const override { return _sy; }

    void drawChar(wchar_t c, float x, float y) override {
        if (_font == nullptr)
            return;
        if (_font->id < 0) {
            drawText(std::wstring(1, c), x, y);
            return;
        }
        declareFont(_font->id);
        head(KVITMATH_OP_GLYPH);
        _w.u16(static_cast<uint16_t>(_font->id));
        _w.u32(static_cast<uint32_t>(remapLowSlot(c)));
        _w.f32(x);
        _w.f32(y);
        _w.f32(_font->size);
    }

    // MicroTeX's boxes never draw strings; this is for completeness, as the
    // characters of a font file, or as measured text in a family.
    void drawText(const std::wstring &t, float x, float y) override {
        if (_font != nullptr && _font->id >= 0) {
            for (wchar_t c : t) {
                if (c == 0)
                    break;
                drawChar(c, x, y);
            }
            return;
        }
        text(toUtf8(t), _font ? _font->family : std::string(), _font ? _font->style : PLAIN,
             _font ? _font->size : 1, x, y);
    }

    void text(const std::string &t, const std::string &family, int style, float size, float x, float y) {
        head(KVITMATH_OP_TEXT);
        _w.f32(x);
        _w.f32(y);
        _w.f32(size);
        _w.u8(static_cast<uint8_t>(style));
        _w.u16(static_cast<uint16_t>(std::min<size_t>(family.size(), 0xFFFF)));
        _w.u32(static_cast<uint32_t>(t.size()));
        _w.bytes(family.substr(0, 0xFFFF));
        _w.bytes(t);
    }

    void drawLine(float x1, float y1, float x2, float y2) override {
        head(KVITMATH_OP_LINE);
        _w.f32(x1);
        _w.f32(y1);
        _w.f32(x2);
        _w.f32(y2);
        stroke();
    }

    void drawRect(float x, float y, float w, float h) override { rect(x, y, w, h, 0, 0, false); }
    void fillRect(float x, float y, float w, float h) override { rect(x, y, w, h, 0, 0, true); }
    void drawRoundRect(float x, float y, float w, float h, float rx, float ry) override {
        rect(x, y, w, h, rx, ry, false);
    }
    void fillRoundRect(float x, float y, float w, float h, float rx, float ry) override {
        rect(x, y, w, h, rx, ry, true);
    }

private:
    Font_rec _default{"SansSerif", PLAIN, 20.f};
    Writer _w;
    color _color = black;
    Stroke _stroke;
    const Font_rec *_font = nullptr;
    float _m[6] = {1, 0, 0, 1, 0, 0};
    float _sx = 1, _sy = 1;
    std::set<int> _declared;

    void declareFont(int id) {
        if (!_declared.insert(id).second)
            return;
        const std::string &path = g_fontPaths[static_cast<size_t>(id)];
        _w.u8(KVITMATH_OP_FONT);
        _w.u16(static_cast<uint16_t>(id));
        _w.u16(static_cast<uint16_t>(std::min<size_t>(path.size(), 0xFFFF)));
        _w.bytes(path.substr(0, 0xFFFF));
    }

    void head(uint8_t op) {
        _w.u8(op);
        _w.u32(_color == kForeground ? 0 : _color);
        for (float v : _m)
            _w.f32(v);
    }

    void stroke() {
        _w.f32(_stroke.lineWidth);
        _w.u8(static_cast<uint8_t>(_stroke.cap));
        _w.u8(static_cast<uint8_t>(_stroke.join));
    }

    void rect(float x, float y, float w, float h, float rx, float ry, bool filled) {
        head(KVITMATH_OP_RECT);
        _w.f32(x);
        _w.f32(y);
        _w.f32(w);
        _w.f32(h);
        _w.f32(rx);
        _w.f32(ry);
        _w.u8(filled ? 1 : 0);
        stroke();
    }
};

// ---- text MicroTeX has no font for ------------------------------------------

class TextLayout_rec : public TextLayout {
public:
    TextLayout_rec(const std::wstring &src, const sptr<Font_rec> &font)
        : _text(toUtf8(src)), _family(font->family), _style(font->style), _size(font->size) {}

    void getBounds(Rect &r) override {
        if (g_measure != nullptr) {
            KvitMathText t{};
            t.text = _text.data();
            t.textLen = static_cast<int32_t>(_text.size());
            t.style = _style;
            t.family = _family.data();
            t.familyLen = static_cast<int32_t>(_family.size());
            t.size = _size;
            g_measure(&t);
            r = Rect(t.x, t.y, t.w, t.h);
            return;
        }
        // Without a way to measure, a guess of the right order: half an em per
        // character, and the line's usual ascent and descent.
        int chars = 0;
        for (unsigned char c : _text)
            chars += (c & 0xC0) != 0x80;
        r = Rect(0, -0.8f * _size, 0.5f * _size * chars, _size);
    }

    void draw(Graphics2D &g2, float x, float y) override {
        static_cast<Graphics2D_rec &>(g2).text(_text, _family, _style, _size, x, y);
    }

private:
    std::string _text;
    std::string _family;
    int _style;
    float _size;
};

sptr<TextLayout> TextLayout::create(const std::wstring &src, const sptr<Font> &font) {
    return sptrOf<TextLayout_rec>(src, std::static_pointer_cast<Font_rec>(font));
}

}  // namespace tex

// ---- the C interface --------------------------------------------------------

namespace {

bool g_inited = false;

int32_t fail(KvitMathResult *out, const std::string &message) {
    g_out = message;
    if (out != nullptr) {
        out->width = out->height = out->depth = out->baseline = 0;
        out->data = reinterpret_cast<const uint8_t *>(g_out.data());
        out->len = static_cast<int64_t>(g_out.size());
    }
    return 0;
}

// parse lays tex out at size, as LaTeX::parse does, in display or text
// style. Text style is the size TeX sets a $...$ span in: limits beside a
// large operator, and fractions at script size. Wrapping the TeX in
// \textstyle{...} would give the same style, but MicroTeX parses that
// argument as a formula of its own and, in its lenient mode, drops its parse
// errors: "a & b" typesets to nothing instead of saying '&' needs an array,
// and before the local fix in core/formula.cpp it crashed. Setting the style
// of the whole formula lays it out the same (the tests compare the two) and
// keeps the errors.
tex::TeXRender *parse(const std::wstring &tex, float size, bool display) {
    static tex::Formula *formula = new tex::Formula();
    static tex::TeXRenderBuilder builder;
    // A formula starting "$$" or "\[" is centred in the wrap width, as
    // LaTeX::parse centres it; any other is set on one line of its own width.
    const bool lined = tex.rfind(L"$$", 0) != 0 && tex.rfind(L"\\[", 0) != 0;
    formula->setLaTeX(tex);
    // A very wide wrap width, so a formula never breaks into lines.
    return builder.setStyle(display ? tex::TexStyle::display : tex::TexStyle::text)
        .setTextSize(size)
        .setWidth(tex::UnitType::pixel, 1 << 16, lined ? tex::Alignment::left : tex::Alignment::center)
        .setIsMaxWidth(lined)
        .setLineSpace(tex::UnitType::pixel, size / 3.0f)
        .setForeground(kForeground)
        .build(*formula);
}

int32_t succeed(KvitMathResult *out) {
    if (out != nullptr) {
        out->data = reinterpret_cast<const uint8_t *>(g_out.data());
        out->len = static_cast<int64_t>(g_out.size());
    }
    return 1;
}

}  // namespace

extern "C" {

KVITMATH_API int32_t kvitmath_version(void) { return KVITMATH_VERSION; }

KVITMATH_API int32_t kvitmath_init(const char *resRoot, int64_t resRootLen, KvitMathResult *out) {
    if (out != nullptr)
        out->width = out->height = out->depth = out->baseline = 0;
    if (g_inited) {
        g_out.clear();
        return succeed(out);
    }
    try {
        const std::string root(resRoot, static_cast<size_t>(resRootLen));
        // LaTeX::init keeps its default, the relative path "res", when it
        // finds no marker file in the root it is given or in the places
        // clatexmath installs to; the root the caller found is used then.
        tex::RES_BASE = root;
        tex::LaTeX::init(root);
        g_inited = true;
        g_out.clear();
        return succeed(out);
    } catch (const std::exception &e) {
        return fail(out, e.what());
    } catch (...) {
        return fail(out, "MicroTeX initialization failed");
    }
}

KVITMATH_API void kvitmath_set_measure(KvitMathMeasure measure) { g_measure = measure; }

KVITMATH_API int32_t kvitmath_render(const char *tex, int64_t texLen, int32_t sizePx, int32_t flags,
                                     KvitMathResult *out) {
    if (!g_inited)
        return fail(out, "the math engine is not initialised");
    try {
        const std::wstring source = toWide(tex, texLen);
        const float size = static_cast<float>(sizePx > 0 ? sizePx : 20);
        std::unique_ptr<tex::TeXRender> render(parse(source, size, (flags & KVITMATH_DISPLAY) != 0));
        if (!render)
            return fail(out, "Unrenderable expression");
        g_out.clear();
        if ((flags & KVITMATH_DRAW) != 0) {
            tex::Graphics2D_rec g2(g_out);
            render->draw(g2, 0, 0);
        }
        if (out != nullptr) {
            out->width = render->getExactWidth();
            out->height = render->getExactHeight();
            out->depth = render->getExactDepth();
            out->baseline = render->getExactBaseline();
        }
        return succeed(out);
    } catch (const std::exception &e) {
        return fail(out, e.what());
    } catch (...) {
        return fail(out, "Unrenderable expression");
    }
}

KVITMATH_API int32_t kvitmath_commands(KvitMathResult *out) {
    if (!g_inited)
        return fail(out, "the math engine is not initialised");
    try {
        std::set<std::string> names;
        for (const auto &entry : tex::SymbolAtom::_symbols) {
            if (!entry.first.empty() && entry.first.find('@') == std::string::npos)
                names.insert(entry.first);
        }
        for (const auto &entry : tex::MacroInfo::_commands) {
            const std::string name = toUtf8(entry.first);
            if (!name.empty() && name.find('@') == std::string::npos)
                names.insert(name);
        }
        g_out.clear();
        for (const auto &name : names) {
            g_out += name;
            g_out += '\n';
        }
        if (out != nullptr)
            out->width = out->height = out->depth = out->baseline = 0;
        return succeed(out);
    } catch (const std::exception &e) {
        return fail(out, e.what());
    } catch (...) {
        return fail(out, "listing the commands failed");
    }
}

}  // extern "C"
