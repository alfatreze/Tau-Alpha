# Snippets: the small helpers every meter ends up writing

Copy what you need. They are plain integer C, they use nothing from the host, and `sim/test_sdk_snippets.py` in the Tau Alpha repository compiles every block below and checks its values, so they do not rot.

## min, max, clamp
```c
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iclamp(int x, int lo, int hi) { return imin(imax(x, lo), hi); }
```

## A sine from a 65-entry table (no floating point)
`isin(phase)` takes a phase in 256 steps per turn (0..255, any larger value wraps) and returns -1024..1024. Scale with `x * isin(p) / 1024`.
```c
static const short sine_quarter[65] = { 0, 25, 50, 75, 100, 125, 150, 175, 200, 224, 249, 273, 297, 321, 345, 369, 392, 415, 438, 460, 483, 505, 526, 548, 569, 590, 610, 630, 650, 669, 688, 706, 724, 742, 759, 775, 792, 807, 822, 837, 851, 865, 878, 891, 903, 915, 926, 936, 946, 955, 964, 972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024, 1024 };
static int isin(unsigned phase)
{
    unsigned part = phase & 63u, quadrant = (phase >> 6) & 3u;
    int v = sine_quarter[(quadrant & 1u) ? 64u - part : part];
    return quadrant >= 2u ? -v : v;
}
```

## Mix two RGB565 colours
`t` is 0 (all `a`) to 32 (all `b`). The three channels are mixed separately: averaging the 16-bit numbers directly gives wrong colours.
```c
static unsigned short mix565(unsigned short a, unsigned short b, unsigned t)
{
    unsigned u = 32u - t;
    unsigned r = (((a >> 11) * u + (b >> 11) * t) >> 5) << 11;
    unsigned g = ((((a >> 5) & 63u) * u + ((b >> 5) & 63u) * t) >> 5) << 5;
    unsigned bl = ((a & 31u) * u + (b & 31u) * t) >> 5;
    return (unsigned short)(r | g | bl);
}
```

## A line from rectangles
A staircase of at most two rectangles per segment. `w` is the stroke width in pixels.
```c
/* needs mtr_rect from tau_meter.h; shown without it so it compiles on its own: pass the call you use */
static void seg(void (*rect)(unsigned, unsigned, unsigned, unsigned, unsigned short), int x0, int y0, int x1, int y1, int w, unsigned short c)
{
    if (x0 != x1) rect((unsigned)imin(x0, x1), (unsigned)y0, (unsigned)(imax(x0, x1) - imin(x0, x1) + w), (unsigned)w, c);
    if (y0 != y1 || x0 == x1) rect((unsigned)x1, (unsigned)imin(y0, y1), (unsigned)w, (unsigned)(imax(y0, y1) - imin(y0, y1) + w), c);
}
```
