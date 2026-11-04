// textrender_core.h - Pure C++14 layout, tag-parsing, and word-wrapping engine for TextRenderBase
#ifndef TEXTRENDER_CORE_H
#define TEXTRENDER_CORE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace textrender {

struct RubySpan {
    double x = 0.0;      // relative to base char x
    double y = 0.0;      // relative to base char y
    int size = 10;
    std::u16string text;
};

struct RenderedChar {
    int x = 0;
    int y = 0;
    int cw = 0;
    int size = 24;
    std::u16string text;
    int bold = 0;
    int italic = 0;
    std::u16string face;
    uint32_t color = 0xffffffffu;
    int shadow = 0;
    uint32_t shadowColor = 0xff000000u;
    int shadowDiff = 1;
    int edge = 0;
    uint32_t edgeColor = 0xff000000u;
    bool hasRuby = false;
    std::vector<RubySpan> rubies;
    int graph = 0;
    int vertical = 0;
    std::u16string link;
    int line = 0;
    double delay = 0.0;
};

struct KeyWaitEntry {
    int pos = 0;
    double time = 0.0;
    KeyWaitEntry() = default;
    KeyWaitEntry(int p, double t) : pos(p), time(t) {}
};

struct LinkRect {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    LinkRect() = default;
    LinkRect(int l, int t, int w, int h) : left(l), top(t), width(w), height(h) {}
};

struct LinkInfo {
    std::u16string name;
    std::vector<LinkRect> rects;
};

class Engine {
public:
    // Host callbacks
    std::function<std::u16string(const std::u16string&)> onEval;
    std::function<double(const std::u16string&)> onLabel;
    std::function<void(const std::u16string&, int, int)> onFontChange;
    std::function<double(const std::u16string&, int)> onGetTextWidth;
    std::function<void(const std::u16string&, int&, int&)> onGetGraphSize;

    // Render box
    int renderWidth = 0;
    int renderHeight = 0;

    // Defaults
    int defaultBold = 0;
    int defaultItalic = 0;
    std::u16string defaultFace = u"normal";
    int defaultFontSize = 24;
    int bigFontSize = 48;
    int smallFontSize = 12;
    int defaultRubySize = 10;
    int defaultRubyOffset = -2;
    uint32_t defaultChColor = 0xffffffffu;
    int defaultShadow = 1;
    uint32_t defaultShadowColor = 0xff000000u;
    int defaultShadowDiff = 1;
    int defaultEdge = 0;
    uint32_t defaultEdgeColor = 0xff0080ffu;
    int defaultLineSpacing = 6;
    int defaultPitch = 0;
    int defaultLineSize = 24;
    int defaultAlign = -1;
    int defaultValign = -1;
    bool defaultWordBreak = true;

    // Active font & style
    int vertical = 0;
    int bold = 0;
    int italic = 0;
    std::u16string face = u"normal";
    int fontSize = 24;
    int rubySize = 10;
    int rubyOffset = -2;
    uint32_t chColor = 0xffffffffu;
    int shadow = 1;
    uint32_t shadowColor = 0xff000000u;
    int shadowDiff = 1;
    int edge = 0;
    uint32_t edgeColor = 0xff0080ffu;
    int lineSpacing = 6;
    int pitch = 0;
    int lineSize = 24;
    int align = -1;
    int valign = -1;

    double factor = 1.0;
    double fontScale = 1.0;
    double lineScale = 1.0;

    // Options
    std::u16string optFollowing;
    std::u16string optLeading;
    std::u16string optBegin;
    std::u16string optEnd;
    int kinsokuMax = 1;
    bool wordBreak = true; // true = CJK char wrap, false = Western/Vietnamese word wrap
    double widthTimeScale = 0.0;
    bool ignoreColor = false;
    bool ignoreFace = false;
    bool ignoreSize = false;
    bool ignoreType = false;
    bool ignoreRuby = false;
    bool ignoreDelay = false;
    bool ignoreStyle = false;
    bool ignoreXr = false;
    bool ignoreOverX = false;
    bool ignoreOverY = false;

    // Layout state & results
    std::vector<RenderedChar> chars;
    std::vector<KeyWaitEntry> keyWaits;
    std::vector<LinkInfo> links;
    std::vector<int> lineOffsets;
    std::vector<int> lineBottoms;
    std::u16string renderText;

    int showCount = 0;
    int over = 0;
    double totalDelay = 0.0;
    int indent = 0;
    int lineCount = 0;
    int renderLeft = 0;
    int renderTop = 0;
    int renderRight = 0;
    int renderBottom = 0;

private:
    size_t lineStartIdx = 0;
    int curX = 0;
    int curLineTop = 0;
    int lineIndent = 0;
    bool isLineHead = true;
    bool autoIndentActive = false;
    char16_t autoIndentEndChar = 0;
    std::u16string activeLink;

    // Pending ruby to attach to the next pendingRubySpan base characters
    bool pendingRuby = false;
    std::u16string pendingRubyText;
    int pendingRubySpan = 1;
    size_t pendingRubyStartIdx = 0;
    int pendingRubyCollected = 0;

    double baseSpeed = 0.0;
    double curCharSpeed = 0.0;
    double curDelay = 0.0;
    int fontCallbackDepth = 0;

public:
    Engine() {
        resetFont(false);
        resetStyle();
        clear();
    }

    void setRenderSize(int w, int h) {
        renderWidth = w;
        renderHeight = h;
    }

    void notifyFontChange() {
        if (fontCallbackDepth >= 4 || !onFontChange) return;
        ++fontCallbackDepth;
        onFontChange(face, bold, italic);
        --fontCallbackDepth;
    }

    void resetFont(bool fireCallback = true) {
        face = defaultFace;
        bold = defaultBold;
        italic = defaultItalic;
        fontSize = std::max(1, (int)std::lround(defaultFontSize * fontScale));
        rubySize = std::max(1, (int)std::lround(defaultRubySize * fontScale));
        rubyOffset = (int)std::lround(defaultRubyOffset * fontScale);
        chColor = defaultChColor;
        shadow = defaultShadow;
        shadowColor = defaultShadowColor;
        shadowDiff = defaultShadowDiff;
        edge = defaultEdge;
        edgeColor = defaultEdgeColor;
        if (fireCallback) notifyFontChange();
    }

    void resetStyle() {
        lineSpacing = (int)std::lround(defaultLineSpacing * lineScale);
        pitch = defaultPitch;
        int effBaseLine = (defaultLineSize > 0) ? defaultLineSize : defaultFontSize;
        lineSize = std::max(1, (int)std::lround(effBaseLine * fontScale));
        align = defaultAlign;
        valign = defaultValign;
        wordBreak = defaultWordBreak;
    }

    void clear() {
        chars.clear();
        keyWaits.clear();
        links.clear();
        lineOffsets.clear();
        lineBottoms.clear();
        renderText.clear();
        showCount = 0;
        over = 0;
        totalDelay = 0.0;
        indent = 0;
        lineCount = 0;
        renderLeft = 0;
        renderTop = 0;
        renderRight = 0;
        renderBottom = 0;

        lineStartIdx = 0;
        curX = 0;
        curLineTop = 0;
        lineIndent = 0;
        isLineHead = true;
        autoIndentActive = false;
        autoIndentEndChar = 0;
        activeLink.clear();

        pendingRuby = false;
        pendingRubyText.clear();
        pendingRubySpan = 1;
        pendingRubyStartIdx = 0;
        pendingRubyCollected = 0;

        baseSpeed = 0.0;
        curCharSpeed = 0.0;
        curDelay = 0.0;
        resetFont(false);
        resetStyle();
    }

    int measureText(const std::u16string& s, int pxSize) {
        if (s.empty()) return 0;
        if (onGetTextWidth) {
            double w = onGetTextWidth(s, pxSize);
            if (w > 0.0) return (int)std::ceil(w);
        }
        int total = 0;
        for (char16_t c : s) {
            total += (c < 0x80) ? std::max(1, (pxSize + 1) / 2) : pxSize;
        }
        return total;
    }

    static bool isWhitespace(char16_t c) {
        return c == u' ' || c == u'\t' || c == u'\u3000';
    }

    bool containsChar(const std::u16string& set, char16_t c) const {
        return c != 0 && set.find(c) != std::u16string::npos;
    }

    bool canBreakBefore(char16_t prevCh, char16_t nextCh) const {
        if (containsChar(optFollowing, nextCh)) return false;
        if (containsChar(optLeading, prevCh)) return false;
        if (wordBreak) return true;
        if (isWhitespace(prevCh) || prevCh == u'-' || prevCh == u'/') return true;
        // Allow breaking between CJK characters even when wordBreak == false
        auto isCjk = [](char16_t c) {
            return (c >= 0x3040 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff);
        };
        return isCjk(prevCh) || isCjk(nextCh);
    }

    void finishPendingRubyIfReady(bool forcePartial = false) {
        if (!pendingRuby) return;
        if (!forcePartial && pendingRubyCollected < pendingRubySpan) return;
        if (pendingRubyCollected <= 0 || pendingRubyStartIdx >= chars.size()) {
            pendingRuby = false;
            pendingRubyText.clear();
            pendingRubyCollected = 0;
            return;
        }
        size_t firstIdx = pendingRubyStartIdx;
        size_t lastIdx = std::min(chars.size() - 1, firstIdx + (size_t)pendingRubyCollected - 1);
        int spanLeft = chars[firstIdx].x;
        int spanRight = chars[lastIdx].x + chars[lastIdx].cw;
        int spanWidth = std::max(0, spanRight - spanLeft);
        int rubyPxW = measureText(pendingRubyText, rubySize);

        RubySpan rs;
        rs.size = rubySize;
        rs.text = pendingRubyText;
        if (vertical) {
            rs.x = (double)chars[firstIdx].size + (double)rubyOffset;
            int rubyPxH = (int)pendingRubyText.size() * rubySize;
            rs.y = ((double)spanWidth - (double)rubyPxH) * 0.5;
        } else {
            rs.x = ((double)spanWidth - (double)rubyPxW) * 0.5;
            rs.y = -(double)rubySize - (double)rubyOffset;
        }
        chars[firstIdx].hasRuby = true;
        chars[firstIdx].rubies.push_back(rs);

        pendingRuby = false;
        pendingRubyText.clear();
        pendingRubyCollected = 0;
    }

    void flushLine(bool wrapped, bool forceEmptyLine = false) {
        if (lineStartIdx == chars.size() && !forceEmptyLine) {
            curX = indent;
            lineIndent = indent;
            return;
        }

        int maxSize = std::max(1, fontSize);
        int lineW = 0;
        for (size_t i = lineStartIdx; i < chars.size(); ++i) {
            maxSize = std::max(maxSize, chars[i].size);
            lineW = std::max(lineW, (chars[i].x + chars[i].cw) - lineIndent);
        }
        int actualLineH = (lineSize > 0) ? std::max(lineSize, maxSize) : maxSize;

        int dx = 0;
        int availW = vertical ? renderHeight : renderWidth;
        if (!ignoreStyle && availW > 0 && align >= 0) {
            int rem = availW - lineIndent - lineW;
            if (rem > 0) {
                dx = (align == 0) ? (rem / 2) : rem;
            }
        }
        lineOffsets.push_back(dx);

        int lineIdx = lineCount++;
        for (size_t i = lineStartIdx; i < chars.size(); ++i) {
            RenderedChar& rc = chars[i];
            rc.line = lineIdx;
            if (vertical) {
                int colX = availW > 0 ? (renderWidth - curLineTop - actualLineH) : (-curLineTop - actualLineH);
                int charOffX = 0;
                if (valign == 0) charOffX = (actualLineH - rc.cw) / 2;
                else if (valign > 0) charOffX = actualLineH - rc.cw;
                int finalY = rc.x + dx;
                int finalX = colX + charOffX;
                rc.x = finalX;
                rc.y = finalY;
            } else {
                rc.x += dx;
                int charOffY = 0;
                if (valign == 0) charOffY = (actualLineH - rc.size) / 2;
                else if (valign > 0) charOffY = actualLineH - rc.size;
                rc.y = curLineTop + charOffY;
            }
            if (!rc.link.empty()) {
                addLinkRect(rc.link, rc.x, rc.y, rc.cw, rc.size);
            }
            if (rc.graph == 0) {
                renderText += rc.text;
            }
        }
        renderText += u'\n';

        int lineEndPos = curLineTop + actualLineH;
        lineBottoms.push_back(lineEndPos);
        curLineTop = lineEndPos + lineSpacing;

        int limitH = vertical ? renderWidth : renderHeight;
        if (limitH > 0 && lineEndPos > limitH) {
            over = 1;
        }

        lineStartIdx = chars.size();
        curX = indent;
        lineIndent = indent;
        if (!wrapped) isLineHead = true;
    }

    void updateBounds() {
        if (chars.empty()) {
            renderLeft = renderTop = renderRight = renderBottom = 0;
            return;
        }
        int minX = chars[0].x, minY = chars[0].y;
        int maxX = chars[0].x + chars[0].cw, maxY = chars[0].y + chars[0].size;
        for (size_t i = 1; i < chars.size(); ++i) {
            minX = std::min(minX, chars[i].x);
            minY = std::min(minY, chars[i].y);
            maxX = std::max(maxX, chars[i].x + chars[i].cw);
            maxY = std::max(maxY, chars[i].y + chars[i].size);
        }
        renderLeft = minX;
        renderTop = minY;
        renderRight = maxX;
        renderBottom = maxY;
    }

    void addLinkRect(const std::u16string& name, int x, int y, int w, int h) {
        for (LinkInfo& li : links) {
            if (li.name == name) {
                if (!li.rects.empty()) {
                    LinkRect& last = li.rects.back();
                    if (last.top == y && last.height == h && last.left + last.width == x) {
                        last.width += w;
                        return;
                    }
                }
                li.rects.push_back(LinkRect{x, y, w, h});
                return;
            }
        }
        LinkInfo info;
        info.name = name;
        info.rects.push_back(LinkRect{x, y, w, h});
        links.push_back(info);
    }

    void emitItem(const std::u16string& str, int itemW, int itemH, bool isGraph, int autoIndentMode) {
        const int maxLineExtent = vertical ? renderHeight : renderWidth;
        char16_t ch = (!isGraph && !str.empty()) ? str[0] : 0;

        // Check overflow & wrap before placing this item
        if (maxLineExtent > 0 && curX > lineIndent && curX + itemW > maxLineExtent) {
            bool allowKinsokuHang = (!isGraph && containsChar(optFollowing, ch) &&
                                     curX + itemW <= maxLineExtent + kinsokuMax * fontSize);
            if (!allowKinsokuHang) {
                if (!isGraph && isWhitespace(ch)) {
                    // Swallow wrapping space and advance to next line
                    flushLine(true, false);
                    return;
                }

                // Find latest legal break index on current line
                size_t breakIdx = chars.size();
                if (!chars.empty() && lineStartIdx < chars.size()) {
                    char16_t prevCh = (chars.back().graph == 0 && !chars.back().text.empty()) ? chars.back().text[0] : 0;
                    if (!canBreakBefore(prevCh, ch)) {
                        bool found = false;
                        for (size_t k = chars.size() - 1; k > lineStartIdx; --k) {
                            char16_t p = (chars[k - 1].graph == 0 && !chars[k - 1].text.empty()) ? chars[k - 1].text[0] : 0;
                            char16_t n = (chars[k].graph == 0 && !chars[k].text.empty()) ? chars[k].text[0] : 0;
                            if (canBreakBefore(p, n)) {
                                breakIdx = k;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            // Single unbreakable word wider than entire line: force break before current char
                            breakIdx = chars.size();
                        }
                    }
                }

                if (breakIdx < chars.size()) {
                    std::vector<RenderedChar> moved(chars.begin() + breakIdx, chars.end());
                    chars.erase(chars.begin() + breakIdx, chars.end());
                    while (chars.size() > lineStartIdx && chars.back().graph == 0 &&
                           !chars.back().text.empty() && isWhitespace(chars.back().text[0])) {
                        chars.pop_back();
                    }
                    flushLine(true, false);
                    size_t startNew = chars.size();
                    for (size_t m = 0; m < moved.size(); ++m) {
                        if (m == 0 && moved[m].graph == 0 && !moved[m].text.empty() && isWhitespace(moved[m].text[0])) {
                            continue;
                        }
                        moved[m].x = curX;
                        curX += moved[m].cw + pitch;
                        chars.push_back(moved[m]);
                    }
                    if (pendingRuby && pendingRubyStartIdx >= breakIdx) {
                        pendingRubyStartIdx = startNew + (pendingRubyStartIdx - breakIdx);
                    }
                } else {
                    while (chars.size() > lineStartIdx && chars.back().graph == 0 &&
                           !chars.back().text.empty() && isWhitespace(chars.back().text[0])) {
                        chars.pop_back();
                    }
                    flushLine(true, false);
                }
            }
        }

        // Hanging outdent (%@ -> autoIndentMode < 0) or positive auto-indent (> 0)
        bool activateAutoIndentAfter = false;
        if (!isGraph && isLineHead && curX == 0 && indent == 0) {
            size_t pos = optBegin.find(ch);
            if (pos != std::u16string::npos) {
                if (autoIndentMode < 0) {
                    curX = -itemW;
                } else if (autoIndentMode > 0) {
                    activateAutoIndentAfter = true;
                    autoIndentEndChar = (pos < optEnd.size()) ? optEnd[pos] : 0;
                }
            }
        }

        RenderedChar rc;
        rc.x = curX;
        rc.y = curLineTop;
        rc.cw = itemW;
        rc.size = itemH;
        rc.text = str;
        rc.bold = bold;
        rc.italic = italic;
        rc.face = face;
        rc.color = chColor;
        rc.shadow = shadow;
        rc.shadowColor = shadowColor;
        rc.shadowDiff = shadowDiff;
        rc.edge = edge;
        rc.edgeColor = edgeColor;
        rc.graph = isGraph ? 1 : 0;
        rc.vertical = vertical;
        rc.link = activeLink;
        rc.delay = ignoreDelay ? 0.0 : curDelay;

        if (!ignoreDelay && curCharSpeed > 0.0) {
            double step = curCharSpeed;
            if (widthTimeScale > 0.0 && fontSize > 0) {
                step *= ((double)itemW / (double)fontSize) * widthTimeScale;
            }
            curDelay += step;
        }
        if (!ignoreDelay && curDelay > totalDelay) {
            totalDelay = curDelay;
        }

        size_t placedIdx = chars.size();
        chars.push_back(rc);
        curX += itemW + pitch;
        isLineHead = false;

        if (activateAutoIndentAfter) {
            autoIndentActive = true;
            indent = curX;
        } else if (!isGraph && autoIndentActive && autoIndentEndChar != 0 && ch == autoIndentEndChar) {
            autoIndentActive = false;
            autoIndentEndChar = 0;
            indent = 0;
        }

        if (pendingRuby && !isGraph) {
            if (pendingRubyCollected == 0) pendingRubyStartIdx = placedIdx;
            ++pendingRubyCollected;
            finishPendingRubyIfReady(false);
        }
    }

    static bool parseHexColor(const std::u16string& s, uint32_t& outColor) {
        if (s.empty()) return false;
        size_t i = (s[0] == u'#') ? 1 : 0;
        if (i >= s.size()) return false;
        uint32_t val = 0;
        int digits = 0;
        for (; i < s.size(); ++i) {
            char16_t c = s[i];
            uint32_t d = 0;
            if (c >= u'0' && c <= u'9') d = c - u'0';
            else if (c >= u'a' && c <= u'f') d = 10 + (c - u'a');
            else if (c >= u'A' && c <= u'F') d = 10 + (c - u'A');
            else return false;
            val = (val << 4) | d;
            ++digits;
        }
        if (digits <= 6) val |= 0xff000000u;
        outColor = val;
        return true;
    }

    static int parseSignedInt(const std::u16string& s, int defVal = 0) {
        if (s.empty()) return defVal;
        bool neg = false;
        size_t i = 0;
        if (s[0] == u'-') { neg = true; i = 1; }
        else if (s[0] == u'+') { i = 1; }
        if (i >= s.size()) return defVal;
        long long v = 0;
        for (; i < s.size(); ++i) {
            if (s[i] < u'0' || s[i] > u'9') break;
            v = v * 10 + (s[i] - u'0');
        }
        return (int)(neg ? -v : v);
    }

    double resolveTimeOrLabel(const std::u16string& token, bool scaledBySpeed) {
        if (token.empty()) return 0.0;
        if (token[0] == u'$') {
            return onLabel ? onLabel(token.substr(1)) : 0.0;
        }
        int v = parseSignedInt(token, 0);
        return scaledBySpeed ? ((v / 100.0) * baseSpeed) : (double)v;
    }

    // Matches textrender.dll FUN_1000b8c0:
    // Does NOT wipe existing page lines; clear() is called explicitly by callers on page clear.
    // When same == 0, zeros delay on previously rendered characters and resets keyWaits/totalDelay.
    int render(const std::u16string& text, int autoIndent = 0, double chSpeed = 0.0,
               int waitMode = 0, int same = 0, int plain = 0) {
        double effSpeed = (waitMode > 0 && chSpeed == 0.0) ? 0.001 : chSpeed;
        if (same == 0) {
            for (RenderedChar& rc : chars) {
                rc.delay = 0.0;
            }
            keyWaits.clear();
            totalDelay = 0.0;
        }
        notifyFontChange();
        baseSpeed = effSpeed;
        curCharSpeed = effSpeed;
        curDelay = 0.0;

        renderInternal(text, autoIndent, plain != 0, 0);
        if (!ignoreDelay && curDelay > totalDelay) {
            totalDelay = curDelay;
        }
        showCount = (int)chars.size();
        return over;
    }

    void newline() {
        finishPendingRubyIfReady(true);
        flushLine(false, true);
    }

    void done() {
        finishPendingRubyIfReady(true);
        if (lineStartIdx < chars.size()) {
            flushLine(false, false);
        }
        if (autoIndentActive) {
            autoIndentActive = false;
            autoIndentEndChar = 0;
            indent = 0;
            curX = 0;
            lineIndent = 0;
        }
        showCount = (int)chars.size();
        updateBounds();
    }

    int calcShowCount(double elapsedMs) const {
        if (ignoreDelay || totalDelay <= 0.0) return (int)chars.size();
        for (size_t i = chars.size(); i > 0; --i) {
            if (chars[i - 1].delay * factor <= elapsedMs) {
                return (int)i;
            }
        }
        return 0;
    }

    int calcLineOffset(int line) const {
        if (line < 0 || (size_t)line >= lineOffsets.size()) return 0;
        return lineOffsets[(size_t)line];
    }

    bool contains(int x, int y) const {
        if (chars.empty()) return false;
        return x >= renderLeft && x < renderRight && y >= renderTop && y < renderBottom;
    }

    int maxScrollOffset() const {
        int limitH = vertical ? renderWidth : renderHeight;
        if (limitH <= 0 || lineBottoms.empty()) return 0;
        return std::max(0, lineBottoms.back() - limitH);
    }

    int maxScrollLine() const {
        int limitH = vertical ? renderWidth : renderHeight;
        if (limitH <= 0 || lineBottoms.empty()) return 0;
        int overLines = 0;
        for (int b : lineBottoms) {
            if (b > limitH) ++overLines;
        }
        return overLines;
    }

    int findLink(const std::u16string& name) const {
        for (size_t i = 0; i < links.size(); ++i) {
            if (links[i].name == name) return (int)i;
        }
        return -1;
    }

    bool isLinkContains(int idx, int x, int y) const {
        if (idx < 0 || (size_t)idx >= links.size()) return false;
        for (const LinkRect& r : links[(size_t)idx].rects) {
            if (x >= r.left && x < r.left + r.width && y >= r.top && y < r.top + r.height) {
                return true;
            }
        }
        return false;
    }

    std::u16string getLinkOfPosition(int x, int y) const {
        for (size_t i = 0; i < links.size(); ++i) {
            if (isLinkContains((int)i, x, y)) return links[i].name;
        }
        return std::u16string();
    }

private:
    void renderInternal(const std::u16string& text, int autoIndent, bool plain, int evalDepth) {
        const size_t n = text.size();
        size_t i = 0;
        while (i < n) {
            char16_t c = text[i];
            if (c == u'\r') {
                ++i;
                if (i < n && text[i] == u'\n') ++i;
                if (!ignoreXr) newline();
                continue;
            }
            if (c == u'\n') {
                ++i;
                if (!ignoreXr) newline();
                continue;
            }
            if (plain) {
                std::u16string s(1, c);
                emitItem(s, measureText(s, fontSize), fontSize, false, autoIndent);
                ++i;
                continue;
            }

            // Escape sequences: \n, \t, \i, \r, \w, \k, \x, or literal \<char>
            if (c == u'\\') {
                ++i;
                if (i >= n) break;
                char16_t esc = text[i++];
                switch (esc) {
                case u'n':
                    if (!ignoreXr) newline();
                    break;
                case u't': {
                    std::u16string sp(1, u' ');
                    emitItem(sp, measureText(sp, fontSize) * 4, fontSize, false, autoIndent);
                    break;
                }
                case u'w': {
                    std::u16string fw(1, u'\u3000');
                    curX += measureText(fw, fontSize) + pitch;
                    isLineHead = false;
                    break;
                }
                case u'i':
                    indent = curX;
                    break;
                case u'r':
                    autoIndentActive = false;
                    autoIndentEndChar = 0;
                    indent = 0;
                    break;
                case u'k':
                    if (!ignoreDelay) {
                        keyWaits.push_back(KeyWaitEntry{(int)chars.size(), curDelay});
                    }
                    isLineHead = false;
                    break;
                case u'x':
                    isLineHead = false;
                    break;
                default: {
                    std::u16string lit(1, esc);
                    emitItem(lit, measureText(lit, fontSize), fontSize, false, autoIndent);
                    break;
                }
                }
                continue;
            }

            // Inline graphic: &name;
            if (c == u'&') {
                size_t semi = text.find(u';', i + 1);
                if (semi != std::u16string::npos) {
                    std::u16string gname = text.substr(i + 1, semi - (i + 1));
                    int gw = fontSize, gh = fontSize;
                    if (onGetGraphSize) onGetGraphSize(gname, gw, gh);
                    if (gw > 0 && gh > 0) {
                        emitItem(gname, gw, gh, true, autoIndent);
                    }
                    i = semi + 1;
                    continue;
                }
            }

            // Inline TJS eval: $expr;
            if (c == u'$') {
                size_t semi = text.find(u';', i + 1);
                if (semi != std::u16string::npos) {
                    std::u16string expr = text.substr(i + 1, semi - (i + 1));
                    i = semi + 1;
                    if (onEval && evalDepth < 8) {
                        std::u16string val = onEval(expr);
                        renderInternal(val, autoIndent, false, evalDepth + 1);
                    }
                    continue;
                }
            }

            // Color tag: #RRGGBB; or #;
            if (c == u'#') {
                size_t semi = text.find(u';', i + 1);
                if (semi != std::u16string::npos && semi - (i + 1) <= 8) {
                    std::u16string hex = text.substr(i + 1, semi - (i + 1));
                    uint32_t parsed = defaultChColor;
                    if (hex.empty() || parseHexColor(hex, parsed)) {
                        if (!ignoreColor) chColor = hex.empty() ? defaultChColor : parsed;
                        i = semi + 1;
                        continue;
                    }
                }
            }

            // Ruby tag: [ruby] or [ruby,N] ([[ -> literal [)
            if (c == u'[') {
                if (i + 1 < n && text[i + 1] == u'[') {
                    std::u16string lit(1, u'[');
                    emitItem(lit, measureText(lit, fontSize), fontSize, false, autoIndent);
                    i += 2;
                    continue;
                }
                size_t close = text.find(u']', i + 1);
                if (close != std::u16string::npos) {
                    finishPendingRubyIfReady(true);
                    std::u16string inside = text.substr(i + 1, close - (i + 1));
                    int span = 1;
                    size_t comma = inside.rfind(u',');
                    if (comma != std::u16string::npos && comma + 1 < inside.size()) {
                        bool allDigits = true;
                        for (size_t k = comma + 1; k < inside.size(); ++k) {
                            if (inside[k] < u'0' || inside[k] > u'9') { allDigits = false; break; }
                        }
                        if (allDigits) {
                            int parsedSpan = parseSignedInt(inside.substr(comma + 1), 1);
                            if (parsedSpan > 0) span = parsedSpan;
                            inside = inside.substr(0, comma);
                        }
                    }
                    if (!ignoreRuby && !inside.empty()) {
                        pendingRuby = true;
                        pendingRubyText = inside;
                        pendingRubySpan = span;
                        pendingRubyCollected = 0;
                    }
                    i = close + 1;
                    continue;
                }
            }

            // Control/style tags: %...
            if (c == u'%') {
                ++i;
                if (i >= n) break;
                char16_t cmd = text[i];
                if (cmd == u'%') {
                    std::u16string lit(1, u'%');
                    emitItem(lit, measureText(lit, fontSize), fontSize, false, autoIndent);
                    ++i;
                    continue;
                }
                if (cmd == u';') {
                    if (!ignoreSize) fontSize = std::max(1, (int)std::lround(defaultFontSize * fontScale));
                    ++i;
                    continue;
                }
                if (cmd >= u'0' && cmd <= u'9') {
                    size_t start = i;
                    while (i < n && text[i] >= u'0' && text[i] <= u'9') ++i;
                    int pct = parseSignedInt(text.substr(start, i - start), 100);
                    if (i < n && text[i] == u';') ++i;
                    if (!ignoreSize) {
                        double scale = (pct > 0) ? (pct / 100.0) : 1.0;
                        fontSize = std::max(1, (int)std::lround(defaultFontSize * fontScale * scale));
                    }
                    continue;
                }

                ++i; // advance past cmd
                switch (cmd) {
                case u'r':
                    resetFont(true);
                    resetStyle();
                    break;
                case u'B':
                    if (!ignoreSize) fontSize = std::max(1, (int)std::lround(bigFontSize * fontScale));
                    break;
                case u'S':
                    if (!ignoreSize) fontSize = std::max(1, (int)std::lround(smallFontSize * fontScale));
                    break;
                case u'L':
                    if (!ignoreStyle) align = -1;
                    break;
                case u'C':
                    if (!ignoreStyle) align = 0;
                    break;
                case u'R':
                    if (!ignoreStyle) align = 1;
                    break;
                case u'b': {
                    char16_t arg = (i < n) ? text[i++] : 0;
                    if (!ignoreType) {
                        bold = (arg == u'1') ? 1 : (arg == u'0') ? 0 : defaultBold;
                        notifyFontChange();
                    }
                    break;
                }
                case u'i': {
                    char16_t arg = (i < n) ? text[i++] : 0;
                    if (!ignoreType) {
                        italic = (arg == u'1') ? 1 : (arg == u'0') ? 0 : defaultItalic;
                        notifyFontChange();
                    }
                    break;
                }
                case u'k': {
                    char16_t arg = (i < n) ? text[i++] : 0;
                    wordBreak = (arg == u'1') ? true : (arg == u'0') ? false : defaultWordBreak;
                    break;
                }
                case u's': {
                    if (i < n && text[i] == u'#') {
                        size_t semi = text.find(u';', i + 1);
                        if (semi != std::u16string::npos) {
                            std::u16string hex = text.substr(i + 1, semi - (i + 1));
                            uint32_t col = defaultShadowColor;
                            if (!ignoreColor) {
                                shadowColor = (hex.empty() || !parseHexColor(hex, col)) ? defaultShadowColor : col;
                            }
                            i = semi + 1;
                        }
                    } else if (i < n) {
                        char16_t arg = text[i++];
                        if (!ignoreType) shadow = (arg == u'1') ? 1 : (arg == u'0') ? 0 : defaultShadow;
                    }
                    break;
                }
                case u'e': {
                    if (i < n && text[i] == u'#') {
                        size_t semi = text.find(u';', i + 1);
                        if (semi != std::u16string::npos) {
                            std::u16string hex = text.substr(i + 1, semi - (i + 1));
                            uint32_t col = defaultEdgeColor;
                            if (!ignoreColor) {
                                edgeColor = (hex.empty() || !parseHexColor(hex, col)) ? defaultEdgeColor : col;
                            }
                            i = semi + 1;
                        }
                    } else if (i < n) {
                        char16_t arg = text[i++];
                        if (!ignoreType) edge = (arg == u'1') ? 1 : (arg == u'0') ? 0 : defaultEdge;
                    }
                    break;
                }
                case u'f': {
                    size_t semi = text.find(u';', i);
                    if (semi != std::u16string::npos) {
                        std::u16string f = text.substr(i, semi - i);
                        i = semi + 1;
                        if (!ignoreFace) {
                            face = f.empty() ? defaultFace : f;
                            notifyFontChange();
                        }
                    }
                    break;
                }
                case u'l': {
                    size_t semi = text.find(u';', i);
                    if (semi != std::u16string::npos) {
                        activeLink = text.substr(i, semi - i);
                        i = semi + 1;
                    }
                    break;
                }
                case u'p': {
                    size_t start = i;
                    if (i < n && (text[i] == u'-' || text[i] == u'+')) ++i;
                    while (i < n && text[i] >= u'0' && text[i] <= u'9') ++i;
                    std::u16string tok = text.substr(start, i - start);
                    if (i < n && text[i] == u';') ++i;
                    if (!ignoreStyle) pitch = tok.empty() ? defaultPitch : parseSignedInt(tok, defaultPitch);
                    break;
                }
                case u'n': {
                    size_t start = i;
                    while (i < n && text[i] >= u'0' && text[i] <= u'9') ++i;
                    int cnt = std::max(1, parseSignedInt(text.substr(start, i - start), 1));
                    if (i < n && text[i] == u';') ++i;
                    if (!ignoreXr) {
                        for (int k = 0; k < cnt; ++k) newline();
                    }
                    break;
                }
                case u'd': {
                    size_t start = i;
                    while (i < n && text[i] >= u'0' && text[i] <= u'9') ++i;
                    std::u16string tok = text.substr(start, i - start);
                    if (i < n && text[i] == u';') ++i;
                    if (!ignoreDelay) {
                        curCharSpeed = tok.empty() ? baseSpeed : ((parseSignedInt(tok, 100) / 100.0) * baseSpeed);
                    }
                    break;
                }
                case u'a': {
                    size_t start = i;
                    while (i < n && text[i] >= u'0' && text[i] <= u'9') ++i;
                    std::u16string tok = text.substr(start, i - start);
                    if (i < n && text[i] == u';') ++i;
                    if (!ignoreDelay) {
                        curCharSpeed = tok.empty() ? baseSpeed : (double)parseSignedInt(tok, 0);
                    }
                    break;
                }
                case u'w':
                case u't':
                case u'D': {
                    size_t semi = text.find(u';', i);
                    std::u16string tok;
                    if (semi != std::u16string::npos && semi - i <= 32) {
                        tok = text.substr(i, semi - i);
                        i = semi + 1;
                    } else {
                        size_t start = i;
                        while (i < n && text[i] >= u'0' && text[i] <= u'9') ++i;
                        tok = text.substr(start, i - start);
                    }
                    if (!ignoreDelay) {
                        curDelay += resolveTimeOrLabel(tok, cmd == u'w');
                        if (curDelay > totalDelay) totalDelay = curDelay;
                        if (cmd == u'D') {
                            keyWaits.push_back(KeyWaitEntry{(int)chars.size(), curDelay});
                        }
                    }
                    break;
                }
                default: {
                    // Unknown %tag; -> skip up to ';' if present (matching textrender.dll line 660)
                    size_t semi = text.find(u';', i);
                    if (semi != std::u16string::npos && semi - i <= 32) {
                        i = semi + 1;
                    }
                    break;
                }
                }
                continue;
            }

            // Regular character
            std::u16string s(1, c);
            emitItem(s, measureText(s, fontSize), fontSize, false, autoIndent);
            ++i;
        }
    }
};

} // namespace textrender

#endif // TEXTRENDER_CORE_H
