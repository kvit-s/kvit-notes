/*
 * kvitmath: the MicroTeX math engine as a shared library with a plain C
 * interface, loaded at run time by the Go package mathtex without cgo.
 *
 * Every function takes and returns only integers and pointers, so Go can call
 * it through purego on Linux and macOS and through syscall.SyscallN on
 * Windows, where arguments are passed as machine words. Floating-point values
 * travel inside structs. Strings are UTF-8 with an explicit length.
 *
 * MicroTeX keeps global state and is not thread-safe: the caller makes one
 * call at a time. A result's data stays valid until the next call.
 */
#ifndef KVITMATH_H
#define KVITMATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define KVITMATH_API __declspec(dllexport)
#else
#define KVITMATH_API __attribute__((visibility("default")))
#endif

/* The interface version kvitmath_version returns. Raise it whenever a
 * function, a struct or the command format below changes. */
#define KVITMATH_VERSION 1

/* Flags for kvitmath_render. */
#define KVITMATH_DISPLAY 1 /* display style, as a $$...$$ block; without it text style, as $...$ */
#define KVITMATH_DRAW 2    /* also record the drawing commands; without it only the metrics */

/* What a call returns. On success data holds the call's product (the drawing
 * commands, or the command names); on failure it holds the error message. */
typedef struct KvitMathResult {
    float width;    /* the formula's box, in pixels, unrounded */
    float height;   /* height above the baseline plus depth below it */
    float depth;    /* below the baseline */
    float baseline; /* from the top of the box down to the baseline */
    const uint8_t *data;
    int64_t len;
} KvitMathResult;

/* Text MicroTeX has no math font for (characters of scripts it does not
 * know, and \Textit and its kind) is measured by the caller: the library
 * fills text, family, style and size and calls the measure function, which
 * writes the text's bounds at that size with the origin on the baseline: x
 * and y the top-left corner (y negative above the baseline), w the advance
 * width and h the ascent plus the descent. */
typedef struct KvitMathText {
    const char *text;
    int32_t textLen;
    int32_t style; /* 1 bold, 2 italic */
    const char *family;
    int32_t familyLen;
    float size;
    float x, y, w, h;
} KvitMathText;

/* The measure function. The return value is ignored; it exists because a Go
 * callback must return one machine word. */
typedef uintptr_t (*KvitMathMeasure)(KvitMathText *text);

/* KVITMATH_VERSION of the library. */
KVITMATH_API int32_t kvitmath_version(void);

/* Initialises the engine once against a resource directory (the fonts and the
 * XML files MicroTeX reads). Returns 1, or 0 with the message in out. */
KVITMATH_API int32_t kvitmath_init(const char *resRoot, int64_t resRootLen, KvitMathResult *out);

/* Sets the measure function; 0 falls back to an estimate. */
KVITMATH_API void kvitmath_set_measure(KvitMathMeasure measure);

/* Lays out tex at sizePx pixels per em. Returns 1 with the metrics, and with
 * KVITMATH_DRAW the drawing commands in data; or 0 with the parse error. */
KVITMATH_API int32_t kvitmath_render(const char *tex, int64_t texLen, int32_t sizePx, int32_t flags,
                                     KvitMathResult *out);

/* Every command name a user can type, without the backslash, sorted, one per
 * line. Names holding '@' are internal and left out. Returns 1, or 0. */
KVITMATH_API int32_t kvitmath_commands(KvitMathResult *out);

/*
 * The drawing commands are a byte stream, little-endian, one command after
 * another. Every command except FONT starts with
 *
 *     u8 op, u32 colour, f32 a, b, c, d, e, f
 *
 * The colour is 0xAARRGGBB, or 0 for the foreground colour the caller draws
 * in (a colour set with \color is kept). The six numbers are the transform in
 * force, mapping the command's coordinates to pixels with the formula's
 * top-left corner at the origin: x' = a*x + c*y + e, y' = b*x + d*y + f.
 */
#define KVITMATH_OP_FONT 1  /* u16 id, u16 n, n bytes: the path of font file id, given before its first use */
#define KVITMATH_OP_GLYPH 2 /* u16 font, u32 character, f32 x, y, size: a character on the baseline at x, y */
#define KVITMATH_OP_LINE 3  /* f32 x1, y1, x2, y2, stroke */
#define KVITMATH_OP_RECT 4  /* f32 x, y, w, h, rx, ry, u8 filled, stroke: a rectangle, rounded when rx or ry > 0 */
#define KVITMATH_OP_TEXT 5  /* f32 x, y, size, u8 style, u16 f, u32 t, f bytes family, t bytes text: measured text */
/* A stroke is f32 width, u8 cap (0 butt, 1 round, 2 square), u8 join (0 bevel, 1 miter, 2 round). */

#ifdef __cplusplus
}
#endif

#endif /* KVITMATH_H */
