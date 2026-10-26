#include "common.h"

namespace {
enum class Tool { Select, Rectangle, Arrow, Pen, Mosaic, Text, Ellipse, Number };
struct Mark {
    Tool tool;
    std::vector<POINT> points;
    std::wstring text;
    COLORREF color;
    int width;
};
struct Button {
    std::wstring label;
    RECT rect;
    int action;
};
struct Editor {
    std::wstring path, mode;
    bool selectOnly = false, selected = false, dragging = false, saved = false, failed = false,
         clipboardFailed = false;
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN),
        w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HWND window = nullptr;
    Bitmap shot{w, h};
    Bitmap preview{w, h};
    POINT anchor{};
    RECT selection{};
    Tool tool = Tool::Rectangle;
    int thickness = 3, colorIndex = 0;
    COLORREF colors[5] = {RGB(236, 74, 92), RGB(36, 124, 232), RGB(44, 168, 100), RGB(245, 175, 36),
                          RGB(255, 255, 255)};
    std::vector<Mark> marks, redo;
    std::vector<Button> buttons;
    Mark current{};
    HFONT font = nullptr;
    POINT pointer{};
    bool textEditing = false;
    Editor(std::wstring p, std::wstring m, bool only, bool fake = false)
        : path(p), mode(m), selectOnly(only), x(fake ? 0 : GetSystemMetrics(SM_XVIRTUALSCREEN)),
          y(fake ? 0 : GetSystemMetrics(SM_YVIRTUALSCREEN)),
          w(fake ? 1120 : GetSystemMetrics(SM_CXVIRTUALSCREEN)),
          h(fake ? 720 : GetSystemMetrics(SM_CYVIRTUALSCREEN)) {
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
                           DEFAULT_PITCH, L"Segoe UI");
    }
    ~Editor() {
        if (font)
            DeleteObject(font);
    }
    POINT bound(POINT p) {
        p.x = std::clamp<LONG>(p.x, 0, w);
        p.y = std::clamp<LONG>(p.y, 0, h);
        return p;
    }
    RECT toolbar() {
        int width = std::min(w - 12, 864);
        int left = std::clamp<int>(selection.left, 6, std::max(6, w - width - 6));
        int top = selection.bottom + 10;
        if (top + 124 > h)
            top = std::max(6, (int)selection.top - 132);
        return {left, top, left + width, std::min(top + 122, h - 2)};
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
            GetMonitorInfoW(MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST), &mi);
            selection = {mi.rcMonitor.left - x, mi.rcMonitor.top - y, mi.rcMonitor.right - x,
                         mi.rcMonitor.bottom - y};
        } else if (mode == L"window") {
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
    void finish() {
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
        savePNG(result, path);
        saved = true;
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
        } else if (mark.tool == Tool::Arrow) {
            MoveToEx(dc, a.x, a.y, nullptr);
            LineTo(dc, b.x, b.y);
            double angle = atan2((double)b.y - a.y, (double)b.x - a.x);
            double length = 14 + mark.width * 2;
            for (double side : {-0.5, 0.5}) {
                MoveToEx(dc, b.x, b.y, nullptr);
                LineTo(dc, (int)(b.x - length * cos(angle + side)),
                       (int)(b.y - length * sin(angle + side)));
            }
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
                CreateFontW(-24 - mark.width * 2, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            HGDIOBJ old = SelectObject(dc, textFont);
            TextOutW(dc, a.x, a.y, mark.text.c_str(), (int)mark.text.size());
            SelectObject(dc, old);
            DeleteObject(textFont);
        } else if (mark.tool == Tool::Mosaic) {
            GdiFlush();
            RECT r = normalized(a, b);
            int blockSize = 8 + mark.width * 2;
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
            if (dragging && selected && tool != Tool::Text)
                drawMark(buffer, current);
            RestoreDC(buffer.dc, savedDC);
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(240, 103, 121));
            HGDIOBJ old = SelectObject(buffer.dc, pen),
                    brush = SelectObject(buffer.dc, GetStockObject(NULL_BRUSH));
            Rectangle(buffer.dc, selection.left, selection.top, selection.right, selection.bottom);
            SelectObject(buffer.dc, brush);
            SelectObject(buffer.dc, old);
            DeleteObject(pen);
        }
        HGDIOBJ oldFont = SelectObject(buffer.dc, font);
        SetBkMode(buffer.dc, TRANSPARENT);
        SetTextColor(buffer.dc, RGB(255, 255, 255));
        if (selected && !selectOnly) {
            RECT bar = toolbar();
            RECT shadow = bar;
            OffsetRect(&shadow, 0, 3);
            rounded(buffer.dc, shadow, RGB(15, 18, 22), 18);
            rounded(buffer.dc, bar, RGB(32, 38, 43), 18);
            buttons.clear();
            std::vector<std::wstring> names = {L"矩形", L"箭头", L"画笔", L"马赛克",
                                               L"文字", L"椭圆", L"序号", L"撤销",
                                               L"重做", L"重选", L"保存", L"取消"};
            int cell = (bar.right - bar.left - 16) / 12;
            for (int i = 0; i < 12; i++) {
                RECT r{bar.left + 8 + i * cell, bar.top + 8, bar.left + 8 + (i + 1) * cell - 3,
                       bar.top + 44};
                buttons.push_back({names[i], r, i});
                bool active = i < 7 && (int)tool == i + 1;
                bool enabled = !(i == 7 && marks.empty()) && !(i == 8 && redo.empty());
                bool hover = PtInRect(&r, pointer);
                if (active || i == 10 || (hover && enabled))
                    rounded(buffer.dc, r, active || i == 10 ? RGB(197, 87, 107) : RGB(57, 67, 74));
                SetTextColor(buffer.dc, enabled ? RGB(250, 245, 241) : RGB(111, 121, 128));
                DrawTextW(buffer.dc, names[i].c_str(), -1, &r,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            int cy = bar.top + 65;
            for (int i = 0; i < 5; i++) {
                int cx = bar.left + 25 + i * 30;
                RECT r{cx - 12, cy - 12, cx + 12, cy + 12};
                buttons.push_back({L"颜色", r, 20 + i});
                if (i == colorIndex)
                    rounded(buffer.dc, r, RGB(249, 245, 241), 24);
                InflateRect(&r, -3, -3);
                rounded(buffer.dc, r, colors[i], 18);
            }
            int widthLeft = bar.left + 178;
            for (int i = 0; i < 3; i++) {
                RECT r{widthLeft + i * 38, cy - 13, widthLeft + i * 38 + 32, cy + 13};
                buttons.push_back({L"粗细", r, 30 + i});
                rounded(buffer.dc, r, thickness == (i + 1) * 3 ? RGB(82, 60, 67) : RGB(46, 55, 62));
                int radius = (i + 1) * 2;
                RECT dot{r.left + 16 - radius, cy - radius, r.left + 16 + radius, cy + radius};
                rounded(buffer.dc, dot, RGB(249, 245, 241), radius * 2);
            }
            std::wstring dimension = std::to_wstring(selection.right - selection.left) + L" × " +
                                     std::to_wstring(selection.bottom - selection.top);
            RECT info{bar.left + 310, cy - 12, bar.right - 16, cy + 12};
            SetTextColor(buffer.dc, RGB(185, 199, 206));
            DrawTextW(buffer.dc, dimension.c_str(), -1, &info,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            std::wstring hint = L"R 矩形  A 箭头  P 画笔  M 马赛克  T 文字  O 椭圆  N 序号";
            for (const auto& button : buttons) {
                if (!PtInRect(&button.rect, pointer))
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
                    hint = L"点击选择标注颜色，仅影响之后的新标注";
                if (button.action >= 30)
                    hint = L"选择线条粗细 / 文字大小 / 马赛克块大小";
            }
            if (textEditing)
                hint = L"输入文字 · Backspace 删除 · 点击其他工具结束输入 · Enter 保存";
            RECT help{bar.left + 16, bar.top + 90, bar.right - 12, bar.bottom - 5};
            SetTextColor(buffer.dc, RGB(170, 183, 190));
            DrawTextW(buffer.dc, hint.c_str(), -1, &help,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            RECT r{20, 20, std::min(w - 20, 880), 65};
            HBRUSH dark = CreateSolidBrush(RGB(35, 35, 41));
            FillRect(buffer.dc, &r, dark);
            DeleteObject(dark);
            std::wstring text = mode == L"region" ? L"拖动选择区域，按 Esc 取消。"
                                                  : L"点击选择屏幕或窗口，按 Esc 取消。";
            r.left += 14;
            DrawTextW(buffer.dc, text.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(buffer.dc, oldFont);
    }
    void paint() {
        PAINTSTRUCT ps;
        HDC target = BeginPaint(window, &ps);
        compose();
        BitBlt(target, 0, 0, w, h, preview.dc, 0, 0, SRCCOPY);
        EndPaint(window, &ps);
    }
    void clickAction(int action) {
        textEditing = false;
        if (action >= 20 && action < 25) {
            colorIndex = action - 20;
            return;
        }
        if (action >= 30 && action < 33) {
            thickness = (action - 29) * 3;
            return;
        }
        if (action < 7) {
            tool = (Tool)(action + 1);
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
            selected = false;
            selection = {};
            marks.clear();
            redo.clear();
            tool = Tool::Rectangle;
            mode = L"region";
            break;
        case 10:
            finish();
            break;
        case 11:
            DestroyWindow(window);
            break;
        }
    }
    void undoMark() {
        if (!marks.empty()) {
            redo.push_back(marks.back());
            marks.pop_back();
        }
    }
    void redoMark() {
        if (!redo.empty()) {
            marks.push_back(redo.back());
            redo.pop_back();
        }
    }
    LRESULT message(UINT msg, WPARAM wp, LPARAM lp) {
        POINT p = bound({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paint();
            return 0;
        case WM_MOUSEMOVE:
            pointer = p;
            if (!selected) {
                if (mode == L"region" && dragging)
                    selection = normalized(anchor, p);
                else if (mode != L"region")
                    hover(p);
            } else if (dragging && tool != Tool::Text) {
                p.x = std::clamp<LONG>(p.x, selection.left, selection.right - 1);
                p.y = std::clamp<LONG>(p.y, selection.top, selection.bottom - 1);
                if (tool == Tool::Pen)
                    current.points.push_back(p);
                else {
                    if ((wp & MK_SHIFT) && tool != Tool::Mosaic) {
                        POINT first = current.points.front();
                        if (tool == Tool::Arrow) {
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
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            if (selected) {
                for (auto& b : buttons) {
                    if (PtInRect(&b.rect, p)) {
                        clickAction(b.action);
                        InvalidateRect(window, nullptr, FALSE);
                        return 0;
                    }
                }
                if (!PtInRect(&selection, p))
                    return 0;
                redo.clear();
                if (tool == Tool::Number) {
                    int number = 1;
                    for (const auto& mark : marks)
                        if (mark.tool == Tool::Number)
                            number++;
                    marks.push_back(
                        {tool, {p}, std::to_wstring(number), colors[colorIndex], thickness});
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
                if (tool == Tool::Text) {
                    textEditing = true;
                    marks.push_back({tool, {p}, L"", colors[colorIndex], thickness});
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
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
                }
            } else if (tool != Tool::Text) {
                marks.push_back(current);
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_RBUTTONDOWN:
            if (selected) {
                selected = false;
                marks.clear();
                redo.clear();
                selection = {};
                mode = L"region";
            } else
                DestroyWindow(window);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_CHAR:
            if (textEditing && selected && tool == Tool::Text && !marks.empty() &&
                marks.back().tool == Tool::Text) {
                if (wp == 8) {
                    if (!marks.back().text.empty())
                        marks.back().text.pop_back();
                } else if (wp >= 32 && !(GetKeyState(VK_CONTROL) & 0x8000)) {
                    marks.back().text.push_back((wchar_t)wp);
                }
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                DestroyWindow(window);
                return 0;
            }
            if (wp == VK_RETURN && selected) {
                finish();
                return 0;
            }
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                textEditing = false;
                if (wp == 'Z')
                    undoMark();
                if (wp == 'Y')
                    redoMark();
                if (wp == 'S' || wp == 'C')
                    finish();
            } else if (selected && tool != Tool::Text) {
                if (wp == 'R')
                    tool = Tool::Rectangle;
                if (wp == 'A')
                    tool = Tool::Arrow;
                if (wp == 'P')
                    tool = Tool::Pen;
                if (wp == 'M')
                    tool = Tool::Mosaic;
                if (wp == 'T')
                    tool = Tool::Text;
                if (wp == 'O')
                    tool = Tool::Ellipse;
                if (wp == 'N')
                    tool = Tool::Number;
            }
            InvalidateRect(window, nullptr, FALSE);
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
    wc.lpfnWndProc = procedure;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SakuraCaptureOverlay";
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName,
                                  L"Sakura Capture", WS_POPUP, editor.x, editor.y, editor.w,
                                  editor.h, nullptr, nullptr, wc.hInstance, &editor);
    if (!window)
        throw std::runtime_error("Cannot create screenshot overlay");
    ShowWindow(window, SW_SHOW);
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
    } else if (editor.saved)
        std::cout << (editor.clipboardFailed ? "SAVED clipboard_failed" : "SAVED") << std::endl;
    else
        std::cout << "CANCELLED" << std::endl;
    return 0;
}

void imageSelfTest(const std::wstring& path) {
    Editor editor(L"", L"region", false, true);
    editor.selection = {20, 16, 180, 112};
    editor.marks.push_back({Tool::Mosaic, {{40, 40}, {100, 90}}, L"", RGB(0, 0, 0), 3});
    editor.marks.push_back({Tool::Rectangle, {{120, 30}, {160, 70}}, L"", RGB(255, 0, 0), 3});
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
    editor.selected = true;
    editor.selection = {90, 65, 1000, 465};
    editor.marks = {{Tool::Rectangle, {{160, 130}, {600, 270}}, L"", RGB(236, 74, 92), 3},
                    {Tool::Arrow, {{780, 340}, {600, 240}}, L"", RGB(236, 74, 92), 6},
                    {Tool::Number, {{150, 115}}, L"1", RGB(236, 74, 92), 3},
                    {Tool::Text, {{220, 320}}, L"捕捉灵感，随手标注。", RGB(255, 255, 255), 3}};
    editor.compose();
    savePNG(editor.preview, path + L".editor.png");
    std::cout << "IMAGE_SELFTEST_OK crop=160x96 mosaic=burned-in undo=ok" << std::endl;
}
