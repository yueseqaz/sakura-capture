#include "common.h"
#include <cwchar>

namespace {
enum class Tool { Select, Rectangle, Arrow, Pen, Mosaic, Text, Ellipse, Number, Line, Highlight };
struct Mark {
    Tool tool;
    std::vector<POINT> points;
    std::wstring text;
    COLORREF color;
    int width;
};
struct EditState {
    std::vector<Mark> marks;
    RECT selection;
};
struct Button {
    std::wstring label;
    RECT rect;
    int action;
};
struct Editor {
    std::wstring path, mode;
    bool selectOnly = false, selected = false, dragging = false, saved = false, failed = false,
         clipboardFailed = false, copied = false;
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN),
        w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HWND window = nullptr;
    Bitmap shot{w, h};
    Bitmap preview{w, h};
    POINT anchor{};
    RECT selection{};
    Tool tool = Tool::Rectangle;
    int thickness = 3, colorIndex = 0;
    COLORREF colors[16] = {RGB(236, 74, 92),   RGB(36, 124, 232),  RGB(44, 168, 100),
                           RGB(245, 175, 36),  RGB(255, 255, 255), RGB(24, 29, 34),
                           RGB(117, 87, 210),  RGB(22, 174, 184),  RGB(255, 134, 63),
                           RGB(230, 124, 176), RGB(166, 177, 190), RGB(121, 87, 62)};
    int toolWidths[10] = {3, 3, 3, 3, 14, 28, 3, 3, 3, 20};
    COLORREF toolColors[10] = {};
    std::wstring preferences;
    std::vector<COLORREF> recentColors;
    bool paletteOpen = false, picking = false, sliderDragging = false, hexEditing = false;
    int channelDragging = -1, transform = 0, handle = -1;
    bool gestureChanged = false, hexReplace = false;
    Mark originalMark{};
    RECT originalSelection{};
    POINT dragOrigin{};
    std::wstring hexInput;
    RECT widthTrack{}, uiVisible{};
    double uiScale = 1.0;
    std::wstring notice;
    std::vector<Mark> marks;
    std::vector<EditState> undo, redo;
    int activeMark = -1;
    bool fakeCapture = false;
    std::vector<Button> buttons;
    Mark current{};
    HFONT font = nullptr;
    HWND textInput = nullptr;
    HFONT inputFont = nullptr;
    WNDPROC textDefault = nullptr;
    POINT pointer{}, lastHover{};
    bool textEditing = false;
    Editor(std::wstring p, std::wstring m, bool only, bool fake = false)
        : path(p), mode(m), selectOnly(only), x(fake ? 0 : GetSystemMetrics(SM_XVIRTUALSCREEN)),
          y(fake ? 0 : GetSystemMetrics(SM_YVIRTUALSCREEN)),
          w(fake ? 1120 : GetSystemMetrics(SM_CXVIRTUALSCREEN)),
          h(fake ? 720 : GetSystemMetrics(SM_CYVIRTUALSCREEN)) {
        fakeCapture = fake;
        for (auto& c : toolColors)
            c = colors[0];
        if (!fake) {
            wchar_t root[32768];
            DWORD length = GetEnvironmentVariableW(L"APPDATA", root, 32768);
            if (length && length < 32768) {
                std::wstring folder = std::wstring(root) + L"\\SakuraCapture";
                CreateDirectoryW(folder.c_str(), nullptr);
                preferences = folder + L"\\editor.ini";
                for (int i = 0; i < 10; i++) {
                    auto key = std::to_wstring(i);
                    toolWidths[i] =
                        std::clamp<int>(GetPrivateProfileIntW(L"width", key.c_str(), toolWidths[i],
                                                              preferences.c_str()),
                                        i == 5 ? 8 : 1, 64);
                    toolColors[i] = GetPrivateProfileIntW(L"color", key.c_str(), colors[0],
                                                          preferences.c_str()) &
                                    0xffffff;
                }
                for (int i = 0; i < 6; i++) {
                    UINT color = GetPrivateProfileIntW(L"recent", std::to_wstring(i).c_str(),
                                                       0xffffffff, preferences.c_str());
                    if (color <= 0xffffff)
                        recentColors.push_back(color);
                }
                thickness = toolWidths[1];
                colors[15] = toolColors[1];
                colorIndex = 15;
            }
        }
        if (fake) {
            for (int row = 0; row < h; row++)
                for (int col = 0; col < w; col++) {
                    BYTE* at = shot.pixels + ((size_t)(h - 1 - row) * w + col) * 4;
                    at[0] = (BYTE)col;
                    at[1] = (BYTE)row;
                    at[2] = 120;
                    at[3] = 255;
                }
        } else
            shot.capture(x, y);
        font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Microsoft YaHei UI");
    }
    ~Editor() {
        if (!preferences.empty()) {
            for (int i = 0; i < 10; i++) {
                auto key = std::to_wstring(i), width = std::to_wstring(toolWidths[i]),
                     color = std::to_wstring(toolColors[i]);
                WritePrivateProfileStringW(L"width", key.c_str(), width.c_str(),
                                           preferences.c_str());
                WritePrivateProfileStringW(L"color", key.c_str(), color.c_str(),
                                           preferences.c_str());
            }
            for (int i = 0; i < (int)recentColors.size(); i++)
                WritePrivateProfileStringW(L"recent", std::to_wstring(i).c_str(),
                                           std::to_wstring(recentColors[i]).c_str(),
                                           preferences.c_str());
        }
        if (inputFont)
            DeleteObject(inputFont);
        if (font)
            DeleteObject(font);
    }
    POINT bound(POINT p) {
        p.x = std::clamp<LONG>(p.x, 0, w);
        p.y = std::clamp<LONG>(p.y, 0, h);
        return p;
    }
    POINT uiPoint(POINT p) {
        return {(LONG)std::lround(p.x / uiScale), (LONG)std::lround(p.y / uiScale)};
    }
    RECT toolbar() {
        RECT visible{0, 0, w, h};
        if (!fakeCapture) {
            POINT center{(selection.left + selection.right) / 2 + x,
                         (selection.top + selection.bottom) / 2 + y};
            if (!selected) {
                POINT cursor{};
                if (GetCursorPos(&cursor))
                    center = cursor;
            }
            HMONITOR monitor = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{sizeof(info)};
            if (GetMonitorInfoW(monitor, &info)) {
                visible = info.rcMonitor;
                OffsetRect(&visible, -x, -y);
                using GetMonitorDpi = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
                static HMODULE shcore = LoadLibraryW(L"shcore.dll");
                static auto getDpi =
                    shcore ? (GetMonitorDpi)GetProcAddress(shcore, "GetDpiForMonitor") : nullptr;
                UINT dx = 96, dy = 96;
                if (getDpi && SUCCEEDED(getDpi(monitor, 0, &dx, &dy)))
                    uiScale = dx / 96.0;
                else
                    uiScale = GetDpiForWindow(window) / 96.0;
                if (uiScale < 1)
                    uiScale = 1;
            }
        }
        visible = {(LONG)std::lround(visible.left / uiScale),
                   (LONG)std::lround(visible.top / uiScale),
                   (LONG)std::lround(visible.right / uiScale),
                   (LONG)std::lround(visible.bottom / uiScale)};
        uiVisible = visible;
        RECT crop{(LONG)std::lround(selection.left / uiScale),
                  (LONG)std::lround(selection.top / uiScale),
                  (LONG)std::lround(selection.right / uiScale),
                  (LONG)std::lround(selection.bottom / uiScale)};
        int width = std::max(100, std::min((int)(visible.right - visible.left) - 12, 864));
        int left = std::clamp<int>(crop.left, visible.left + 6,
                                   std::max<LONG>(visible.left + 6, visible.right - width - 6));
        int top = crop.bottom + 10;
        if (top + 124 > visible.bottom)
            top = crop.top - 132;
        top =
            std::clamp<int>(top, visible.top + 6, std::max(visible.top + 6, visible.bottom - 128));
        return {left, top, left + width, top + 122};
    }
    static BOOL CALLBACK findWindow(HWND win, LPARAM data) {
        auto pair = (std::pair<Editor*, POINT>*)data;
        auto e = pair->first;
        if (win == e->window || !IsWindowVisible(win) || IsIconic(win))
            return TRUE;
        LONG ex = GetWindowLongW(win, GWL_EXSTYLE);
        if (ex & WS_EX_TOOLWINDOW)
            return TRUE;
        RECT r{};
        if (FAILED(DwmGetWindowAttribute(win, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
            GetWindowRect(win, &r);
        if (PtInRect(&r, pair->second)) {
            r.left -= e->x;
            r.right -= e->x;
            r.top -= e->y;
            r.bottom -= e->y;
            e->selection = {std::clamp<LONG>(r.left, 0, e->w), std::clamp<LONG>(r.top, 0, e->h),
                            std::clamp<LONG>(r.right, 0, e->w),
                            std::clamp<LONG>(r.bottom, 0, e->h)};
            return FALSE;
        }
        return TRUE;
    }
    void hover(POINT p) {
        if (mode == L"screen") {
            POINT screen{p.x + x, p.y + y};
            MONITORINFO mi{sizeof(mi)};
            check(GetMonitorInfoW(MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST), &mi)
                      ? S_OK
                      : HRESULT_FROM_WIN32(GetLastError()),
                  "Find selected monitor");
            selection = {mi.rcMonitor.left - x, mi.rcMonitor.top - y, mi.rcMonitor.right - x,
                         mi.rcMonitor.bottom - y};
        } else if (mode == L"window") {
            selection = {};
            std::pair<Editor*, POINT> data{this, {p.x + x, p.y + y}};
            EnumWindows(findWindow, (LPARAM)&data);
        }
    }
    void renderSelection(Bitmap& result) {
        BitBlt(result.dc, 0, 0, result.w, result.h, shot.dc, selection.left, selection.top,
               SRCCOPY);
        SetViewportOrgEx(result.dc, -selection.left, -selection.top, nullptr);
        for (const auto& mark : marks)
            drawMark(result, mark, selection.left, selection.top);
        SetViewportOrgEx(result.dc, 0, 0, nullptr);
        GdiFlush();
    }
    void finish(bool copyOnly = false) {
        int width = selection.right - selection.left, height = selection.bottom - selection.top;
        if (width < 4 || height < 4)
            return;
        if (selectOnly) {
            selected = true;
            DestroyWindow(window);
            return;
        }
        Bitmap result(width, height);
        renderSelection(result);
        if (!copyOnly) {
            savePNG(result, path);
            saved = true;
        }
        clipboardFailed = false;
        // Clipboard owns a CF_DIB allocation after SetClipboardData succeeds.
        bool opened = false;
        for (int i = 0; i < 6 && !opened; i++) {
            opened = OpenClipboard(window) != FALSE;
            if (!opened)
                Sleep(20);
        }
        if (opened) {
            SIZE_T size = sizeof(BITMAPINFOHEADER) + (SIZE_T)width * height * 4;
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
            if (memory) {
                BYTE* target = (BYTE*)GlobalLock(memory);
                if (target) {
                    BITMAPINFOHEADER info{};
                    info.biSize = sizeof(info);
                    info.biWidth = width;
                    info.biHeight = height;
                    info.biPlanes = 1;
                    info.biBitCount = 32;
                    info.biCompression = BI_RGB;
                    memcpy(target, &info, sizeof(info));
                    memcpy(target + sizeof(info), result.pixels, (size_t)width * height * 4);
                    GlobalUnlock(memory);
                    if (!EmptyClipboard() || !SetClipboardData(CF_DIB, memory)) {
                        GlobalFree(memory);
                        clipboardFailed = true;
                    }
                } else {
                    GlobalFree(memory);
                    clipboardFailed = true;
                }
            } else
                clipboardFailed = true;
            CloseClipboard();
        } else
            clipboardFailed = true;
        copied = copyOnly && !clipboardFailed;
        if (copyOnly && clipboardFailed) {
            notice = L"剪贴板被占用，请重试复制或选择保存。";
            return;
        }
        DestroyWindow(window);
    }
    void drawMark(Bitmap& surface, const Mark& mark, int offsetX = 0, int offsetY = 0) {
        if (mark.points.empty())
            return;
        HDC dc = surface.dc;
        POINT a = mark.points.front(), b = mark.points.back();
        HPEN pen = CreatePen(PS_SOLID, mark.width, mark.color);
        HGDIOBJ oldPen = SelectObject(dc, pen),
                oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, mark.color);
        if (mark.tool == Tool::Ellipse) {
            RECT r = normalized(a, b);
            Ellipse(dc, r.left, r.top, r.right, r.bottom);
        } else if (mark.tool == Tool::Number) {
            int radius = 13 + mark.width;
            HBRUSH fill = CreateSolidBrush(mark.color);
            SelectObject(dc, fill);
            Ellipse(dc, a.x - radius, a.y - radius, a.x + radius, a.y + radius);
            SelectObject(dc, oldBrush);
            DeleteObject(fill);
            HGDIOBJ oldFont = SelectObject(dc, font);
            SetTextColor(dc,
                         mark.color == RGB(255, 255, 255) ? RGB(32, 38, 43) : RGB(255, 255, 255));
            RECT r{a.x - radius, a.y - radius, a.x + radius, a.y + radius};
            DrawTextW(dc, mark.text.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, oldFont);
        } else if (mark.tool == Tool::Rectangle) {
            RECT r = normalized(a, b);
            Rectangle(dc, r.left, r.top, r.right, r.bottom);
        } else if (mark.tool == Tool::Arrow || mark.tool == Tool::Line) {
            MoveToEx(dc, a.x, a.y, nullptr);
            LineTo(dc, b.x, b.y);
            double angle = atan2((double)b.y - a.y, (double)b.x - a.x);
            double length = 14 + mark.width * 2;
            if (mark.tool == Tool::Arrow)
                for (double side : {-0.5, 0.5}) {
                    MoveToEx(dc, b.x, b.y, nullptr);
                    LineTo(dc, (int)(b.x - length * cos(angle + side)),
                           (int)(b.y - length * sin(angle + side)));
                }
        } else if (mark.tool == Tool::Highlight) {
            RECT r = normalized(a, b);
            if (r.bottom - r.top < mark.width)
                r.bottom = r.top + mark.width;
            Bitmap tint(1, 1);
            tint.pixels[0] = GetBValue(mark.color);
            tint.pixels[1] = GetGValue(mark.color);
            tint.pixels[2] = GetRValue(mark.color);
            BLENDFUNCTION alpha{AC_SRC_OVER, 0, 80, 0};
            AlphaBlend(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, tint.dc, 0, 0, 1, 1,
                       alpha);
        } else if (mark.tool == Tool::Pen) {
            if (mark.points.size() > 1)
                Polyline(dc, mark.points.data(), (int)mark.points.size());
            HBRUSH fill = CreateSolidBrush(mark.color);
            SelectObject(dc, fill);
            for (const auto& p : mark.points)
                Ellipse(dc, p.x - mark.width / 2, p.y - mark.width / 2, p.x + (mark.width + 1) / 2,
                        p.y + (mark.width + 1) / 2);
            SelectObject(dc, oldBrush);
            DeleteObject(fill);
        } else if (mark.tool == Tool::Text) {
            HFONT textFont =
                CreateFontW(-mark.width, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            HGDIOBJ old = SelectObject(dc, textFont);
            TextOutW(dc, a.x, a.y, mark.text.c_str(), (int)mark.text.size());
            SelectObject(dc, old);
            DeleteObject(textFont);
        } else if (mark.tool == Tool::Mosaic) {
            GdiFlush();
            RECT r = normalized(a, b);
            int blockSize = std::max(2, mark.width);
            for (int by = r.top; by < r.bottom; by += blockSize) {
                for (int bx = r.left; bx < r.right; bx += blockSize) {
                    RECT block{bx, by, std::min<LONG>(bx + blockSize, r.right),
                               std::min<LONG>(by + blockSize, r.bottom)};
                    int sx = std::clamp<int>(bx - offsetX, 0, surface.w - 1),
                        sy = std::clamp<int>(by - offsetY, 0, surface.h - 1);
                    BYTE* pixel =
                        surface.pixels + ((size_t)(surface.h - 1 - sy) * surface.w + sx) * 4;
                    HBRUSH brush = CreateSolidBrush(RGB(pixel[2], pixel[1], pixel[0]));
                    FillRect(dc, &block, brush);
                    DeleteObject(brush);
                }
            }
        }
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    void rounded(HDC dc, RECT r, COLORREF color, int radius = 12) {
        HBRUSH fill = CreateSolidBrush(color);
        HGDIOBJ oldBrush = SelectObject(dc, fill);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(fill);
    }
    std::wstring colorHex(COLORREF c) {
        wchar_t text[8];
        swprintf_s(text, L"%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
        return text;
    }
    void chooseColor(COLORREF c, bool history = true) {
        colors[15] = c;
        colorIndex = 15;
        updateStyle(history);
        hexInput = colorHex(c);
        if (history)
            rememberColor(c);
    }
    void rememberColor(COLORREF c) {
        recentColors.erase(std::remove(recentColors.begin(), recentColors.end(), c),
                           recentColors.end());
        recentColors.insert(recentColors.begin(), c);
        if (recentColors.size() > 6)
            recentColors.resize(6);
    }
    int sliderValue(POINT p, RECT r, int low, int high) {
        return low + (std::clamp<int>(p.x - r.left - 8, 0, r.right - r.left - 16) * (high - low) +
                      (r.right - r.left - 16) / 2) /
                         (r.right - r.left - 16);
    }
    void drawSlider(HDC dc, RECT r, int value, int low, int high, COLORREF accent) {
        int cy = (r.top + r.bottom) / 2;
        RECT rail{r.left + 8, cy - 2, r.right - 8, cy + 2};
        rounded(dc, rail, RGB(76, 86, 94), 4);
        int cx = r.left + 8 + (r.right - r.left - 16) * (value - low) / (high - low);
        RECT fill{rail.left, rail.top, cx, rail.bottom};
        rounded(dc, fill, accent, 4);
        RECT knob{cx - 6, cy - 6, cx + 6, cy + 6};
        rounded(dc, knob, RGB(251, 246, 241), 12);
    }
    RECT palette() {
        RECT bar = toolbar();
        int top = bar.top - 264;
        if (top < uiVisible.top + 6)
            top = bar.bottom + 8;
        top = std::clamp<int>(top, uiVisible.top + 6,
                              std::max<LONG>(uiVisible.top + 6, uiVisible.bottom - 262));
        int left = std::clamp<int>(bar.left + 12, uiVisible.left + 6,
                                   std::max<LONG>(uiVisible.left + 6, uiVisible.right - 332));
        return {left, top, left + 326, top + 256};
    }
    RECT hexRect() {
        RECT r = palette();
        return {r.left + 14, r.top + 166, r.left + 205, r.top + 202};
    }
    RECT channelRect(int channel) {
        RECT r = palette();
        return {r.left + 50, r.top + 76 + channel * 27, r.right - 16, r.top + 100 + channel * 27};
    }
    void drawPalette(HDC dc) {
        RECT r = palette();
        rounded(dc, r, RGB(41, 49, 56), 16);
        for (int i = 0; i < 12; i++) {
            int cx = r.left + 28 + (i % 6) * 53, cy = r.top + 22 + (i / 6) * 32;
            RECT swatch{cx - 12, cy - 12, cx + 12, cy + 12};
            rounded(dc, swatch, RGB(230, 229, 225), 24);
            InflateRect(&swatch, -2, -2);
            rounded(dc, swatch, colors[i], 20);
        }
        RECT recentLabel{r.left + 14, r.top + 216, r.left + 59, r.top + 246};
        SetTextColor(dc, RGB(174, 188, 197));
        DrawTextW(dc, L"最近", -1, &recentLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        for (int i = 0; i < (int)recentColors.size(); i++) {
            int cx = r.left + 76 + i * 43;
            RECT chip{cx - 11, r.top + 221, cx + 11, r.top + 243};
            rounded(dc, chip, RGB(222, 226, 230), 22);
            InflateRect(&chip, -2, -2);
            rounded(dc, chip, recentColors[i], 18);
        }
        const wchar_t* labels[] = {L"R", L"G", L"B"};
        int values[] = {GetRValue(colors[colorIndex]), GetGValue(colors[colorIndex]),
                        GetBValue(colors[colorIndex])};
        COLORREF accents[] = {RGB(236, 102, 113), RGB(96, 196, 148), RGB(103, 164, 242)};
        for (int i = 0; i < 3; i++) {
            RECT label{r.left + 18, r.top + 76 + i * 27, r.left + 45, r.top + 100 + i * 27};
            SetTextColor(dc, RGB(226, 232, 236));
            DrawTextW(dc, labels[i], -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            drawSlider(dc, channelRect(i), values[i], 0, 255, accents[i]);
        }
        RECT field = hexRect();
        rounded(dc, field, hexEditing ? RGB(82, 60, 73) : RGB(29, 36, 42), 8);
        field.left += 10;
        std::wstring value = L"#" + (hexEditing ? hexInput : colorHex(colors[colorIndex])) +
                             (hexEditing ? L"|" : L"");
        DrawTextW(dc, value.c_str(), -1, &field, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT pick{r.left + 215, r.top + 166, r.right - 12, r.top + 202};
        rounded(dc, pick, RGB(65, 77, 85), 8);
        DrawTextW(dc, L"拾色 G", -1, &pick, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    bool paletteClick(POINT p) {
        RECT r = palette();
        if (!PtInRect(&r, p)) {
            paletteOpen = false;
            hexEditing = false;
            return true;
        }
        for (int i = 0; i < 12; i++) {
            int cx = r.left + 28 + (i % 6) * 53, cy = r.top + 22 + (i / 6) * 32;
            RECT swatch{cx - 15, cy - 15, cx + 15, cy + 15};
            if (PtInRect(&swatch, p)) {
                chooseColor(colors[i]);
                paletteOpen = false;
                return true;
            }
        }
        for (int i = 0; i < (int)recentColors.size(); i++) {
            int cx = r.left + 76 + i * 43;
            RECT chip{cx - 14, r.top + 218, cx + 14, r.top + 246};
            if (PtInRect(&chip, p)) {
                COLORREF chosen = recentColors[i];
                chooseColor(chosen);
                paletteOpen = false;
                return true;
            }
        }
        for (int i = 0; i < 3; i++) {
            RECT track = channelRect(i);
            if (PtInRect(&track, p)) {
                checkpoint();
                channelDragging = i;
                hexEditing = false;
                SetCapture(window);
                updateChannel(p);
                return true;
            }
        }
        RECT field = hexRect();
        if (PtInRect(&field, p)) {
            hexEditing = true;
            hexReplace = true;
            hexInput = colorHex(colors[colorIndex]);
            return true;
        }
        if (p.y >= r.top + 166 && p.x >= r.left + 215) {
            paletteOpen = false;
            picking = true;
        }
        return true;
    }
    void updateChannel(POINT p) {
        int v = sliderValue(p, channelRect(channelDragging), 0, 255);
        COLORREF c = colors[colorIndex];
        int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
        if (channelDragging == 0)
            r = v;
        if (channelDragging == 1)
            g = v;
        if (channelDragging == 2)
            b = v;
        chooseColor(RGB(r, g, b), false);
    }
    void drawLoupe(HDC dc) {
        POINT location = uiPoint(pointer);
        int left = std::clamp<int>(location.x + 24, uiVisible.left + 4,
                                   std::max<LONG>(uiVisible.left + 4, uiVisible.right - 142));
        int top = std::clamp<int>(location.y + 24, uiVisible.top + 4,
                                  std::max<LONG>(uiVisible.top + 4, uiVisible.bottom - 160));
        RECT box{left, top, left + 134, top + 152};
        rounded(dc, box, RGB(32, 38, 43), 12);
        int sx = std::clamp<int>(pointer.x - 4, 0, std::max(0, w - 9)),
            sy = std::clamp<int>(pointer.y - 4, 0, std::max(0, h - 9));
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchBlt(dc, left + 13, top + 10, 108, 108, shot.dc, sx, sy, 9, 9, SRCCOPY);
        RECT pixel{left + 13 + (pointer.x - sx) * 12, top + 10 + (pointer.y - sy) * 12,
                   left + 25 + (pointer.x - sx) * 12, top + 22 + (pointer.y - sy) * 12};
        FrameRect(dc, &pixel, (HBRUSH)GetStockObject(WHITE_BRUSH));
        SetTextColor(dc, RGB(255, 255, 255));
        RECT text{left + 6, top + 122, left + 128, top + 148};
        std::wstring value = L"#" + colorHex(GetPixel(shot.dc, std::min<LONG>(pointer.x, w - 1),
                                                      std::min<LONG>(pointer.y, h - 1)));
        DrawTextW(dc, value.c_str(), -1, &text, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    std::vector<POINT> handles(const Mark& mark) {
        if (mark.tool == Tool::Arrow || mark.tool == Tool::Line)
            return {mark.points.front(), mark.points.back()};
        RECT r = geometry(mark);
        return {{r.left, r.top}, {r.right, r.top}, {r.right, r.bottom}, {r.left, r.bottom}};
    }
    int handleAt(const std::vector<POINT>& points, POINT p) {
        for (int i = 0; i < (int)points.size(); i++)
            if (abs(points[i].x - p.x) <= 7 && abs(points[i].y - p.y) <= 7)
                return i;
        return -1;
    }
    void transformMark(POINT p) {
        auto& mark = marks[activeMark];
        mark = originalMark;
        int dx = p.x - dragOrigin.x, dy = p.y - dragOrigin.y;
        RECT box = geometry(originalMark);
        if (transform == 1) {
            if (box.right - box.left <= selection.right - selection.left)
                dx = std::clamp<int>(dx, selection.left - box.left, selection.right - box.right);
            if (box.bottom - box.top <= selection.bottom - selection.top)
                dy = std::clamp<int>(dy, selection.top - box.top, selection.bottom - box.bottom);
            for (auto& point : mark.points) {
                point.x += dx;
                point.y += dy;
            }
        } else if (mark.tool == Tool::Arrow || mark.tool == Tool::Line) {
            mark.points[handle == 0 ? 0 : mark.points.size() - 1] = p;
        } else {
            RECT target = box;
            if (handle == 0 || handle == 3)
                target.left = std::min<LONG>(p.x, box.right - 2);
            else
                target.right = std::max<LONG>(p.x, box.left + 2);
            if (handle == 0 || handle == 1)
                target.top = std::min<LONG>(p.y, box.bottom - 2);
            else
                target.bottom = std::max<LONG>(p.y, box.top + 2);
            for (auto& point : mark.points) {
                point.x = target.left + MulDiv(point.x - box.left, target.right - target.left,
                                               std::max<LONG>(1, box.right - box.left));
                point.y = target.top + MulDiv(point.y - box.top, target.bottom - target.top,
                                              std::max<LONG>(1, box.bottom - box.top));
            }
            if (mark.tool == Tool::Text)
                mark.width = std::clamp<int>(MulDiv(originalMark.width, target.bottom - target.top,
                                                    std::max<LONG>(1, box.bottom - box.top)),
                                             8, 64);
            if (mark.tool == Tool::Number) {
                mark.width = std::clamp<int>((target.right - target.left) / 2 - 13, 1, 64);
                mark.points[0] = {(target.left + target.right) / 2,
                                  (target.top + target.bottom) / 2};
            }
        }
        syncStyle();
    }
    void compose() {
        Bitmap& buffer = preview;
        BitBlt(buffer.dc, 0, 0, w, h, shot.dc, 0, 0, SRCCOPY);
        // Darken a copy, keeping the unmodified capture separate from the selection UI.
        Bitmap tint(1, 1);
        tint.pixels[0] = 20;
        tint.pixels[1] = 18;
        tint.pixels[2] = 18;
        tint.pixels[3] = 255;
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 135, 0};
        AlphaBlend(buffer.dc, 0, 0, w, h, tint.dc, 0, 0, 1, 1, blend);
        if (selection.right > selection.left && selection.bottom > selection.top) {
            BitBlt(buffer.dc, selection.left, selection.top, selection.right - selection.left,
                   selection.bottom - selection.top, shot.dc, selection.left, selection.top,
                   SRCCOPY);
            int savedDC = SaveDC(buffer.dc);
            IntersectClipRect(buffer.dc, selection.left, selection.top, selection.right,
                              selection.bottom);
            for (auto& mark : marks)
                drawMark(buffer, mark);
            if (dragging && selected && tool != Tool::Text && tool != Tool::Select)
                drawMark(buffer, current);
            if (activeMark >= 0 && activeMark < (int)marks.size()) {
                RECT r = markBounds(marks[activeMark]);
                HPEN focus = CreatePen(PS_DOT, 1, RGB(255, 255, 255));
                HGDIOBJ old = SelectObject(buffer.dc, focus);
                HGDIOBJ oldBrush = SelectObject(buffer.dc, GetStockObject(NULL_BRUSH));
                Rectangle(buffer.dc, r.left, r.top, r.right, r.bottom);
                SelectObject(buffer.dc, oldBrush);
                SelectObject(buffer.dc, old);
                DeleteObject(focus);
                if (tool == Tool::Select)
                    for (auto p : handles(marks[activeMark])) {
                        RECT grip{p.x - 4, p.y - 4, p.x + 5, p.y + 5};
                        rounded(buffer.dc, grip, RGB(255, 255, 255), 4);
                    }
            }
            RestoreDC(buffer.dc, savedDC);
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(240, 103, 121));
            HGDIOBJ old = SelectObject(buffer.dc, pen),
                    brush = SelectObject(buffer.dc, GetStockObject(NULL_BRUSH));
            Rectangle(buffer.dc, selection.left, selection.top, selection.right, selection.bottom);
            SelectObject(buffer.dc, brush);
            SelectObject(buffer.dc, old);
            DeleteObject(pen);
            if (selected && tool == Tool::Select && activeMark < 0) {
                for (POINT grip : std::vector<POINT>{{selection.left, selection.top},
                                                     {selection.right, selection.top},
                                                     {selection.right, selection.bottom},
                                                     {selection.left, selection.bottom}}) {
                    RECT r{grip.x - 4, grip.y - 4, grip.x + 5, grip.y + 5};
                    rounded(buffer.dc, r, RGB(240, 103, 121), 4);
                }
            }
        }
        RECT bar = toolbar();
        int uiDC = SaveDC(buffer.dc);
        SetGraphicsMode(buffer.dc, GM_ADVANCED);
        XFORM scale{(FLOAT)uiScale, 0, 0, (FLOAT)uiScale, 0, 0};
        SetWorldTransform(buffer.dc, &scale);
        POINT pointerUI = uiPoint(pointer);
        HGDIOBJ oldFont = SelectObject(buffer.dc, font);
        SetBkMode(buffer.dc, TRANSPARENT);
        SetTextColor(buffer.dc, RGB(255, 255, 255));
        if (selected && !selectOnly) {
            RECT shadow = bar;
            OffsetRect(&shadow, 0, 3);
            rounded(buffer.dc, shadow, RGB(15, 18, 22), 18);
            rounded(buffer.dc, bar, RGB(32, 38, 43), 18);
            buttons.clear();
            std::vector<std::wstring> names = {L"选择", L"矩形", L"箭头", L"画笔", L"打码", L"文字",
                                               L"椭圆", L"序号", L"直线", L"高亮", L"撤销", L"重做",
                                               L"重选", L"复制", L"保存", L"取消"};
            int actions[] = {40, 0, 1, 2, 3, 4, 5, 6, 41, 42, 7, 8, 9, 12, 10, 11};
            int cell = (bar.right - bar.left - 16) / (int)names.size();
            for (int i = 0; i < (int)names.size(); i++) {
                RECT r{bar.left + 8 + i * cell, bar.top + 8, bar.left + 8 + (i + 1) * cell - 2,
                       bar.top + 44};
                int action = actions[i];
                buttons.push_back({names[i], r, action});
                bool active = (action == 40 && tool == Tool::Select) ||
                              (action < 7 && (int)tool == action + 1) ||
                              (action == 41 && tool == Tool::Line) ||
                              (action == 42 && tool == Tool::Highlight);
                bool enabled = !(action == 7 && undo.empty()) && !(action == 8 && redo.empty());
                if (active || action == 10 || (enabled && PtInRect(&r, pointerUI)))
                    rounded(buffer.dc, r,
                            active || action == 10 ? RGB(197, 87, 107) : RGB(57, 67, 74));
                SetTextColor(buffer.dc, enabled ? RGB(250, 245, 241) : RGB(111, 121, 128));
                DrawTextW(buffer.dc, names[i].c_str(), -1, &r,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            int cy = bar.top + 65;
            SetTextColor(buffer.dc, RGB(185, 199, 206));
            RECT colorLabel{bar.left + 14, cy - 12, bar.left + 54, cy + 12};
            DrawTextW(buffer.dc, L"颜色", -1, &colorLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT swatch{bar.left + 66, cy - 14, bar.left + 106, cy + 14};
            rounded(buffer.dc, swatch, RGB(245, 240, 236), 10);
            RECT inset = swatch;
            InflateRect(&inset, -3, -3);
            rounded(buffer.dc, inset, colors[colorIndex], 8);
            buttons.push_back({L"颜色", swatch, 50});
            RECT pick{bar.left + 118, cy - 14, bar.left + 170, cy + 14};
            buttons.push_back({L"拾色", pick, 51});
            SetTextColor(buffer.dc, RGB(220, 229, 232));
            DrawTextW(buffer.dc, L"拾色", -1, &pick, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT widthLabel{bar.left + 180, cy - 14, bar.left + 230, cy + 14};
            const wchar_t* parameter = tool == Tool::Text     ? L"字号"
                                       : tool == Tool::Mosaic ? L"块大小"
                                                              : L"粗细";
            DrawTextW(buffer.dc, parameter, -1, &widthLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            widthTrack = {bar.left + 242, cy - 14, bar.left + 392, cy + 14};
            drawSlider(buffer.dc, widthTrack, thickness, tool == Tool::Text ? 8 : 1, 64,
                       RGB(224, 124, 141));
            buttons.push_back({L"滑块", widthTrack, 60});
            RECT value{bar.left + 402, cy - 14, bar.left + 455, cy + 14};
            std::wstring size = std::to_wstring(thickness) + L" px";
            DrawTextW(buffer.dc, size.c_str(), -1, &value, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            std::wstring dimension = std::to_wstring(selection.right - selection.left) + L" × " +
                                     std::to_wstring(selection.bottom - selection.top);
            RECT info{bar.left + 465, cy - 12, bar.right - 16, cy + 12};
            SetTextColor(buffer.dc, RGB(185, 199, 206));
            DrawTextW(buffer.dc, dimension.c_str(), -1, &info,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            std::wstring hint =
                activeMark >= 0
                    ? L"拖动移动 · 角点缩放 / 端点调整 · Delete 删除 · Ctrl+D 复制 · 双击文字编辑"
                    : L"V 选择 · R 矩形 A 箭头 P 画笔 M 马赛克 T 文字 · G 拾色 · 滚轮调粗细";
            for (const auto& button : buttons) {
                if (!PtInRect(&button.rect, pointerUI))
                    continue;
                if (button.action == 7)
                    hint = L"撤销上一步 · Ctrl+Z";
                if (button.action == 8)
                    hint = L"重做 · Ctrl+Y";
                if (button.action == 9)
                    hint = L"重新选择区域，清除本次标注";
                if (button.action == 10)
                    hint = L"保存 PNG 并复制到剪贴板 · Enter / Ctrl+S";
                if (button.action == 11)
                    hint = L"放弃本次截图 · Esc";
                if (button.action >= 20 && button.action < 25)
                    hint = L"选择颜色：立即修改选中标注，或设置下一笔颜色";
                if (button.action >= 30 && button.action < 33)
                    hint = L"选择线条粗细 / 文字大小 / 马赛克块大小";
            }
            if (textEditing)
                hint = L"正在编辑文字 · Enter 完成文字 · Esc 结束编辑 · 再次 Enter 保存截图";
            if (!notice.empty())
                hint = notice;
            RECT help{bar.left + 16, bar.top + 90, bar.right - 12, bar.bottom - 5};
            SetTextColor(buffer.dc, RGB(170, 183, 190));
            DrawTextW(buffer.dc, hint.c_str(), -1, &help,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            RECT r{uiVisible.left + 20, uiVisible.top + 20,
                   std::min<LONG>(uiVisible.right - 20, uiVisible.left + 880), uiVisible.top + 65};
            HBRUSH dark = CreateSolidBrush(RGB(35, 35, 41));
            FillRect(buffer.dc, &r, dark);
            DeleteObject(dark);
            std::wstring text = mode == L"region" ? L"拖动选择区域，按 Esc 取消。"
                                                  : L"点击选择屏幕或窗口，按 Esc 取消。";
            if (!notice.empty())
                text = notice;
            r.left += 14;
            DrawTextW(buffer.dc, text.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        if (paletteOpen)
            drawPalette(buffer.dc);
        if (picking)
            drawLoupe(buffer.dc);
        SelectObject(buffer.dc, oldFont);
        RestoreDC(buffer.dc, uiDC);
    }
    void paint() {
        PAINTSTRUCT ps;
        HDC target = BeginPaint(window, &ps);
        compose();
        BitBlt(target, 0, 0, w, h, preview.dc, 0, 0, SRCCOPY);
        EndPaint(window, &ps);
    }
    void checkpoint() {
        undo.push_back({marks, selection});
        if (undo.size() > 100)
            undo.erase(undo.begin());
        redo.clear();
    }
    void setTool(Tool next) {
        endText();
        toolWidths[(int)tool] = thickness;
        toolColors[(int)tool] = colors[colorIndex];
        tool = next;
        activeMark = -1;
        thickness = toolWidths[(int)tool];
        colors[15] = toolColors[(int)tool];
        colorIndex = 15;
        paletteOpen = false;
        picking = false;
        notice.clear();
    }
    void syncText() {
        if (!textInput || activeMark < 0 || activeMark >= (int)marks.size())
            return;
        int length = GetWindowTextLengthW(textInput);
        std::wstring value((size_t)length + 1, L'\0');
        GetWindowTextW(textInput, value.data(), length + 1);
        value.resize(length);
        marks[activeMark].text = std::move(value);
    }
    static LRESULT CALLBACK textProcedure(HWND input, UINT msg, WPARAM wp, LPARAM lp) {
        auto editor = (Editor*)GetWindowLongPtrW(input, GWLP_USERDATA);
        if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
            editor->endText();
            InvalidateRect(editor->window, nullptr, FALSE);
            return 0;
        }
        if (msg == WM_KEYDOWN && wp == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            editor->endText();
            editor->finish();
            return 0;
        }
        return CallWindowProcW(editor->textDefault, input, msg, wp, lp);
    }
    void beginText(bool existing) {
        if (activeMark < 0 || activeMark >= (int)marks.size())
            return;
        if (existing)
            checkpoint();
        textEditing = true;
        auto& mark = marks[activeMark];
        inputFont =
            CreateFontW(-mark.width, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                        0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        POINT p = mark.points.front();
        int available = std::max<LONG>(24, selection.right - p.x);
        textInput =
            CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                            p.x, p.y, std::min(available, 440), mark.width + 12, window, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
        if (!textInput) {
            DeleteObject(inputFont);
            inputFont = nullptr;
            // Keep the overlay's Unicode WM_CHAR input path as a fallback.
            return;
        }
        SetWindowLongPtrW(textInput, GWLP_USERDATA, (LONG_PTR)this);
        textDefault = (WNDPROC)SetWindowLongPtrW(textInput, GWLP_WNDPROC, (LONG_PTR)textProcedure);
        SendMessageW(textInput, WM_SETFONT, (WPARAM)inputFont, TRUE);
        SendMessageW(textInput, EM_SETLIMITTEXT, 4096, 0);
        SetWindowTextW(textInput, mark.text.c_str());
        SetFocus(textInput);
        SendMessageW(textInput, EM_SETSEL, existing ? 0 : (WPARAM)-1, (LPARAM)-1);
    }
    void endText() {
        if (textInput) {
            syncText();
            HWND input = textInput;
            textInput = nullptr;
            DestroyWindow(input);
            DeleteObject(inputFont);
            inputFont = nullptr;
            SetFocus(window);
        }
        if (textEditing && activeMark >= 0 && activeMark < (int)marks.size() &&
            marks[activeMark].text.empty()) {
            marks.erase(marks.begin() + activeMark);
            activeMark = -1;
        }
        textEditing = false;
        if (activeMark >= (int)marks.size())
            activeMark = -1;
    }
    void resetSelection() {
        endText();
        selected = false;
        selection = {};
        marks.clear();
        undo.clear();
        redo.clear();
        activeMark = -1;
        tool = Tool::Rectangle;
        thickness = toolWidths[1];
        mode = L"region";
        paletteOpen = false;
        picking = false;
        buttons.clear();
        notice.clear();
    }
    void clickAction(int action) {
        endText();
        notice.clear();
        if (action == 40) {
            setTool(Tool::Select);
            return;
        }
        if (action == 41 || action == 42) {
            setTool(action == 41 ? Tool::Line : Tool::Highlight);
            return;
        }
        if (action == 50) {
            paletteOpen = !paletteOpen;
            hexEditing = false;
            return;
        }
        if (action == 51) {
            picking = true;
            paletteOpen = false;
            return;
        }
        paletteOpen = false;
        if (action < 7) {
            setTool((Tool)(action + 1));
            return;
        }
        switch (action) {
        case 7:
            undoMark();
            break;
        case 8:
            redoMark();
            break;
        case 9:
            resetSelection();
            break;
        case 10:
            finish();
            break;
        case 11:
            DestroyWindow(window);
            break;
        case 12:
            finish(true);
            break;
        }
    }
    void commitMark(Mark mark) {
        checkpoint();
        activeMark = (int)marks.size();
        marks.push_back(std::move(mark));
    }
    void updateStyle(bool history = true) {
        toolWidths[(int)tool] = thickness;
        toolColors[(int)tool] = colors[colorIndex];
        if (activeMark < 0 || activeMark >= (int)marks.size())
            return;
        auto& mark = marks[activeMark];
        if (mark.width == thickness && mark.color == colors[colorIndex])
            return;
        if (history)
            checkpoint();
        mark.width = thickness;
        mark.color = colors[colorIndex];
        toolWidths[(int)mark.tool] = thickness;
        toolColors[(int)mark.tool] = mark.color;
    }
    RECT geometry(const Mark& mark) {
        RECT r{mark.points[0].x, mark.points[0].y, mark.points[0].x, mark.points[0].y};
        for (auto p : mark.points) {
            r.left = std::min(r.left, p.x);
            r.top = std::min(r.top, p.y);
            r.right = std::max(r.right, p.x);
            r.bottom = std::max(r.bottom, p.y);
        }
        if (mark.tool == Tool::Text) {
            HFONT f =
                CreateFontW(-mark.width, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            auto old = SelectObject(shot.dc, f);
            SIZE size{};
            GetTextExtentPoint32W(shot.dc, mark.text.c_str(), (int)mark.text.size(), &size);
            SelectObject(shot.dc, old);
            DeleteObject(f);
            r.right += std::max<LONG>(size.cx, 12);
            r.bottom += std::max<LONG>(size.cy, mark.width);
        }
        if (mark.tool == Tool::Number)
            InflateRect(&r, 13 + mark.width, 13 + mark.width);
        if (mark.tool == Tool::Highlight && r.bottom - r.top < mark.width)
            r.bottom = r.top + mark.width;
        return r;
    }
    RECT markBounds(const Mark& mark) {
        RECT r = geometry(mark);
        InflateRect(&r, 8, 8);
        return r;
    }
    void syncStyle() {
        if (activeMark < 0 || activeMark >= (int)marks.size())
            return;
        thickness = marks[activeMark].width;
        colors[15] = marks[activeMark].color;
        colorIndex = 15;
    }
    void selectMark(POINT p) {
        activeMark = -1;
        for (int i = (int)marks.size() - 1; i >= 0; i--) {
            RECT r = markBounds(marks[i]);
            if (PtInRect(&r, p)) {
                activeMark = i;
                syncStyle();
                break;
            }
        }
    }
    void undoMark() {
        endText();
        if (undo.empty())
            return;
        redo.push_back({marks, selection});
        selection = undo.back().selection;
        marks = std::move(undo.back().marks);
        undo.pop_back();
        activeMark = -1;
    }
    void redoMark() {
        endText();
        if (redo.empty())
            return;
        undo.push_back({marks, selection});
        selection = redo.back().selection;
        marks = std::move(redo.back().marks);
        redo.pop_back();
        activeMark = -1;
    }
    int buttonAt(POINT p) {
        p = uiPoint(p);
        for (const auto& button : buttons)
            if (PtInRect(&button.rect, p))
                return button.action;
        return -1;
    }
    LRESULT message(UINT msg, WPARAM wp, LPARAM lp) {
        POINT p = bound({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        auto refresh = [&]() { InvalidateRect(window, nullptr, FALSE); };
        switch (msg) {
        case WM_CTLCOLOREDIT:
            if ((HWND)lp == textInput && activeMark >= 0) {
                SetTextColor((HDC)wp, marks[activeMark].color);
                SetBkColor((HDC)wp, RGB(255, 255, 255));
                return (LRESULT)GetStockObject(WHITE_BRUSH);
            }
            break;
        case WM_COMMAND:
            if ((HWND)lp == textInput && HIWORD(wp) == EN_CHANGE) {
                syncText();
                refresh();
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paint();
            return 0;
        case WM_SETCURSOR: {
            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(window, &cursor);
            LPCWSTR icon = selected && buttonAt(cursor) >= 0  ? IDC_HAND
                           : tool == Tool::Select && selected ? IDC_ARROW
                                                              : IDC_CROSS;
            SetCursor(LoadCursorW(nullptr, icon));
            return TRUE;
        }
        case WM_MOUSEMOVE:
            pointer = p;
            if (sliderDragging) {
                thickness = sliderValue(uiPoint(p), widthTrack, tool == Tool::Text ? 8 : 1, 64);
                updateStyle(false);
                refresh();
                return 0;
            }
            if (channelDragging >= 0) {
                updateChannel(uiPoint(p));
                refresh();
                return 0;
            }
            if (picking) {
                refresh();
                return 0;
            }
            if (transform && (p.x != dragOrigin.x || p.y != dragOrigin.y)) {
                p.x = std::clamp<LONG>(p.x, 0, w - 1);
                p.y = std::clamp<LONG>(p.y, 0, h - 1);
                if (!gestureChanged) {
                    checkpoint();
                    gestureChanged = true;
                }
                if (transform == 3) {
                    selection = originalSelection;
                    if (handle == 0 || handle == 3)
                        selection.left = std::min(p.x, originalSelection.right - 4);
                    else
                        selection.right = std::max(p.x, originalSelection.left + 4);
                    if (handle == 0 || handle == 1)
                        selection.top = std::min(p.y, originalSelection.bottom - 4);
                    else
                        selection.bottom = std::max(p.y, originalSelection.top + 4);
                } else {
                    p.x = std::clamp<LONG>(p.x, selection.left, selection.right - 1);
                    p.y = std::clamp<LONG>(p.y, selection.top, selection.bottom - 1);
                    transformMark(p);
                }
                refresh();
                return 0;
            }
            if (!selected) {
                if (mode == L"region" && dragging)
                    selection = normalized(anchor, p);
                else if (mode != L"region")
                    hover(p);
                refresh();
                return 0;
            }
            if (dragging && tool != Tool::Text && tool != Tool::Select) {
                p.x = std::clamp<LONG>(p.x, selection.left, selection.right - 1);
                p.y = std::clamp<LONG>(p.y, selection.top, selection.bottom - 1);
                if (tool == Tool::Pen)
                    current.points.push_back(p);
                else {
                    if ((wp & MK_SHIFT) && tool != Tool::Mosaic) {
                        POINT first = current.points.front();
                        if (tool == Tool::Arrow || tool == Tool::Line) {
                            if (abs(p.x - first.x) > abs(p.y - first.y))
                                p.y = first.y;
                            else
                                p.x = first.x;
                        } else {
                            int size = std::min(abs(p.x - first.x), abs(p.y - first.y));
                            p.x = first.x + (p.x >= first.x ? size : -size);
                            p.y = first.y + (p.y >= first.y ? size : -size);
                        }
                    }
                    current.points.back() = p;
                }
                refresh();
                return 0;
            }
            // Only toolbar hover changes need a full desktop repaint when idle.
            if (buttonAt(p) != buttonAt(lastHover))
                refresh();
            lastHover = p;
            return 0;
        case WM_LBUTTONDBLCLK:
            if (selected && !paletteOpen && !picking && buttonAt(p) < 0) {
                selectMark(p);
                if (activeMark >= 0 && marks[activeMark].tool == Tool::Text) {
                    tool = Tool::Text;
                    beginText(true);
                    refresh();
                    return 0;
                }
            }
            return 0;
        case WM_LBUTTONDOWN:
            pointer = p;
            if (picking) {
                chooseColor(
                    GetPixel(shot.dc, std::min<LONG>(p.x, w - 1), std::min<LONG>(p.y, h - 1)));
                picking = false;
                refresh();
                return 0;
            }
            if (paletteOpen) {
                paletteClick(uiPoint(p));
                refresh();
                return 0;
            }
            if (selected) {
                int action = buttonAt(p);
                if (action == 60) {
                    endText();
                    checkpoint();
                    sliderDragging = true;
                    SetCapture(window);
                    thickness = sliderValue(uiPoint(p), widthTrack, tool == Tool::Text ? 8 : 1, 64);
                    updateStyle(false);
                    refresh();
                    return 0;
                }
                if (action >= 0) {
                    clickAction(action);
                    refresh();
                    return 0;
                }
                endText();
                if (tool == Tool::Select) {
                    handle = activeMark >= 0 ? handleAt(handles(marks[activeMark]), p) : -1;
                    if (handle >= 0)
                        transform = 2;
                    else {
                        selectMark(p);
                        transform = activeMark >= 0 ? 1 : 0;
                    }
                    if (activeMark >= 0)
                        originalMark = marks[activeMark];
                    else {
                        handle = handleAt({{selection.left, selection.top},
                                           {selection.right, selection.top},
                                           {selection.right, selection.bottom},
                                           {selection.left, selection.bottom}},
                                          p);
                        if (handle >= 0) {
                            transform = 3;
                            originalSelection = selection;
                        }
                    }
                    dragOrigin = p;
                    gestureChanged = false;
                    if (transform)
                        SetCapture(window);
                    refresh();
                    return 0;
                }
                if (!PtInRect(&selection, p))
                    return 0;
                if (tool == Tool::Number) {
                    int number = 1;
                    for (const auto& mark : marks)
                        if (mark.tool == Tool::Number)
                            number++;
                    commitMark({tool, {p}, std::to_wstring(number), colors[colorIndex], thickness});
                    refresh();
                    return 0;
                }
                if (tool == Tool::Text) {
                    commitMark({tool, {p}, L"", colors[colorIndex], thickness});
                    beginText(false);
                    refresh();
                    return 0;
                }
                activeMark = -1;
                current = {tool, {p, p}, L"", colors[colorIndex], thickness};
            } else {
                anchor = p;
                if (mode != L"region")
                    hover(p);
            }
            dragging = true;
            SetCapture(window);
            return 0;
        case WM_LBUTTONUP:
            if (sliderDragging || channelDragging >= 0) {
                if (channelDragging >= 0)
                    rememberColor(colors[colorIndex]);
                sliderDragging = false;
                channelDragging = -1;
                ReleaseCapture();
                refresh();
                return 0;
            }
            if (transform) {
                transform = 0;
                ReleaseCapture();
                refresh();
                return 0;
            }
            if (!dragging)
                return 0;
            dragging = false;
            ReleaseCapture();
            if (!selected) {
                if (mode == L"region")
                    selection = normalized(anchor, p);
                if (selection.right - selection.left >= 4 &&
                    selection.bottom - selection.top >= 4) {
                    selected = true;
                    if (selectOnly)
                        finish();
                } else {
                    selection = {};
                    notice = L"区域太小，请重新拖动；Esc 可取消。";
                }
            } else if (tool != Tool::Text && tool != Tool::Select)
                commitMark(current);
            refresh();
            return 0;
        case WM_CAPTURECHANGED:
            dragging = false;
            transform = 0;
            sliderDragging = false;
            channelDragging = -1;
            refresh();
            return 0;
        case WM_RBUTTONDOWN:
            if (picking || paletteOpen) {
                picking = false;
                paletteOpen = false;
            } else if (selected)
                resetSelection();
            else
                DestroyWindow(window);
            refresh();
            return 0;
        case WM_MOUSEWHEEL:
            if (selected && !textEditing && !paletteOpen) {
                thickness = std::clamp(thickness + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1),
                                       tool == Tool::Text ? 8 : 1, 64);
                updateStyle();
                refresh();
            }
            return 0;
        case WM_IME_STARTCOMPOSITION:
            if (!textEditing)
                return 0;
            break;
        case WM_CHAR:
            if (hexEditing && paletteOpen) {
                if (wp == 8 && !hexInput.empty()) {
                    if (hexReplace)
                        hexInput.clear();
                    else
                        hexInput.pop_back();
                    hexReplace = false;
                } else if (((wp >= '0' && wp <= '9') || (wp >= 'A' && wp <= 'F') ||
                            (wp >= 'a' && wp <= 'f')) &&
                           (hexReplace || hexInput.size() < 6)) {
                    if (hexReplace)
                        hexInput.clear();
                    hexReplace = false;
                    hexInput.push_back((wchar_t)wp);
                }
                refresh();
                return 0;
            }
            if (textEditing && activeMark >= 0 && activeMark < (int)marks.size()) {
                auto& text = marks[activeMark].text;
                if (wp == 8 && !text.empty()) {
                    wchar_t last = text.back();
                    text.pop_back();
                    if (last >= 0xDC00 && last <= 0xDFFF && !text.empty())
                        text.pop_back();
                } else if (wp >= 32 && !(GetKeyState(VK_CONTROL) & 0x8000))
                    text.push_back((wchar_t)wp);
                refresh();
            }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                if (paletteOpen || picking) {
                    paletteOpen = false;
                    picking = false;
                    hexEditing = false;
                } else if (textEditing)
                    endText();
                else
                    DestroyWindow(window);
                refresh();
                return 0;
            }
            if (wp == VK_RETURN && selected) {
                if (hexEditing && paletteOpen) {
                    if (hexInput.size() == 6) {
                        unsigned long value = wcstoul(hexInput.c_str(), nullptr, 16);
                        chooseColor(RGB((value >> 16) & 255, (value >> 8) & 255, value & 255));
                        hexEditing = false;
                        paletteOpen = false;
                    }
                } else if (textEditing)
                    endText();
                else if (!paletteOpen && !picking)
                    finish();
                refresh();
                return 0;
            }
            if (hexEditing || picking || paletteOpen)
                return 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (wp == 'Z') {
                    if (GetKeyState(VK_SHIFT) & 0x8000)
                        redoMark();
                    else
                        undoMark();
                }
                if (wp == 'Y')
                    redoMark();
                if (wp == 'S' || wp == 'C') {
                    endText();
                    finish(wp == 'C');
                }
                if (wp == 'D' && activeMark >= 0) {
                    Mark duplicate = marks[activeMark];
                    RECT r = geometry(duplicate);
                    int dx = std::min<LONG>(12, selection.right - r.right),
                        dy = std::min<LONG>(12, selection.bottom - r.bottom);
                    for (auto& point : duplicate.points) {
                        point.x += dx;
                        point.y += dy;
                    }
                    commitMark(std::move(duplicate));
                }
            } else if (!textEditing && selected) {
                if (wp == VK_DELETE && activeMark >= 0) {
                    checkpoint();
                    marks.erase(marks.begin() + activeMark);
                    activeMark = -1;
                }
                if (wp == 'V')
                    setTool(Tool::Select);
                if (wp == 'R')
                    setTool(Tool::Rectangle);
                if (wp == 'A')
                    setTool(Tool::Arrow);
                if (wp == 'P')
                    setTool(Tool::Pen);
                if (wp == 'M')
                    setTool(Tool::Mosaic);
                if (wp == 'T')
                    setTool(Tool::Text);
                if (wp == 'O')
                    setTool(Tool::Ellipse);
                if (wp == 'N')
                    setTool(Tool::Number);
                if (wp == 'L')
                    setTool(Tool::Line);
                if (wp == 'H')
                    setTool(Tool::Highlight);
                if (wp == 'G')
                    picking = true;
                if (tool == Tool::Select && activeMark >= 0 && wp >= VK_LEFT && wp <= VK_DOWN) {
                    checkpoint();
                    int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
                    originalMark = marks[activeMark];
                    dragOrigin = {0, 0};
                    transform = 1;
                    transformMark({wp == VK_LEFT    ? -step
                                   : wp == VK_RIGHT ? step
                                                    : 0,
                                   wp == VK_UP     ? -step
                                   : wp == VK_DOWN ? step
                                                   : 0});
                    transform = 0;
                }
            }
            refresh();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, msg, wp, lp);
    }
};
LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto editor = (Editor*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        editor = (Editor*)((CREATESTRUCTW*)lp)->lpCreateParams;
        editor->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)editor);
    }
    if (editor) {
        try {
            return editor->message(msg, wp, lp);
        } catch (const std::exception& e) {
            editor->failed = true;
            std::cerr << e.what() << std::endl;
            MessageBoxA(hwnd, e.what(), "Sakura Capture", MB_OK | MB_ICONERROR);
            DestroyWindow(hwnd);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace
int snip(const std::wstring& path, const std::wstring& mode, bool selectOnly) {
    Editor editor(path, mode, selectOnly);
    WNDCLASSW wc{};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = procedure;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SakuraCaptureOverlay";
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName,
                                  L"Sakura Capture", WS_POPUP | WS_CLIPCHILDREN, editor.x, editor.y,
                                  editor.w, editor.h, nullptr, nullptr, wc.hInstance, &editor);
    if (!window)
        throw std::runtime_error("Cannot create screenshot overlay");
    // SWP_SHOWWINDOW is not overridden by a parent's STARTUPINFO/SW_HIDE.
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    POINT cursor{};
    GetCursorPos(&cursor);
    editor.hover({cursor.x - editor.x, cursor.y - editor.y});
    SetForegroundWindow(window);
    SetFocus(window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (editor.failed)
        return 1;
    if (selectOnly && editor.selected) {
        auto r = editor.selection;
        std::cout << "REGION " << r.left + editor.x << " " << r.top + editor.y << " "
                  << r.right - r.left << " " << r.bottom - r.top << std::endl;
    } else if (editor.copied)
        std::cout << "COPIED" << std::endl;
    else if (editor.saved)
        std::cout << (editor.clipboardFailed ? "SAVED clipboard_failed" : "SAVED") << std::endl;
    else
        std::cout << "CANCELLED" << std::endl;
    return 0;
}

void imageSelfTest(const std::wstring& path) {
    Editor editor(L"", L"region", false, true);
    editor.selection = {20, 16, 180, 112};
    editor.commitMark({Tool::Mosaic, {{40, 40}, {100, 90}}, L"", RGB(0, 0, 0), 14});
    editor.commitMark({Tool::Rectangle, {{120, 30}, {160, 70}}, L"", RGB(255, 0, 0), 3});
    Bitmap result(160, 96);
    editor.renderSelection(result);
    savePNG(result, path);
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&factory)),
          "Create test image factory");
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                             WICDecodeMetadataCacheOnLoad, &decoder),
          "Decode screenshot PNG");
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, &frame), "Read screenshot PNG frame");
    ComPtr<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(&converter), "Create test PNG converter");
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone,
                                nullptr, 0, WICBitmapPaletteTypeCustom),
          "Convert test PNG");
    std::vector<BYTE> pixels(160 * 96 * 4);
    check(converter->CopyPixels(nullptr, 160 * 4, (UINT)pixels.size(), pixels.data()),
          "Read test PNG pixels");
    auto at = [&](int x, int y) { return pixels.data() + ((size_t)y * 160 + x) * 4; };
    if (at(0, 0)[0] != 20 || at(0, 0)[1] != 16 || at(0, 0)[2] != 120 || at(159, 95)[0] != 179 ||
        at(159, 95)[1] != 111 || at(159, 95)[2] != 120)
        throw std::runtime_error("PNG crop or vertical orientation is incorrect");
    BYTE* mosaic = at(25, 29);
    if (mosaic[0] != 40 || mosaic[1] != 40)
        throw std::runtime_error("Mosaic was not burned into exported pixels");
    if (at(100, 14)[2] != 255 || at(100, 14)[1] != 0)
        throw std::runtime_error("Rectangle annotation was not exported");
    editor.undoMark();
    if (editor.marks.size() != 1)
        throw std::runtime_error("Screenshot undo failed");
    editor.redoMark();
    if (editor.marks.size() != 2)
        throw std::runtime_error("Screenshot redo failed");
    editor.marks.push_back({Tool::Ellipse, {{30, 30}, {60, 60}}, L"", RGB(0, 255, 0), 3});
    editor.marks.push_back({Tool::Number, {{75, 80}}, L"1", RGB(236, 74, 92), 3});
    editor.renderSelection(result);
    if (GetPixel(result.dc, 25, 14) != RGB(0, 255, 0))
        throw std::runtime_error("Ellipse annotation was not exported");
    editor.activeMark = 0;
    editor.thickness = 9;
    editor.updateStyle();
    if (editor.marks[0].width != 9)
        throw std::runtime_error("Existing annotation width did not update");
    editor.undoMark();
    if (editor.marks[0].width != 14)
        throw std::runtime_error("Annotation style undo failed");
    editor.redoMark();
    if (editor.marks[0].width != 9)
        throw std::runtime_error("Annotation style redo failed");
    editor.undo.clear();
    editor.redo.clear();
    editor.activeMark = 1;
    editor.tool = Tool::Select;
    editor.syncStyle();
    editor.checkpoint();
    for (int width : {4, 8, 17, 23}) {
        editor.thickness = width;
        editor.updateStyle(false);
    }
    if (editor.undo.size() != 1 || editor.marks[1].width != 23)
        throw std::runtime_error("Continuous slider did not group its undo gesture");
    editor.undoMark();
    if (editor.marks[1].width != 3)
        throw std::runtime_error("Slider gesture undo failed");
    editor.activeMark = 1;
    editor.syncStyle();
    editor.chooseColor(RGB(17, 83, 149));
    if (editor.marks[1].color != RGB(17, 83, 149))
        throw std::runtime_error("Custom RGB color did not update selected mark");
    editor.undoMark();
    if (editor.marks[1].color != RGB(255, 0, 0))
        throw std::runtime_error("Custom color undo failed");
    editor.selection = {0, 0, 1120, 720};
    editor.activeMark = 1;
    editor.originalMark = editor.marks[1];
    editor.dragOrigin = {0, 0};
    editor.transform = 1;
    editor.checkpoint();
    editor.transformMark({25, 18});
    if (editor.marks[1].points[0].x != 145 || editor.marks[1].points[0].y != 48)
        throw std::runtime_error("Selected mark move failed");
    editor.undoMark();
    editor.activeMark = 1;
    editor.originalMark = editor.marks[1];
    editor.transform = 2;
    editor.handle = 2;
    editor.transformMark({190, 100});
    if (editor.marks[1].points.back().x != 190 || editor.marks[1].points.back().y != 100)
        throw std::runtime_error("Selected mark resize failed");
    editor.checkpoint();
    editor.marks.erase(editor.marks.begin() + 1);
    editor.undoMark();
    if (editor.marks.size() != 4 || editor.marks[1].tool != Tool::Rectangle)
        throw std::runtime_error("Deleted annotation undo failed");
    editor.redoMark();
    if (editor.marks.size() != 3)
        throw std::runtime_error("Deleted annotation redo failed");
    editor.chooseColor(GetPixel(editor.shot.dc, 123, 77));
    if (editor.colors[editor.colorIndex] != RGB(120, 77, 123))
        throw std::runtime_error("Eyedropper must sample the original frozen image");
    RECT originalCrop = editor.selection;
    editor.checkpoint();
    editor.selection.right -= 32;
    editor.undoMark();
    if (!EqualRect(&editor.selection, &originalCrop))
        throw std::runtime_error("Crop geometry undo failed");
    editor.transform = 0;
    editor.activeMark = -1;
    editor.selected = true;
    editor.selection = {90, 65, 1000, 465};
    editor.marks = {{Tool::Rectangle, {{160, 130}, {600, 270}}, L"", RGB(236, 74, 92), 3},
                    {Tool::Arrow, {{780, 340}, {600, 240}}, L"", RGB(236, 74, 92), 6},
                    {Tool::Number, {{150, 115}}, L"1", RGB(236, 74, 92), 3},
                    {Tool::Text, {{220, 320}}, L"捕捉灵感，随手标注。", RGB(255, 255, 255), 28}};
    editor.compose();
    savePNG(editor.preview, path + L".editor.png");
    editor.paletteOpen = true;
    editor.compose();
    savePNG(editor.preview, path + L".palette.png");
    editor.paletteOpen = false;
    editor.picking = true;
    editor.pointer = {480, 210};
    editor.compose();
    savePNG(editor.preview, path + L".eyedropper.png");
    editor.picking = false;
    editor.uiScale = 1.5;
    editor.compose();
    savePNG(editor.preview, path + L".editor-150.png");
    std::cout << "IMAGE_SELFTEST_OK crop=160x96 mosaic=burned-in undo=ok slider=grouped "
                 "custom-color=ok eyedropper=original move=ok resize=ok delete=ok crop-undo=ok"
              << std::endl;
}
