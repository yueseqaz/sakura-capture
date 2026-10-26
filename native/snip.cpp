#include "common.h"

namespace {
enum class Tool { Select, Rectangle, Arrow, Pen, Mosaic, Text };
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
    Editor(std::wstring p, std::wstring m, bool only, bool fake = false)
        : path(p), mode(m), selectOnly(only), x(fake ? 0 : GetSystemMetrics(SM_XVIRTUALSCREEN)),
          y(fake ? 0 : GetSystemMetrics(SM_YVIRTUALSCREEN)),
          w(fake ? 320 : GetSystemMetrics(SM_CXVIRTUALSCREEN)),
          h(fake ? 180 : GetSystemMetrics(SM_CYVIRTUALSCREEN)) {
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
        int width = std::min(w - 12, 858);
        int left = std::clamp<int>(selection.left, 6, std::max(6, w - width - 6));
        int top = selection.bottom + 10;
        if (top + 70 > h)
            top = std::max(6, (int)selection.top - 78);
        return {left, top, left + width, top + 68};
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
        if (mark.tool == Tool::Rectangle) {
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
            for (int by = r.top; by < r.bottom; by += 14) {
                for (int bx = r.left; bx < r.right; bx += 14) {
                    RECT block{bx, by, std::min<LONG>(bx + 14, r.right),
                               std::min<LONG>(by + 14, r.bottom)};
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
    void paint() {
        PAINTSTRUCT ps;
        HDC target = BeginPaint(window, &ps);
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
            HBRUSH dark = CreateSolidBrush(RGB(35, 35, 41));
            FillRect(buffer.dc, &bar, dark);
            DeleteObject(dark);
            buttons.clear();
            std::vector<std::wstring> names = {L"矩形", L"箭头", L"画笔", L"马赛克",
                                               L"文字", L"颜色", L"线宽", L"撤销",
                                               L"重做", L"重选", L"保存", L"取消"};
            int cell = (bar.right - bar.left) / 12;
            for (int i = 0; i < 12; i++) {
                RECT r{bar.left + i * cell, bar.top, bar.left + (i + 1) * cell, bar.top + 38};
                buttons.push_back({names[i], r, i});
                bool active = (i < 5 && (int)tool == i + 1);
                if (active) {
                    HBRUSH accent = CreateSolidBrush(RGB(166, 65, 85));
                    FillRect(buffer.dc, &r, accent);
                    DeleteObject(accent);
                }
                SetTextColor(buffer.dc, i == 5 ? colors[colorIndex] : RGB(250, 250, 250));
                DrawTextW(buffer.dc, names[i].c_str(), -1, &r,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            SetTextColor(buffer.dc, RGB(190, 190, 195));
            RECT help{bar.left + 10, bar.top + 40, bar.right, bar.bottom};
            std::wstring text = std::to_wstring(selection.right - selection.left) + L" x " +
                                std::to_wstring(selection.bottom - selection.top) +
                                L"   Enter 保存并复制   Ctrl+Z/Y 撤销/重做   Esc 取消";
            DrawTextW(buffer.dc, text.c_str(), -1, &help, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
        BitBlt(target, 0, 0, w, h, buffer.dc, 0, 0, SRCCOPY);
        EndPaint(window, &ps);
    }
    void clickAction(int action) {
        if (action < 5) {
            tool = (Tool)(action + 1);
            return;
        }
        switch (action) {
        case 5:
            colorIndex = (colorIndex + 1) % 5;
            break;
        case 6:
            thickness = thickness >= 9 ? 3 : thickness + 3;
            break;
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
                else
                    current.points.back() = p;
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
                if (tool == Tool::Text) {
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
            if (selected && tool == Tool::Text && !marks.empty() &&
                marks.back().tool == Tool::Text) {
                if (wp == 8) {
                    if (!marks.back().text.empty())
                        marks.back().text.pop_back();
                } else if (wp >= 32) {
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
    std::cout << "IMAGE_SELFTEST_OK crop=160x96 mosaic=burned-in undo=ok" << std::endl;
}
