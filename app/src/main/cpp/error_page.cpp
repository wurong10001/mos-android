#include "error_page.h"

#include "error_page_font.h"
#include "log.h"
#include "strings.h"

#include <cstdlib>
#include <cstring>

namespace {

constexpr uint32_t kColorBg     = 0xFF101418;  // dark blue-grey background
constexpr uint32_t kColorTitle  = 0xFFFF4D4D;  // red title
constexpr uint32_t kColorText   = 0xFFE6E6E6;  // light grey body
constexpr uint32_t kColorHint   = 0xFFFFC94D;  // yellow hint
constexpr int kScale = 2;                      // glyph scale
constexpr int kMargin = 16;
constexpr int kLineHeight = font8x16::kCharH * kScale + 6;

// UTF-8 decode one codepoint; *advance = bytes consumed
uint32_t utf8Next(const char* s, int* advance)
{
    const unsigned char c = static_cast<unsigned char>(s[0]);
    if (c < 0x80) {
        *advance = 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && s[1] != 0) {
        *advance = 2;
        return (static_cast<uint32_t>(c & 0x1F) << 6) |
               (static_cast<uint32_t>(s[1]) & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && s[1] != 0 && s[2] != 0) {
        *advance = 3;
        return (static_cast<uint32_t>(c & 0x0F) << 12) |
               ((static_cast<uint32_t>(s[1]) & 0x3F) << 6) |
               (static_cast<uint32_t>(s[2]) & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && s[1] != 0 && s[2] != 0 && s[3] != 0) {
        *advance = 4;
        return (static_cast<uint32_t>(c & 0x07) << 18) |
               ((static_cast<uint32_t>(s[1]) & 0x3F) << 12) |
               ((static_cast<uint32_t>(s[2]) & 0x3F) << 6) |
               (static_cast<uint32_t>(s[3]) & 0x3F);
    }
    *advance = 1;
    return 0xFFFD;  // replacement char
}

// codepoint advance width in pixels
int charWidth(uint32_t cp)
{
    return cp < 0x80 ? font8x16::kCharW : 16;
}

// binary search in the CJK table (sorted by codepoint)
const font8x16::CjkGlyph* findCjk(uint32_t cp)
{
    if (cp > 0xFFFF) return nullptr;
    const uint16_t key = static_cast<uint16_t>(cp);
    int lo = 0;
    int hi = font8x16::kCjkCount - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const uint16_t code = font8x16::kCjkGlyphs[mid].code;
        if (code == key) return &font8x16::kCjkGlyphs[mid];
        if (code < key) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return nullptr;
}

void drawAscii(uint32_t* dst, int stride, int winW, int winH,
               int x, int y, uint32_t cp, uint32_t color)
{
    int idx = static_cast<int>(cp) - font8x16::kFirstChar;
    if (idx < 0 || idx > font8x16::kLastChar - font8x16::kFirstChar) {
        idx = '?' - font8x16::kFirstChar;
    }
    const uint8_t* glyph = font8x16::kGlyphs[idx];
    for (int row = 0; row < font8x16::kCharH; ++row) {
        const uint8_t bits = glyph[row];
        for (int col = 0; col < font8x16::kCharW; ++col) {
            if ((bits & (0x80 >> col)) == 0) continue;
            for (int sy = 0; sy < kScale; ++sy) {
                const int py = y + row * kScale + sy;
                if (py < 0 || py >= winH) continue;
                for (int sx = 0; sx < kScale; ++sx) {
                    const int px = x + col * kScale + sx;
                    if (px < 0 || px >= winW) continue;
                    dst[static_cast<size_t>(py) * stride + px] = color;
                }
            }
        }
    }
}

void drawCjk(uint32_t* dst, int stride, int winW, int winH,
             int x, int y, const font8x16::CjkGlyph* glyph, uint32_t color)
{
    for (int row = 0; row < 16; ++row) {
        const uint16_t bits =
            (static_cast<uint16_t>(glyph->rows[row][0]) << 8) | glyph->rows[row][1];
        for (int col = 0; col < 16; ++col) {
            if ((bits & (0x8000 >> col)) == 0) continue;
            for (int sy = 0; sy < kScale; ++sy) {
                const int py = y + row * kScale + sy;
                if (py < 0 || py >= winH) continue;
                for (int sx = 0; sx < kScale; ++sx) {
                    const int px = x + col * kScale + sx;
                    if (px < 0 || px >= winW) continue;
                    dst[static_cast<size_t>(py) * stride + px] = color;
                }
            }
        }
    }
}

void drawText(uint32_t* dst, int stride, int winW, int winH,
              int x, int y, const char* text, uint32_t color)
{
    int cx = x;
    const char* p = text;
    while (*p != 0) {
        int adv = 1;
        const uint32_t cp = utf8Next(p, &adv);
        if (cp < 0x80) {
            drawAscii(dst, stride, winW, winH, cx, y, cp, color);
        } else {
            const font8x16::CjkGlyph* g = findCjk(cp);
            if (g != nullptr) {
                drawCjk(dst, stride, winW, winH, cx, y, g, color);
            } else {
                drawAscii(dst, stride, winW, winH, cx, y, '?', color);
            }
        }
        cx += charWidth(cp) * kScale;
        p += adv;
    }
}

}  // namespace

void ErrorPage::open(const char* pageTitle, const std::string& body)
{
    active = true;
    exitRequested = false;
    clearLogRequested = false;
    title = pageTitle != nullptr ? pageTitle : "Error";
    lines.clear();

    // split by newline (over-long lines are wrapped in draw by screen width)
    size_t start = 0;
    while (start <= body.size()) {
        size_t nl = body.find(10, start);
        std::string seg = body.substr(start, nl == std::string::npos
                                                  ? std::string::npos
                                                  : nl - start);
        while (!seg.empty() && seg.back() == 13) seg.pop_back();
        lines.push_back(seg);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    LOGI("error page opened: %s (%zu lines)", title.c_str(), lines.size());
}

void ErrorPage::close()
{
    active = false;
    exitRequested = false;
    clearLogRequested = false;
    lines.clear();
}

void ErrorPage::draw(ANativeWindow* window)
{
    if (window == nullptr || !active) return;

    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(window, &buffer, nullptr) != 0) return;

    uint32_t* dst = static_cast<uint32_t*>(buffer.bits);
    const int stride = static_cast<int>(buffer.stride);
    const int winW = static_cast<int>(buffer.width);
    const int winH = static_cast<int>(buffer.height);

    // background
    for (int y = 0; y < winH; ++y) {
        uint32_t* row = dst + static_cast<size_t>(y) * stride;
        for (int x = 0; x < winW; ++x) row[x] = kColorBg;
    }

    const int maxWidthPx = winW - kMargin * 2;
    const int maxRows = (winH - kMargin * 2 - kLineHeight * 2) / kLineHeight;

    int y = kMargin;
    int shown = 0;

    // title
    drawText(dst, stride, winW, winH, kMargin, y, title.c_str(), kColorTitle);
    y += kLineHeight + 8;

    // body: hard-wrap each line to fit the width (by codepoint)
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& seg = lines[i];
        size_t off = 0;
        while (off < seg.size()) {
            if (shown >= maxRows) break;
            int w = 0;
            size_t end = off;
            while (end < seg.size()) {
                int adv = 1;
                const uint32_t cp = utf8Next(seg.c_str() + end, &adv);
                const int cw = charWidth(cp) * kScale;
                if (w + cw > maxWidthPx && end > off) break;
                w += cw;
                end += adv;
            }
            const std::string piece = seg.substr(off, end - off);
            drawText(dst, stride, winW, winH, kMargin, y, piece.c_str(),
                     kColorText);
            y += kLineHeight;
            ++shown;
            off = end;
        }
        if (shown >= maxRows) break;
    }
    if (shown < static_cast<int>(lines.size())) {
        drawText(dst, stride, winW, winH, kMargin, y,
                 strings::get().hintFullLog.c_str(), kColorHint);
        y += kLineHeight;
    }

    // bottom hints (localized)
    const std::string hint = strings::get().hintTap + "   " +
                             strings::get().hintLongPress;
    drawText(dst, stride, winW, winH, kMargin, winH - kMargin - kLineHeight,
             hint.c_str(), kColorHint);

    ANativeWindow_unlockAndPost(window);
}

void ErrorPage::onTouch(bool longPress)
{
    LOGI("error page touch (longPress=%d)", longPress ? 1 : 0);
    if (longPress) {
        clearLogRequested = true;
    }
    exitRequested = true;
}
