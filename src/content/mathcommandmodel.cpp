// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "mathcommandmodel.h"

#include <algorithm>

#include "mathrenderer.h"

namespace {

// Canonical category names. Shared string constants so the ctor and
// categories() cannot drift apart.
const QString kGreek = QStringLiteral("Greek");
const QString kArrows = QStringLiteral("Arrows");
const QString kBinary = QStringLiteral("Binary operators");
const QString kRelations = QStringLiteral("Relations");
const QString kNegated = QStringLiteral("Negated relations");
const QString kBigOps = QStringLiteral("Big operators");
const QString kFracRoots = QStringLiteral("Fractions & roots");
const QString kDelimiters = QStringLiteral("Delimiters");
const QString kAccents = QStringLiteral("Accents & decorations");
const QString kScripts = QStringLiteral("Scripts & limits");
const QString kFonts = QStringLiteral("Fonts & styles");
const QString kFunctions = QStringLiteral("Functions");
const QString kStructure = QStringLiteral("Structure");
const QString kMisc = QStringLiteral("Dots & misc");
const QString kSpacing = QStringLiteral("Spacing");
const QString kRecent = QStringLiteral("Recently used");

} // namespace

MathCommandModel::MathCommandModel(QObject *parent)
    : QObject(parent)
{
    m_categories = { kGreek, kArrows, kBinary, kRelations, kNegated,
                     kBigOps, kFracRoots, kDelimiters, kAccents, kScripts,
                     kFonts, kFunctions, kStructure, kMisc, kSpacing };

    // ---- Greek ----
    addSymbols(kGreek, {
        QStringLiteral("alpha"), QStringLiteral("beta"),
        QStringLiteral("gamma"), QStringLiteral("delta"),
        QStringLiteral("epsilon"), QStringLiteral("varepsilon"),
        QStringLiteral("zeta"), QStringLiteral("eta"),
        QStringLiteral("theta"), QStringLiteral("vartheta"),
        QStringLiteral("iota"), QStringLiteral("kappa"),
        QStringLiteral("lambda"), QStringLiteral("mu"),
        QStringLiteral("nu"), QStringLiteral("xi"),
        QStringLiteral("pi"), QStringLiteral("varpi"),
        QStringLiteral("rho"), QStringLiteral("varrho"),
        QStringLiteral("sigma"), QStringLiteral("varsigma"),
        QStringLiteral("tau"), QStringLiteral("upsilon"),
        QStringLiteral("phi"), QStringLiteral("varphi"),
        QStringLiteral("chi"), QStringLiteral("psi"),
        QStringLiteral("omega"),
        QStringLiteral("Gamma"), QStringLiteral("Delta"),
        QStringLiteral("Theta"), QStringLiteral("Lambda"),
        QStringLiteral("Xi"), QStringLiteral("Pi"),
        QStringLiteral("Sigma"), QStringLiteral("Upsilon"),
        QStringLiteral("Phi"), QStringLiteral("Psi"),
        QStringLiteral("Omega"),
    });

    // ---- Arrows ----
    addSymbol(kArrows, QStringLiteral("to"),
              QStringLiteral("Right arrow"),
              { QStringLiteral("rightarrow"), QStringLiteral("arrow") });
    addSymbol(kArrows, QStringLiteral("gets"),
              QStringLiteral("Left arrow"),
              { QStringLiteral("leftarrow") });
    addSymbol(kArrows, QStringLiteral("Rightarrow"),
              QStringLiteral("Implies"),
              { QStringLiteral("implies") });
    addSymbol(kArrows, QStringLiteral("Leftarrow"),
              QStringLiteral("Implied by"));
    addSymbol(kArrows, QStringLiteral("Leftrightarrow"),
              QStringLiteral("If and only if"),
              { QStringLiteral("iff"), QStringLiteral("equivalent") });
    addSymbol(kArrows, QStringLiteral("mapsto"),
              QStringLiteral("Maps to"));
    addSymbols(kArrows, {
        QStringLiteral("leftrightarrow"),
        QStringLiteral("longrightarrow"), QStringLiteral("longleftarrow"),
        QStringLiteral("longleftrightarrow"),
        QStringLiteral("Longrightarrow"), QStringLiteral("Longleftarrow"),
        QStringLiteral("longmapsto"),
        QStringLiteral("uparrow"), QStringLiteral("downarrow"),
        QStringLiteral("updownarrow"),
        QStringLiteral("Uparrow"), QStringLiteral("Downarrow"),
        QStringLiteral("Updownarrow"),
        QStringLiteral("hookrightarrow"), QStringLiteral("hookleftarrow"),
        QStringLiteral("rightharpoonup"), QStringLiteral("rightharpoondown"),
        QStringLiteral("leftharpoonup"), QStringLiteral("leftharpoondown"),
        QStringLiteral("rightleftharpoons"),
        QStringLiteral("nearrow"), QStringLiteral("searrow"),
        QStringLiteral("swarrow"), QStringLiteral("nwarrow"),
    });

    // ---- Binary operators ----
    addSymbol(kBinary, QStringLiteral("pm"),
              QStringLiteral("Plus or minus"),
              { QStringLiteral("plusminus") });
    addSymbol(kBinary, QStringLiteral("times"),
              QStringLiteral("Multiplication cross"),
              { QStringLiteral("cross"), QStringLiteral("multiply") });
    addSymbol(kBinary, QStringLiteral("cdot"),
              QStringLiteral("Center dot"),
              { QStringLiteral("dot product") });
    addSymbol(kBinary, QStringLiteral("div"),
              QStringLiteral("Division sign"),
              { QStringLiteral("divide") });
    addSymbol(kBinary, QStringLiteral("setminus"),
              QStringLiteral("Set difference"));
    addSymbols(kBinary, {
        QStringLiteral("mp"), QStringLiteral("ast"), QStringLiteral("star"),
        QStringLiteral("circ"), QStringLiteral("bullet"),
        QStringLiteral("cap"), QStringLiteral("cup"),
        QStringLiteral("uplus"), QStringLiteral("sqcap"),
        QStringLiteral("sqcup"), QStringLiteral("vee"),
        QStringLiteral("wedge"), QStringLiteral("wr"),
        QStringLiteral("oplus"), QStringLiteral("ominus"),
        QStringLiteral("otimes"), QStringLiteral("oslash"),
        QStringLiteral("odot"), QStringLiteral("dagger"),
        QStringLiteral("ddagger"), QStringLiteral("amalg"),
        QStringLiteral("diamond"),
        QStringLiteral("bigtriangleup"), QStringLiteral("bigtriangledown"),
        QStringLiteral("triangleleft"), QStringLiteral("triangleright"),
    });

    // ---- Relations ----
    addSymbol(kRelations, QStringLiteral("le"),
              QStringLiteral("Less than or equal"),
              { QStringLiteral("leq"), QStringLiteral("<=") });
    addSymbol(kRelations, QStringLiteral("ge"),
              QStringLiteral("Greater than or equal"),
              { QStringLiteral("geq"), QStringLiteral(">=") });
    addSymbol(kRelations, QStringLiteral("ne"),
              QStringLiteral("Not equal"),
              { QStringLiteral("neq"), QStringLiteral("!=") });
    addSymbol(kRelations, QStringLiteral("approx"),
              QStringLiteral("Approximately equal"));
    addSymbol(kRelations, QStringLiteral("equiv"),
              QStringLiteral("Identical / equivalent"));
    addSymbol(kRelations, QStringLiteral("propto"),
              QStringLiteral("Proportional to"));
    addSymbol(kRelations, QStringLiteral("in"),
              QStringLiteral("Element of"),
              { QStringLiteral("element"), QStringLiteral("member") });
    addSymbols(kRelations, {
        QStringLiteral("sim"), QStringLiteral("simeq"),
        QStringLiteral("cong"), QStringLiteral("doteq"),
        QStringLiteral("prec"), QStringLiteral("preceq"),
        QStringLiteral("succ"), QStringLiteral("succeq"),
        QStringLiteral("ll"), QStringLiteral("gg"),
        QStringLiteral("subset"), QStringLiteral("supset"),
        QStringLiteral("subseteq"), QStringLiteral("supseteq"),
        QStringLiteral("sqsubseteq"), QStringLiteral("sqsupseteq"),
        QStringLiteral("ni"), QStringLiteral("vdash"),
        QStringLiteral("dashv"), QStringLiteral("models"),
        QStringLiteral("perp"), QStringLiteral("mid"),
        QStringLiteral("parallel"), QStringLiteral("asymp"),
        QStringLiteral("smile"), QStringLiteral("frown"),
        QStringLiteral("bowtie"),
    });

    // ---- Negated relations ----
    addSymbol(kNegated, QStringLiteral("notin"),
              QStringLiteral("Not an element of"));
    addSymbols(kNegated, {
        QStringLiteral("nless"), QStringLiteral("ngtr"),
        QStringLiteral("nleq"), QStringLiteral("ngeq"),
        QStringLiteral("nsubseteq"), QStringLiteral("nsupseteq"),
        QStringLiteral("nsim"), QStringLiteral("ncong"),
        QStringLiteral("nmid"), QStringLiteral("nparallel"),
        QStringLiteral("nprec"), QStringLiteral("nsucc"),
        QStringLiteral("nvdash"),
        QStringLiteral("nRightarrow"), QStringLiteral("nLeftarrow"),
    });

    // ---- Big operators ----
    addSymbol(kBigOps, QStringLiteral("sum"),
              QStringLiteral("Sum"), { QStringLiteral("sigma") });
    addSymbol(kBigOps, QStringLiteral("prod"),
              QStringLiteral("Product"));
    addSymbol(kBigOps, QStringLiteral("int"),
              QStringLiteral("Integral"), { QStringLiteral("integral") });
    addSymbol(kBigOps, QStringLiteral("oint"),
              QStringLiteral("Contour integral"));
    addSymbols(kBigOps, {
        QStringLiteral("iint"), QStringLiteral("iiint"),
        QStringLiteral("coprod"),
        QStringLiteral("bigcap"), QStringLiteral("bigcup"),
        QStringLiteral("bigsqcup"), QStringLiteral("bigvee"),
        QStringLiteral("bigwedge"), QStringLiteral("bigoplus"),
        QStringLiteral("bigotimes"), QStringLiteral("bigodot"),
        QStringLiteral("biguplus"),
    });

    // ---- Fractions & roots ----
    addTemplate(kFracRoots, QStringLiteral("\\frac"),
                QStringLiteral("\\frac{}{}"), QStringLiteral("\\frac{a}{b}"),
                QStringLiteral("Fraction"),
                { QStringLiteral("fraction"), QStringLiteral("over") });
    addTemplate(kFracRoots, QStringLiteral("\\tfrac"),
                QStringLiteral("\\tfrac{}{}"), QStringLiteral("\\tfrac{a}{b}"),
                QStringLiteral("Inline-size fraction"));
    addTemplate(kFracRoots, QStringLiteral("\\dfrac"),
                QStringLiteral("\\dfrac{}{}"), QStringLiteral("\\dfrac{a}{b}"),
                QStringLiteral("Display-size fraction"));
    addTemplate(kFracRoots, QStringLiteral("\\binom"),
                QStringLiteral("\\binom{}{}"), QStringLiteral("\\binom{n}{k}"),
                QStringLiteral("Binomial coefficient"),
                { QStringLiteral("choose"), QStringLiteral("combination") });
    addTemplate(kFracRoots, QStringLiteral("\\sqrt"),
                QStringLiteral("\\sqrt{}"), QStringLiteral("\\sqrt{x}"),
                QStringLiteral("Square root"),
                { QStringLiteral("root"), QStringLiteral("radical") });
    addTemplate(kFracRoots, QStringLiteral("\\sqrt[n]"),
                QStringLiteral("\\sqrt[]{}"), QStringLiteral("\\sqrt[n]{x}"),
                QStringLiteral("nth root"),
                { QStringLiteral("root"), QStringLiteral("nthroot"),
                  QStringLiteral("cube root") });

    // ---- Delimiters ----
    addTemplate(kDelimiters, QStringLiteral("\\left(\\right)"),
                QStringLiteral("\\left( \\right)"),
                QStringLiteral("\\left( x \\right)"),
                QStringLiteral("Parentheses, auto-sized"),
                { QStringLiteral("paren"), QStringLiteral("parentheses"),
                  QStringLiteral("()") });
    addTemplate(kDelimiters, QStringLiteral("\\left[\\right]"),
                QStringLiteral("\\left[ \\right]"),
                QStringLiteral("\\left[ x \\right]"),
                QStringLiteral("Brackets, auto-sized"),
                { QStringLiteral("bracket"), QStringLiteral("[]") });
    addTemplate(kDelimiters, QStringLiteral("\\left\\{\\right\\}"),
                QStringLiteral("\\left\\{ \\right\\}"),
                QStringLiteral("\\left\\{ x \\right\\}"),
                QStringLiteral("Braces, auto-sized"),
                { QStringLiteral("brace"), QStringLiteral("set"),
                  QStringLiteral("{}") });
    addTemplate(kDelimiters, QStringLiteral("\\left|\\right|"),
                QStringLiteral("\\left| \\right|"),
                QStringLiteral("\\left| x \\right|"),
                QStringLiteral("Absolute value, auto-sized"),
                { QStringLiteral("abs"), QStringLiteral("modulus") });
    addTemplate(kDelimiters, QStringLiteral("\\left\\langle\\right\\rangle"),
                QStringLiteral("\\left\\langle \\right\\rangle"),
                QStringLiteral("\\left\\langle x \\right\\rangle"),
                QStringLiteral("Angle brackets, auto-sized"),
                { QStringLiteral("angle brackets"), QStringLiteral("inner product") });
    addSymbols(kDelimiters, {
        QStringLiteral("langle"), QStringLiteral("rangle"),
        QStringLiteral("lceil"), QStringLiteral("rceil"),
        QStringLiteral("lfloor"), QStringLiteral("rfloor"),
    });
    addTemplate(kDelimiters, QStringLiteral("\\|"),
                QStringLiteral("\\|"), QStringLiteral("\\|"),
                QStringLiteral("Double vertical bar"),
                { QStringLiteral("Vert"), QStringLiteral("norm") });

    // ---- Accents & decorations ----
    const struct { const char *cmd; const char *desc; } accents[] = {
        { "hat", "Hat accent" }, { "widehat", "Wide hat" },
        { "bar", "Bar accent" }, { "overline", "Overline" },
        { "underline", "Underline" }, { "vec", "Vector arrow" },
        { "dot", "Dot accent" }, { "ddot", "Double dot accent" },
        { "tilde", "Tilde accent" }, { "widetilde", "Wide tilde" },
        { "check", "Check accent" }, { "breve", "Breve accent" },
        { "acute", "Acute accent" }, { "grave", "Grave accent" },
        { "mathring", "Ring accent" },
        { "overrightarrow", "Arrow over" },
        { "overleftarrow", "Left arrow over" },
    };
    for (const auto &a : accents) {
        const QString cmd = QString::fromLatin1(a.cmd);
        addTemplate(kAccents, QStringLiteral("\\") + cmd,
                    QStringLiteral("\\") + cmd + QStringLiteral("{}"),
                    QStringLiteral("\\") + cmd + QStringLiteral("{x}"),
                    QString::fromLatin1(a.desc));
    }
    addTemplate(kAccents, QStringLiteral("\\overbrace"),
                QStringLiteral("\\overbrace{}"),
                QStringLiteral("\\overbrace{abc}"),
                QStringLiteral("Brace over"));
    addTemplate(kAccents, QStringLiteral("\\underbrace"),
                QStringLiteral("\\underbrace{}"),
                QStringLiteral("\\underbrace{abc}"),
                QStringLiteral("Brace under"));

    // ---- Scripts & limits ----
    addTemplate(kScripts, QStringLiteral("^{}"),
                QStringLiteral("^{}"), QStringLiteral("x^{2}"),
                QStringLiteral("Superscript"),
                { QStringLiteral("sup"), QStringLiteral("superscript"),
                  QStringLiteral("power"), QStringLiteral("exponent") },
                QString(), false);
    addTemplate(kScripts, QStringLiteral("_{}"),
                QStringLiteral("_{}"), QStringLiteral("x_{i}"),
                QStringLiteral("Subscript"),
                { QStringLiteral("sub"), QStringLiteral("subscript"),
                  QStringLiteral("index") },
                QString(), false);
    addTemplate(kScripts, QStringLiteral("_{}^{}"),
                QStringLiteral("_{}^{}"), QStringLiteral("x_{i}^{2}"),
                QStringLiteral("Sub- and superscript"),
                { QStringLiteral("subsup") },
                QString(), false);
    addTemplate(kScripts, QStringLiteral("\\overset"),
                QStringLiteral("\\overset{}{}"),
                QStringLiteral("\\overset{a}{=}"),
                QStringLiteral("Symbol above another"));
    addTemplate(kScripts, QStringLiteral("\\underset"),
                QStringLiteral("\\underset{}{}"),
                QStringLiteral("\\underset{a}{=}"),
                QStringLiteral("Symbol below another"));
    addTemplate(kScripts, QStringLiteral("\\stackrel"),
                QStringLiteral("\\stackrel{}{}"),
                QStringLiteral("\\stackrel{a}{=}"),
                QStringLiteral("Stack above a relation"));
    addTemplate(kScripts, QStringLiteral("\\limits"),
                QStringLiteral("\\limits"),
                QStringLiteral("\\sum\\limits_{i=0}^{n}"),
                QStringLiteral("Force limits above/below"),
                {}, QString(), false);
    addTemplate(kScripts, QStringLiteral("\\nolimits"),
                QStringLiteral("\\nolimits"),
                QStringLiteral("\\int\\nolimits_{0}^{1}"),
                QStringLiteral("Force limits to the side"),
                {}, QString(), false);

    // ---- Fonts & styles ----
    const struct { const char *cmd; const char *sample; const char *desc;
                   const char *alias; } fonts[] = {
        { "mathbb", "R", "Blackboard bold", "blackboard" },
        { "mathcal", "L", "Calligraphic", "calligraphic" },
        { "mathfrak", "g", "Fraktur", "fraktur" },
        { "mathrm", "d", "Upright roman", "roman" },
        { "mathbf", "v", "Bold", "bold" },
        { "mathit", "f", "Italic", "italic" },
        { "mathsf", "S", "Sans-serif", "sans" },
        { "mathtt", "t", "Typewriter", "monospace" },
        { "mathscr", "F", "Script", "script" },
    };
    for (const auto &f : fonts) {
        const QString cmd = QString::fromLatin1(f.cmd);
        addTemplate(kFonts, QStringLiteral("\\") + cmd,
                    QStringLiteral("\\") + cmd + QStringLiteral("{}"),
                    QStringLiteral("\\") + cmd + QStringLiteral("{")
                        + QString::fromLatin1(f.sample) + QStringLiteral("}"),
                    QString::fromLatin1(f.desc),
                    { QString::fromLatin1(f.alias) });
    }
    addTemplate(kFonts, QStringLiteral("\\boldsymbol"),
                QStringLiteral("\\boldsymbol{}"),
                QStringLiteral("\\boldsymbol{\\alpha}"),
                QStringLiteral("Bold symbol"),
                { QStringLiteral("bold greek") });
    addTemplate(kFonts, QStringLiteral("\\text"),
                QStringLiteral("\\text{}"),
                QStringLiteral("\\text{text}"),
                QStringLiteral("Upright text in math"),
                { QStringLiteral("words"), QStringLiteral("label") });

    // ---- Functions ----
    addSymbols(kFunctions, {
        QStringLiteral("sin"), QStringLiteral("cos"), QStringLiteral("tan"),
        QStringLiteral("cot"), QStringLiteral("sec"), QStringLiteral("csc"),
        QStringLiteral("arcsin"), QStringLiteral("arccos"),
        QStringLiteral("arctan"),
        QStringLiteral("sinh"), QStringLiteral("cosh"),
        QStringLiteral("tanh"), QStringLiteral("coth"),
        QStringLiteral("log"), QStringLiteral("ln"), QStringLiteral("lg"),
        QStringLiteral("exp"), QStringLiteral("lim"),
        QStringLiteral("limsup"), QStringLiteral("liminf"),
        QStringLiteral("max"), QStringLiteral("min"),
        QStringLiteral("sup"), QStringLiteral("inf"),
        QStringLiteral("det"), QStringLiteral("dim"), QStringLiteral("ker"),
        QStringLiteral("gcd"), QStringLiteral("hom"), QStringLiteral("arg"),
        QStringLiteral("deg"), QStringLiteral("Pr"),
    });
    addTemplate(kFunctions, QStringLiteral("\\operatorname"),
                QStringLiteral("\\operatorname{}"),
                QStringLiteral("\\operatorname{lcm}"),
                QStringLiteral("Custom upright operator"));

    // ---- Structure (environments; the display form is multi-line) ----
    const struct { const char *env; const char *desc; } matrices[] = {
        { "pmatrix", "Matrix in parentheses" },
        { "bmatrix", "Matrix in brackets" },
        { "Bmatrix", "Matrix in braces" },
        { "vmatrix", "Determinant bars" },
        { "Vmatrix", "Matrix in double bars" },
        { "matrix", "Matrix, no delimiters" },
        { "smallmatrix", "Small inline matrix" },
    };
    for (const auto &m : matrices) {
        const QString env = QString::fromLatin1(m.env);
        addTemplate(kStructure,
                    QStringLiteral("\\begin{") + env + QStringLiteral("}"),
                    QStringLiteral("\\begin{") + env
                        + QStringLiteral("} & \\\\ & \\end{") + env
                        + QStringLiteral("}"),
                    QStringLiteral("\\begin{") + env
                        + QStringLiteral("}a&b\\\\c&d\\end{") + env
                        + QStringLiteral("}"),
                    QString::fromLatin1(m.desc),
                    { env, QStringLiteral("matrix") },
                    QStringLiteral("\\begin{") + env
                        + QStringLiteral("}\n & \\\\\n & \n\\end{") + env
                        + QStringLiteral("}"));
    }
    addTemplate(kStructure, QStringLiteral("\\begin{cases}"),
                QStringLiteral("\\begin{cases} & \\\\ & \\end{cases}"),
                QStringLiteral("\\begin{cases}a&x>0\\\\b&x\\le 0\\end{cases}"),
                QStringLiteral("Piecewise cases"),
                { QStringLiteral("cases"), QStringLiteral("piecewise") },
                QStringLiteral("\\begin{cases}\n & \\\\\n & \n\\end{cases}"));
    addTemplate(kStructure, QStringLiteral("\\begin{aligned}"),
                QStringLiteral("\\begin{aligned} &= \\\\ &= \\end{aligned}"),
                QStringLiteral("\\begin{aligned}a&=b\\\\c&=d\\end{aligned}"),
                QStringLiteral("Aligned equations"),
                { QStringLiteral("aligned"), QStringLiteral("align") },
                QStringLiteral("\\begin{aligned}\n &= \\\\\n &= \n\\end{aligned}"));
    addTemplate(kStructure, QStringLiteral("\\begin{array}"),
                QStringLiteral("\\begin{array}{cc} & \\\\ & \\end{array}"),
                QStringLiteral("\\begin{array}{cc}a&b\\\\c&d\\end{array}"),
                QStringLiteral("Array with column spec"),
                { QStringLiteral("array"), QStringLiteral("table") },
                QStringLiteral("\\begin{array}{cc}\n & \\\\\n & \n\\end{array}"));
    addTemplate(kStructure, QStringLiteral("\\\\"),
                QStringLiteral("\\\\"), QString(),
                QStringLiteral("New row / line break"),
                { QStringLiteral("\\"), QStringLiteral("newline"),
                  QStringLiteral("row"), QStringLiteral("linebreak") },
                QString(), false);
    addTemplate(kStructure, QStringLiteral("&"),
                QStringLiteral("&"), QString(),
                QStringLiteral("Next cell / alignment point"),
                { QStringLiteral("cell"), QStringLiteral("align"),
                  QStringLiteral("ampersand") },
                QString(), false);

    // ---- Dots & misc ----
    addSymbol(kMisc, QStringLiteral("infty"),
              QStringLiteral("Infinity"), { QStringLiteral("infinity") });
    addSymbol(kMisc, QStringLiteral("partial"),
              QStringLiteral("Partial derivative"),
              { QStringLiteral("derivative") });
    addSymbol(kMisc, QStringLiteral("nabla"),
              QStringLiteral("Nabla / gradient"),
              { QStringLiteral("del"), QStringLiteral("gradient") });
    addSymbol(kMisc, QStringLiteral("emptyset"),
              QStringLiteral("Empty set"));
    addSymbol(kMisc, QStringLiteral("neg"),
              QStringLiteral("Logical not"), { QStringLiteral("lnot") });
    addSymbols(kMisc, {
        QStringLiteral("cdots"), QStringLiteral("ldots"),
        QStringLiteral("vdots"), QStringLiteral("ddots"),
        QStringLiteral("forall"), QStringLiteral("exists"),
        QStringLiteral("nexists"),
        QStringLiteral("hbar"), QStringLiteral("ell"),
        QStringLiteral("aleph"), QStringLiteral("Re"), QStringLiteral("Im"),
        QStringLiteral("wp"), QStringLiteral("prime"),
        QStringLiteral("angle"), QStringLiteral("triangle"),
        QStringLiteral("top"), QStringLiteral("bot"),
        QStringLiteral("surd"), QStringLiteral("imath"),
        QStringLiteral("jmath"),
    });

    // ---- Spacing ----
    addTemplate(kSpacing, QStringLiteral("\\quad"),
                QStringLiteral("\\quad"), QStringLiteral("a\\quad b"),
                QStringLiteral("Quad space"), { QStringLiteral("space") });
    addTemplate(kSpacing, QStringLiteral("\\qquad"),
                QStringLiteral("\\qquad"), QStringLiteral("a\\qquad b"),
                QStringLiteral("Double quad space"));
    addTemplate(kSpacing, QStringLiteral("\\,"),
                QStringLiteral("\\,"), QStringLiteral("a\\,b"),
                QStringLiteral("Thin space"),
                { QStringLiteral("thinspace"), QStringLiteral("space") },
                QString(), false);
    addTemplate(kSpacing, QStringLiteral("\\;"),
                QStringLiteral("\\;"), QStringLiteral("a\\;b"),
                QStringLiteral("Thick space"),
                { QStringLiteral("thickspace") },
                QString(), false);
    addTemplate(kSpacing, QStringLiteral("\\:"),
                QStringLiteral("\\:"), QStringLiteral("a\\:b"),
                QStringLiteral("Medium space"),
                { QStringLiteral("medspace") },
                QString(), false);
    addTemplate(kSpacing, QStringLiteral("\\!"),
                QStringLiteral("\\!"), QStringLiteral("a\\!b"),
                QStringLiteral("Negative thin space"),
                { QStringLiteral("negspace") },
                QString(), false);
}

void MathCommandModel::addSymbol(const QString &category, const QString &command,
                                 const QString &description,
                                 const QStringList &aliases)
{
    Entry e;
    e.name = QStringLiteral("\\") + command;
    e.command = command;
    e.category = category;
    e.description = description.isEmpty() ? command : description;
    e.insert = e.name;
    e.preview = e.name;
    e.aliases = aliases;
    m_catalog.append(e);
    m_curatedCommands.insert(command);
}

void MathCommandModel::addSymbols(const QString &category,
                                  const QStringList &commands)
{
    for (const QString &command : commands)
        addSymbol(category, command);
}

void MathCommandModel::addTemplate(const QString &category, const QString &name,
                                   const QString &insert, const QString &preview,
                                   const QString &description,
                                   const QStringList &aliases,
                                   const QString &insertDisplay,
                                   bool standalone)
{
    Entry e;
    e.name = name;
    e.category = category;
    e.description = description;
    e.insert = insert;
    e.insertDisplay = insertDisplay;
    e.preview = preview;
    e.aliases = aliases;
    e.standalone = standalone;
    // The bare engine command, for enumeration dedup: the leading run of
    // letters after a backslash ("\sqrt[n]" and "\sqrt{}" both map to
    // "sqrt"). Non-backslash fragments ("^{}", "&") dedup nothing.
    if (name.startsWith(QLatin1Char('\\'))) {
        int end = 1;
        while (end < name.size() && name.at(end).isLetter())
            ++end;
        if (end > 1)
            e.command = name.mid(1, end - 1);
    }
    m_catalog.append(e);
    if (!e.command.isEmpty())
        m_curatedCommands.insert(e.command);
}

bool MathCommandModel::isSubsequence(const QString &needle,
                                     const QString &haystack)
{
    int n = 0;
    for (int h = 0; h < haystack.size() && n < needle.size(); ++h) {
        if (haystack.at(h) == needle.at(n))
            ++n;
    }
    return n == needle.size();
}

MathCommandModel::MatchTier MathCommandModel::tierOf(const QString &candidate,
                                                     const QString &query,
                                                     Qt::CaseSensitivity cs)
{
    const QString cand = cs == Qt::CaseSensitive ? candidate
                                                 : candidate.toLower();
    const QString q = cs == Qt::CaseSensitive ? query : query.toLower();
    if (cand.startsWith(q))
        return PrefixMatch;
    if (cand.contains(q))
        return SubstringMatch;
    if (isSubsequence(q, cand))
        return SubsequenceMatch;
    return NoMatch;
}

MathCommandModel::MatchQuality
MathCommandModel::bestQuality(const QStringList &candidates,
                              const QString &query)
{
    MatchQuality best;
    for (const QString &candidate : candidates) {
        const MatchTier exact = tierOf(candidate, query, Qt::CaseSensitive);
        if (exact != NoMatch && (best.caseQuality > 0 || exact < best.tier)) {
            best.caseQuality = 0;
            best.tier = exact;
            if (exact == PrefixMatch)
                return best;  // nothing beats a case-exact prefix
            continue;
        }
        if (best.caseQuality == 0)
            continue;  // an exact-case match already found; only better exact wins
        const MatchTier loose = tierOf(candidate, query, Qt::CaseInsensitive);
        if (loose < best.tier)
            best.tier = loose;
    }
    return best;
}

int MathCommandModel::caretOffsetFor(const QString &insert)
{
    const int brace = insert.indexOf(QLatin1String("{}"));
    const int bracket = insert.indexOf(QLatin1String("[]"));
    int slot = -1;
    if (brace >= 0 && (bracket < 0 || brace < bracket))
        slot = brace + 1;
    else if (bracket >= 0)
        slot = bracket + 1;
    if (slot >= 0)
        return slot;
    const int amp = insert.indexOf(QLatin1Char('&'));
    if (amp >= 0)
        return amp;
    const int right = insert.indexOf(QLatin1String(" \\right"));
    if (right >= 0)
        return right + 1;
    return -1;
}

QVariantMap MathCommandModel::entryRow(const Entry &entry) const
{
    return {
        { QStringLiteral("kind"), QStringLiteral("entry") },
        { QStringLiteral("name"), entry.name },
        { QStringLiteral("description"), entry.description },
        { QStringLiteral("category"), entry.category },
        { QStringLiteral("insert"), entry.insert },
        { QStringLiteral("insertDisplay"), entry.insertDisplay },
        { QStringLiteral("cursorOffset"), caretOffsetFor(entry.insert) },
        { QStringLiteral("cursorOffsetDisplay"),
          entry.insertDisplay.isEmpty() ? -1
                                        : caretOffsetFor(entry.insertDisplay) },
        { QStringLiteral("preview"), entry.preview },
        { QStringLiteral("standalone"), entry.standalone },
        { QStringLiteral("curated"), true },
    };
}

QVariantMap MathCommandModel::enumeratedRow(const QString &command) const
{
    const QString name = QStringLiteral("\\") + command;
    return {
        { QStringLiteral("kind"), QStringLiteral("entry") },
        { QStringLiteral("name"), name },
        { QStringLiteral("description"), QString() },
        { QStringLiteral("category"), QString() },
        { QStringLiteral("insert"), name },
        { QStringLiteral("insertDisplay"), QString() },
        { QStringLiteral("cursorOffset"), -1 },
        { QStringLiteral("cursorOffsetDisplay"), -1 },
        { QStringLiteral("preview"), name },
        { QStringLiteral("standalone"), true },
        { QStringLiteral("curated"), false },
    };
}

QStringList MathCommandModel::matchCandidates(const Entry &entry) const
{
    QStringList candidates;
    QString bare = entry.name;
    if (bare.startsWith(QLatin1Char('\\')))
        bare.remove(0, 1);
    candidates.append(bare);
    if (!entry.command.isEmpty() && entry.command != bare)
        candidates.append(entry.command);
    candidates.append(entry.aliases);
    return candidates;
}

QStringList MathCommandModel::categories() const
{
    QStringList result;
    if (!m_recent.isEmpty())
        result.append(kRecent);
    result.append(m_categories);
    return result;
}

QVariantList MathCommandModel::itemsForCategory(const QString &category) const
{
    QVariantList rows;
    if (category == kRecent) {
        // Curated entries resolve by display name; anything else (an
        // accepted enumerated completion) synthesizes a row, dropped when
        // the engine no longer knows the command (stale settings value).
        const QStringList known = MathRenderer::availableCommands();
        for (const QString &name : m_recent) {
            bool found = false;
            for (const Entry &entry : m_catalog) {
                if (entry.name == name) {
                    rows.append(entryRow(entry));
                    found = true;
                    break;
                }
            }
            if (!found && name.startsWith(QLatin1Char('\\'))
                && known.contains(name.mid(1)))
                rows.append(enumeratedRow(name.mid(1)));
        }
        return rows;
    }
    for (const Entry &entry : m_catalog) {
        if (entry.category == category)
            rows.append(entryRow(entry));
    }
    return rows;
}

QVariantList MathCommandModel::itemsFor(const QString &query) const
{
    const QString q = query.trimmed();
    QVariantList rows;

    if (q.isEmpty()) {
        for (const Entry &entry : m_catalog)
            rows.append(entryRow(entry));
        for (const QString &command : MathRenderer::availableCommands()) {
            if (!m_curatedCommands.contains(command))
                rows.append(enumeratedRow(command));
        }
        return rows;
    }

    struct Ranked {
        int caseQuality;
        int tier;
        int curated;   // 0 curated, 1 enumerated
        QString sortName;
        QVariantMap row;
    };
    QList<Ranked> matches;

    for (const Entry &entry : m_catalog) {
        const MatchQuality quality = bestQuality(matchCandidates(entry), q);
        if (quality.tier == NoMatch)
            continue;
        matches.append({ quality.caseQuality, quality.tier, 0,
                         entry.name.toLower(), entryRow(entry) });
    }
    for (const QString &command : MathRenderer::availableCommands()) {
        if (m_curatedCommands.contains(command))
            continue;
        const MatchQuality quality = bestQuality({ command }, q);
        if (quality.tier == NoMatch)
            continue;
        matches.append({ quality.caseQuality, quality.tier, 1,
                         command.toLower(), enumeratedRow(command) });
    }

    std::stable_sort(matches.begin(), matches.end(),
                     [](const Ranked &a, const Ranked &b) {
        if (a.caseQuality != b.caseQuality)
            return a.caseQuality < b.caseQuality;
        if (a.tier != b.tier)
            return a.tier < b.tier;
        if (a.curated != b.curated)
            return a.curated < b.curated;
        return a.sortName < b.sortName;
    });
    for (const Ranked &match : matches)
        rows.append(match.row);
    return rows;
}

void MathCommandModel::noteUsed(const QString &name)
{
    if (name.isEmpty())
        return;
    m_recent.removeAll(name);
    m_recent.prepend(name);
    while (m_recent.size() > MaxRecent)
        m_recent.removeLast();
    emit recentChanged();
}

QVariantList MathCommandModel::recentCommands() const
{
    QVariantList list;
    for (const QString &name : m_recent)
        list.append(name);
    return list;
}

void MathCommandModel::setRecentCommands(const QVariantList &names)
{
    m_recent.clear();
    for (const QVariant &value : names) {
        const QString name = value.toString();
        if (name.isEmpty() || m_recent.contains(name))
            continue;
        m_recent.append(name);
        if (m_recent.size() == MaxRecent)
            break;
    }
}
