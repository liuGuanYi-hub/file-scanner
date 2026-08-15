#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <objbase.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "scanner/domain/types.h"
#include "scanner/domain/utilities.h"
#include "scanner/infrastructure/file_scanner.h"

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kWindowClassName[] = L"CppFileScannerWindow";
constexpr int kWindowWidth = 980;
constexpr int kWindowHeight = 620;

constexpr int IDC_PATH_EDIT = 1001;
constexpr int IDC_BROWSE_BUTTON = 1002;
constexpr int IDC_WORKER_EDIT = 1003;
constexpr int IDC_START_BUTTON = 1004;
constexpr int IDC_STATUS_LABEL = 1005;
constexpr int IDC_RESULT_EDIT = 1006;
constexpr int IDC_PROGRESS_BAR = 1007;
constexpr int IDC_EXTENSION_LIST = 1008;
constexpr int IDC_TITLE_LABEL = 1009;
constexpr int IDC_PATH_LABEL = 1010;
constexpr int IDC_WORKER_LABEL = 1011;
constexpr int IDC_RESULT_LABEL = 1012;
constexpr int IDC_EXTENSION_FILTER_LABEL = 1013;
constexpr int IDC_EXTENSION_FILTER_EDIT = 1014;
constexpr int IDC_MIN_SIZE_LABEL = 1015;
constexpr int IDC_MIN_SIZE_EDIT = 1016;
constexpr int IDC_MAX_SIZE_LABEL = 1017;
constexpr int IDC_MAX_SIZE_EDIT = 1018;
constexpr int IDC_TREEMAP = 1019;
constexpr UINT_PTR kProgressTimer = 1;
constexpr UINT WM_SCAN_COMPLETE = WM_APP + 1;

// === 现代 macOS 风格配色 ===
constexpr COLORREF kBgColor = RGB(242, 242, 247);         // 浅灰背景 (macOS window bg)
constexpr COLORREF kCardColor = RGB(255, 255, 255);       // 白色卡片
constexpr COLORREF kTitleColor = RGB(28, 28, 30);         // 深色标题
constexpr COLORREF kTextColor = RGB(60, 60, 67);          // 正文文字
constexpr COLORREF kSecondaryText = RGB(142, 142, 147);   // 次要文字
constexpr COLORREF kAccentColor = RGB(0, 122, 255);       // macOS 蓝
constexpr COLORREF kAccentHover = RGB(0, 100, 220);       // 悬停蓝
constexpr COLORREF kButtonTextColor = RGB(255, 255, 255); // 按钮白字
constexpr COLORREF kInputBg = RGB(246, 246, 250);         // 输入框背景
constexpr COLORREF kInputBorder = RGB(209, 209, 214);     // 输入框边框
constexpr COLORREF kProgressTrack = RGB(233, 233, 237);   // 进度条底色
constexpr COLORREF kProgressFill = RGB(0, 122, 255);      // 进度条填充
constexpr COLORREF kSeparator = RGB(229, 229, 234);       // 分割线
constexpr COLORREF kErrorText = RGB(255, 59, 48);         // 红色错误
constexpr COLORREF kSuccessText = RGB(52, 199, 89);       // 绿色成功

struct GuiState {
    HWND title_label = nullptr;
    HWND path_label = nullptr;
    HWND path_edit = nullptr;
    HWND browse_button = nullptr;
    HWND worker_label = nullptr;
    HWND worker_edit = nullptr;
    HWND extension_filter_label = nullptr;
    HWND extension_filter_edit = nullptr;
    HWND min_size_label = nullptr;
    HWND min_size_edit = nullptr;
    HWND max_size_label = nullptr;
    HWND max_size_edit = nullptr;
    HWND start_button = nullptr;
    HWND status_label = nullptr;
    HWND progress_bar = nullptr;
    HWND result_label = nullptr;
    HWND result_edit = nullptr;
    HWND extension_list = nullptr;

    HBRUSH window_brush = nullptr;
    HBRUSH card_brush = nullptr;
    HBRUSH input_brush = nullptr;
    HFONT title_font = nullptr;
    HFONT section_font = nullptr;

    std::atomic_bool cancel{false};
    std::atomic_bool closing{false};
    std::atomic<std::uint64_t> discovered_files{0};
    std::thread worker;

    // Treemap 数据
    HWND treemap = nullptr;
    std::vector<scanner::domain::FileMetadata> treemap_data;
};

struct ScanMessage {
    bool success = false;
    scanner::domain::ScanResult result;
    std::string error;
};

GuiState* get_state(HWND window) {
    return reinterpret_cast<GuiState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

std::wstring get_window_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return L"无法转换错误信息";
    }

    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

void set_control_font(HWND control) {
    SendMessageW(control,
                 WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
}

void enable_input_controls(GuiState* state, bool enabled) {
    EnableWindow(state->path_edit, enabled);
    EnableWindow(state->browse_button, enabled);
    EnableWindow(state->worker_edit, enabled);
    EnableWindow(state->extension_filter_edit, enabled);
    EnableWindow(state->min_size_edit, enabled);
    EnableWindow(state->max_size_edit, enabled);
    EnableWindow(state->start_button, enabled);
}

void layout_controls(HWND window, GuiState* state) {
    RECT client{};
    GetClientRect(window, &client);

    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int margin = 24;
    const int label_width = 88;
    const int browse_width = 92;
    const int row_height = 28;
    const int top_section_height = 172;
    const int section_gap = 16;
    const int result_top = top_section_height + section_gap;
    const int result_bottom = height - 18;
    const int result_height = std::max(120, result_bottom - result_top - 24);
    const int summary_width = std::max(260, std::min(340, width / 2 - margin));
    const int table_x = margin + summary_width + 20;
    const int table_width = std::max(260, width - table_x - margin);

    MoveWindow(state->title_label, margin, 14, width - margin * 2, 28, TRUE);
    MoveWindow(state->path_label, margin, 48, label_width, 22, TRUE);
    MoveWindow(state->path_edit,
               margin + label_width,
               48,
               std::max(180, width - margin * 2 - label_width - browse_width - 8),
               row_height,
               TRUE);
    MoveWindow(state->browse_button,
               width - margin - browse_width,
               48,
               browse_width,
               row_height,
               TRUE);
    MoveWindow(state->worker_label, margin, 92, label_width, 22, TRUE);
    MoveWindow(state->worker_edit, margin + label_width, 92, 74, row_height, TRUE);
    MoveWindow(state->start_button, margin + label_width + 90, 92, 112, row_height, TRUE);
    MoveWindow(state->status_label,
               margin + label_width + 220,
               96,
               std::max(180, width - margin * 2 - label_width - 220),
               22,
               TRUE);
    MoveWindow(state->extension_filter_label, margin, 132, label_width, 22, TRUE);
    MoveWindow(state->extension_filter_edit, margin + label_width, 128, 190, row_height, TRUE);
    MoveWindow(state->min_size_label, 330, 132, 72, 22, TRUE);
    MoveWindow(state->min_size_edit, 402, 128, 100, row_height, TRUE);
    MoveWindow(state->max_size_label, 520, 132, 72, 22, TRUE);
    MoveWindow(state->max_size_edit,
               592,
               128,
               std::max(100, width - 592 - margin),
               row_height,
               TRUE);
    MoveWindow(state->progress_bar, margin, 164, width - margin * 2, 16, TRUE);
    MoveWindow(state->result_label, margin, result_top, summary_width, 22, TRUE);
    MoveWindow(state->result_edit, margin, result_top + 24, summary_width, result_height, TRUE);
    MoveWindow(state->extension_list, table_x, result_top + 24, table_width, result_height, TRUE);
    const int count_column_width = 70;
    const int ratio_column_width = 70;
    const int extension_column_width =
        std::max(100, table_width - count_column_width - ratio_column_width);
    ListView_SetColumnWidth(state->extension_list, 0, extension_column_width);
    ListView_SetColumnWidth(state->extension_list, 1, count_column_width);
    ListView_SetColumnWidth(
        state->extension_list,
        2,
        std::max(70, table_width - extension_column_width - count_column_width));

    // Treemap 布局（底部全宽区域）
    const int treemap_top = result_top + 24 + result_height + 12;
    const int treemap_height = std::max(120, height - treemap_top - 12);
    MoveWindow(state->treemap, margin, treemap_top,
               width - margin * 2, treemap_height, TRUE);
}

void set_status(GuiState* state, const std::wstring& text) {
    SetWindowTextW(state->status_label, text.c_str());
}

std::wstring format_result(const scanner::domain::ScanResult& result) {
    std::wostringstream output;
    output << L"扫描完成\r\n"
           << L"文件数: " << result.file_count << L"\r\n"
           << L"总大小: " << std::fixed << std::setprecision(2)
           << static_cast<double>(result.total_bytes) / (1024.0 * 1024.0)
           << L" MiB\r\n"
           << L"错误数: " << result.error_count << L"\r\n"
           << L"元数据记录: " << result.files.size() << L" 条\r\n"
           << L"耗时: " << std::fixed << std::setprecision(2)
           << result.elapsed_seconds << L" 秒\r\n\r\n"
           << L"扩展名统计已显示在右侧表格。\r\n\r\n";

    if (result.errors.empty()) {
        output << L"错误详情: 无";
    } else {
        output << L"错误详情（最多显示 10 条）:\r\n";
        const std::size_t limit = result.errors.size() < 10 ? result.errors.size() : 10;
        for (std::size_t index = 0; index < limit; ++index) {
            output << L"  [" << utf8_to_wide(result.errors[index].path) << L"] "
                   << utf8_to_wide(result.errors[index].message) << L"\r\n";
        }
        if (result.errors.size() > limit) {
            output << L"  ... 其余 " << (result.errors.size() - limit)
                   << L" 条错误已省略";
        }
    }

    return output.str();
}

void populate_extension_table(GuiState* state, const scanner::domain::ScanResult& result) {
    ListView_DeleteAllItems(state->extension_list);

    int index = 0;
    for (const auto& [extension, count] : result.extension_counts) {
        const std::wstring extension_text = utf8_to_wide(extension);
        const std::wstring count_text = std::to_wstring(count);
        const double ratio = result.file_count == 0
                                 ? 0.0
                                 : 100.0 * static_cast<double>(count) /
                                       static_cast<double>(result.file_count);

        std::wostringstream ratio_stream;
        ratio_stream << std::fixed << std::setprecision(1) << ratio << L"%";
        const std::wstring ratio_text = ratio_stream.str();

        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = index;
        item.pszText = const_cast<LPWSTR>(extension_text.c_str());
        ListView_InsertItem(state->extension_list, &item);
        ListView_SetItemText(state->extension_list,
                             index,
                             1,
                             const_cast<LPWSTR>(count_text.c_str()));
        ListView_SetItemText(state->extension_list,
                             index,
                             2,
                             const_cast<LPWSTR>(ratio_text.c_str()));
        ++index;
    }
}

void start_progress(GuiState* state) {
    SendMessageW(state->progress_bar, PBM_SETPOS, 0, 0);
    SendMessageW(state->progress_bar, PBM_SETMARQUEE, TRUE, 30);
}

void finish_progress(GuiState* state) {
    SendMessageW(state->progress_bar, PBM_SETMARQUEE, FALSE, 0);
    SendMessageW(state->progress_bar, PBM_SETPOS, 100, 0);
}

void draw_button(const DRAWITEMSTRUCT* draw_item) {
    const bool is_start_button = draw_item->CtlID == IDC_START_BUTTON;
    const bool enabled = (draw_item->itemState & ODS_DISABLED) == 0;
    const bool pressed = (draw_item->itemState & ODS_SELECTED) != 0;

    HDC hdc = draw_item->hDC;
    RECT rc = draw_item->rcItem;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    // 创建兼容位图实现双缓冲（消除闪烁）
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
    SelectObject(memDC, memBM);

    // 背景
    HBRUSH bg = CreateSolidBrush(kBgColor);
    FillRect(memDC, &rc, bg);
    DeleteObject(bg);

    // 按钮区域（带 6px 圆角和阴影边距）
    int margin = 1;
    RECT br = {rc.left + margin, rc.top + margin,
               rc.right - margin, rc.bottom - margin};
    int radius = 14;

    // 按钮填充
    COLORREF fill = is_start_button ? (pressed ? kAccentHover : kAccentColor) : kCardColor;
    if (!enabled) fill = RGB(200, 200, 207);

    HBRUSH fillBrush = CreateSolidBrush(fill);
    HPEN fillPen = CreatePen(PS_SOLID, 0, fill);
    SelectObject(memDC, fillBrush);
    SelectObject(memDC, fillPen);
    RoundRect(memDC, br.left, br.top, br.right, br.bottom, radius, radius);
    DeleteObject(fillBrush);
    DeleteObject(fillPen);

    // 白色边框（macOS 风格细线）
    if (!is_start_button) {
        HPEN border = CreatePen(PS_SOLID, 1, kInputBorder);
        SelectObject(memDC, GetStockObject(NULL_BRUSH));
        SelectObject(memDC, border);
        RoundRect(memDC, br.left, br.top, br.right, br.bottom, radius, radius);
        DeleteObject(border);
    }

    // 文字
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, is_start_button && enabled ? kButtonTextColor : kTextColor);
    SelectObject(memDC, GetStockObject(DEFAULT_GUI_FONT));

    wchar_t text[64]{};
    GetWindowTextW(draw_item->hwndItem, text, _countof(text));
    DrawTextW(memDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 复制到屏幕
    BitBlt(hdc, rc.left, rc.top, w, h, memDC, rc.left, rc.top, SRCCOPY);
    DeleteObject(memBM);
    DeleteDC(memDC);
}

void browse_for_directory(HWND window, GuiState* state) {
    BROWSEINFOW browse_info{};
    browse_info.hwndOwner = window;
    browse_info.lpszTitle = L"选择要扫描的目录";
    browse_info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&browse_info);
    if (!item) {
        return;
    }

    wchar_t path[MAX_PATH]{};
    if (SHGetPathFromIDListW(item, path)) {
        SetWindowTextW(state->path_edit, path);
    }
    CoTaskMemFree(item);
}

void start_scan(HWND window, GuiState* state) {
    if (state->worker.joinable()) {
        return;
    }

    const std::wstring path_text = get_window_text(state->path_edit);
    std::size_t worker_count = 0;
    scanner::domain::FileFilter filter;
    try {
        worker_count = scanner::domain::parse_worker_count(get_window_text(state->worker_edit));
        filter.extensions = scanner::domain::parse_extensions(
            get_window_text(state->extension_filter_edit));

        const std::wstring min_size_text = get_window_text(state->min_size_edit);
        if (!min_size_text.empty()) {
            filter.min_size = scanner::domain::parse_size_bytes(min_size_text);
        }

        const std::wstring max_size_text = get_window_text(state->max_size_edit);
        if (!max_size_text.empty()) {
            filter.max_size = scanner::domain::parse_size_bytes(max_size_text);
        }
        if (filter.min_size && filter.max_size &&
            *filter.min_size > *filter.max_size) {
            throw std::invalid_argument("最小文件大小不能大于最大文件大小");
        }
    } catch (const std::exception& error) {
        const std::wstring message = utf8_to_wide(error.what());
        MessageBoxW(window, message.c_str(), L"参数错误", MB_OK | MB_ICONWARNING);
        return;
    }

    if (path_text.empty()) {
        MessageBoxW(window, L"请输入或选择要扫描的目录。", L"参数错误", MB_OK | MB_ICONWARNING);
        return;
    }

    state->cancel.store(false, std::memory_order_relaxed);
    state->discovered_files.store(0, std::memory_order_relaxed);
    enable_input_controls(state, false);
    SetWindowTextW(state->result_edit, L"");
    ListView_DeleteAllItems(state->extension_list);
    start_progress(state);
    set_status(state, L"正在扫描...");
    SetTimer(window, kProgressTimer, 150, nullptr);

    const fs::path root(path_text);
    state->worker = std::thread([window, state, root, worker_count, filter = std::move(filter)]() mutable {
        scanner::infrastructure::FileScanner file_scanner(nullptr);
        try {
            scanner::domain::ScanOptions options;
            options.root = root;
            options.worker_count = worker_count;
            options.cancel = &state->cancel;
            options.discovered_files = &state->discovered_files;
            options.filter = std::move(filter);
            auto result = file_scanner.scan(options);
            auto* message = new ScanMessage();
            message->success = true;
            message->result = std::move(result);
            if (state->closing.load(std::memory_order_relaxed) ||
                !PostMessageW(window, WM_SCAN_COMPLETE, 0, reinterpret_cast<LPARAM>(message))) {
                delete message;
            }
        } catch (const std::exception& error) {
            auto* message = new ScanMessage();
            message->success = false;
            message->error = error.what();
            if (state->closing.load(std::memory_order_relaxed) ||
                !PostMessageW(window, WM_SCAN_COMPLETE, 0, reinterpret_cast<LPARAM>(message))) {
                delete message;
            }
        }
    });
}

void handle_scan_complete(HWND window, GuiState* state, ScanMessage* message) {
    if (state->worker.joinable()) {
        state->worker.join();
    }

    KillTimer(window, kProgressTimer);
    finish_progress(state);
    enable_input_controls(state, true);

    if (message->success) {
        SetWindowTextW(state->result_edit, format_result(message->result).c_str());
        populate_extension_table(state, message->result);
        // 更新 Treemap 数据
        state->treemap_data = message->result.files;
        InvalidateRect(state->treemap, nullptr, TRUE);
        if (message->result.cancelled) {
            set_status(state, L"扫描已停止");
        } else if (message->result.error_count > 0) {
            set_status(state, L"扫描完成（存在错误）");
        } else {
            set_status(state, L"扫描完成");
        }
    } else {
        const std::wstring error = utf8_to_wide(message->error);
        SetWindowTextW(state->result_edit, error.c_str());
        ListView_DeleteAllItems(state->extension_list);
        set_status(state, L"扫描失败");
        MessageBoxW(window, error.c_str(), L"扫描失败", MB_OK | MB_ICONERROR);
    }

    delete message;
}

// === Treemap 窗口过程 ===
LRESULT CALLBACK treemap_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    GuiState* state = reinterpret_cast<GuiState*>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));

    if (msg == WM_PAINT && state) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        // 背景
        HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        auto& data = state->treemap_data;
        if (data.empty()) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(150, 150, 150));
            DrawTextW(hdc, L"扫描完成后显示磁盘占用 Treemap", -1, &rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (w > 10 && h > 10) {
            // 按扩展名分组并计算总大小
            std::map<std::string, std::uintmax_t> ext_sizes;
            std::uintmax_t total = 0;
            for (const auto& f : data) { ext_sizes[f.extension] += f.size; total += f.size; }
            if (total == 0) total = 1;

            // 按大小排序
            std::vector<std::pair<std::string, std::uintmax_t>> sorted;
            for (const auto& [ext, sz] : ext_sizes) sorted.push_back({ext, sz});
            std::sort(sorted.begin(), sorted.end(),
                      [](auto& a, auto& b) { return a.second > b.second; });

            // 颜色表
            static const COLORREF kColors[] = {
                RGB(37, 99, 235), RGB(239, 68, 68), RGB(34, 197, 94),
                RGB(251, 191, 36), RGB(168, 85, 247), RGB(236, 72, 153),
                RGB(20, 184, 166), RGB(249, 115, 22)
            };
            const int kNumColors = sizeof(kColors) / sizeof(kColors[0]);

            // Slice-and-dice 布局
            double x = 2, y = 2;
            double tw = w - 4, th = h - 4;
            int ci = 0;

            for (const auto& [ext, sz] : sorted) {
                double ratio = static_cast<double>(sz) / static_cast<double>(total);
                double area = tw * th * ratio;
                double bw, bh;

                if (tw > th) { bw = area / th; bh = th; }
                else { bh = area / tw; bw = tw; }

                if (bw < 2 || bh < 2) continue;

                RECT r = {static_cast<int>(x), static_cast<int>(y),
                          static_cast<int>(x + bw), static_cast<int>(y + bh)};

                HBRUSH brush = CreateSolidBrush(kColors[ci % kNumColors]);
                FillRect(hdc, &r, brush);
                DeleteObject(brush);

                // 边框
                FrameRect(hdc, &r, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

                // 标签（只在空间够大时绘制）
                if (bw > 40 && bh > 20) {
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(255, 255, 255));
                    std::wstring label;
                    label.reserve(ext.size() + 32);
                    for (char c : ext) label.push_back(static_cast<wchar_t>(c));
                    label += L" ";
                    label += std::to_wstring(static_cast<int>(ratio * 100));
                    label += L"%";
                    RECT lr = r; lr.left += 4; lr.top += 2;
                    DrawTextW(hdc, label.c_str(), -1, &lr, DT_LEFT | DT_TOP | DT_SINGLELINE);
                }

                // 更新剩余空间
                if (tw > th) { x += bw; tw -= bw; }
                else { y += bh; th -= bh; }
                total -= sz;
                ++ci;
            }
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// === 强制圆角窗口（兼容 Win10/11）===
void ApplyRoundedCorners(HWND window) {
    RECT rc;
    GetWindowRect(window, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    // 16px 圆角，足够明显
    HRGN region = CreateRoundRectRgn(0, 0, w + 1, h + 1, 16, 16);
    SetWindowRgn(window, region, TRUE);
    // 注意：SetWindowRgn 在使用后 region 由系统管理，不要 DeleteObject
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        auto* state = reinterpret_cast<GuiState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    GuiState* state = get_state(window);

    switch (message) {
    case WM_CREATE: {
        state->window_brush = CreateSolidBrush(kBgColor);
        state->card_brush = CreateSolidBrush(kCardColor);
        state->input_brush = CreateSolidBrush(kInputBg);
        state->title_font = CreateFontW(
            24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        state->section_font = CreateFontW(
            16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

        state->title_label = CreateWindowW(
            L"STATIC", L"C++ 多线程文件扫描器", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(IDC_TITLE_LABEL), nullptr, nullptr);
        state->path_label = CreateWindowW(
            L"STATIC", L"扫描目录:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(IDC_PATH_LABEL), nullptr, nullptr);
        state->path_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(IDC_PATH_EDIT), nullptr, nullptr);
        state->browse_button = CreateWindowW(
            L"BUTTON", L"选择目录", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(IDC_BROWSE_BUTTON), nullptr, nullptr);
        state->worker_label = CreateWindowW(
            L"STATIC", L"线程数:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(IDC_WORKER_LABEL), nullptr, nullptr);
        state->worker_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"4", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(IDC_WORKER_EDIT), nullptr, nullptr);
        state->extension_filter_label = CreateWindowW(
            L"STATIC", L"扩展名:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(IDC_EXTENSION_FILTER_LABEL), nullptr, nullptr);
        state->extension_filter_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(IDC_EXTENSION_FILTER_EDIT), nullptr, nullptr);
        state->min_size_label = CreateWindowW(
            L"STATIC", L"最小字节:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(IDC_MIN_SIZE_LABEL), nullptr, nullptr);
        state->min_size_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(IDC_MIN_SIZE_EDIT), nullptr, nullptr);
        state->max_size_label = CreateWindowW(
            L"STATIC", L"最大字节:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(IDC_MAX_SIZE_LABEL), nullptr, nullptr);
        state->max_size_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(IDC_MAX_SIZE_EDIT), nullptr, nullptr);
        state->start_button = CreateWindowW(
            L"BUTTON", L"开始扫描", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(IDC_START_BUTTON), nullptr, nullptr);
        state->status_label = CreateWindowW(
            L"STATIC", L"准备就绪", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(IDC_STATUS_LABEL), nullptr, nullptr);
        state->progress_bar = CreateWindowExW(
            0,
            PROGRESS_CLASSW,
            nullptr,
            WS_CHILD | WS_VISIBLE | PBS_MARQUEE | PBS_SMOOTH,
            0,
            0,
            0,
            0,
            window,
            reinterpret_cast<HMENU>(IDC_PROGRESS_BAR),
            nullptr,
            nullptr);
        state->result_label = CreateWindowW(
            L"STATIC", L"扫描摘要", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(IDC_RESULT_LABEL), nullptr, nullptr);
        state->result_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"扫描结果将在这里显示。",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(IDC_RESULT_EDIT), nullptr, nullptr);
        state->extension_list = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_LISTVIEWW,
            nullptr,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0,
            0,
            0,
            0,
            window,
            reinterpret_cast<HMENU>(IDC_EXTENSION_LIST),
            nullptr,
            nullptr);

        ListView_SetUnicodeFormat(state->extension_list, TRUE);
        ListView_SetExtendedListViewStyle(
            state->extension_list,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        column.fmt = LVCFMT_LEFT;
        column.cx = 150;
        column.pszText = const_cast<LPWSTR>(L"扩展名");
        ListView_InsertColumn(state->extension_list, 0, &column);
        column.cx = 90;
        column.pszText = const_cast<LPWSTR>(L"文件数");
        column.fmt = LVCFMT_RIGHT;
        ListView_InsertColumn(state->extension_list, 1, &column);
        column.cx = 90;
        column.pszText = const_cast<LPWSTR>(L"占比");
        ListView_InsertColumn(state->extension_list, 2, &column);

        SendMessageW(state->progress_bar, PBM_SETMARQUEE, FALSE, 0);
        SendMessageW(state->progress_bar, PBM_SETBKCOLOR, 0,
                     static_cast<LPARAM>(kProgressTrack));
        SendMessageW(state->progress_bar, PBM_SETBARCOLOR, 0,
                     static_cast<LPARAM>(kProgressFill));
        SendMessageW(state->title_label, WM_SETFONT, reinterpret_cast<WPARAM>(state->title_font), TRUE);
        SendMessageW(state->result_label, WM_SETFONT, reinterpret_cast<WPARAM>(state->section_font), TRUE);
        set_control_font(state->path_label);
        set_control_font(state->worker_label);
        set_control_font(state->path_edit);
        set_control_font(state->browse_button);
        set_control_font(state->worker_edit);
        set_control_font(state->extension_filter_label);
        set_control_font(state->extension_filter_edit);
        set_control_font(state->min_size_label);
        set_control_font(state->min_size_edit);
        set_control_font(state->max_size_label);
        set_control_font(state->max_size_edit);
        set_control_font(state->start_button);
        set_control_font(state->status_label);
        set_control_font(state->progress_bar);
        set_control_font(state->result_edit);
        set_control_font(state->extension_list);

        // 创建 Treemap 面板
        state->treemap = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"TreemapPanel", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(IDC_TREEMAP),
            GetModuleHandleW(nullptr), nullptr);

        std::wstring current_path(MAX_PATH, L'\0');
        const DWORD length = GetCurrentDirectoryW(
            static_cast<DWORD>(current_path.size()), current_path.data());
        if (length > 0 && length < current_path.size()) {
            current_path.resize(length);
            SetWindowTextW(state->path_edit, current_path.c_str());
        }

        layout_controls(window, state);

        // 强制圆角窗口（Win10+ 通用方案，不依赖 Win11 DWM）
        ApplyRoundedCorners(window);

        // 启用 DWM 圆角（Windows 11 风格，更平滑）
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &corner, sizeof(corner));
        // 启用窗口阴影
        BOOL enable = TRUE;
        DwmSetWindowAttribute(window, DWMWA_NCRENDERING_ENABLED,
                              &enable, sizeof(enable));
        return 0;
    }

    case WM_SIZE:
        if (state) {
            layout_controls(window, state);
            ApplyRoundedCorners(window);
        }
        return 0;

    case WM_ERASEBKGND:
        if (state && state->window_brush) {
            RECT client{};
            GetClientRect(window, &client);
            FillRect(reinterpret_cast<HDC>(w_param), &client, state->window_brush);
            return 1;
        }
        break;

    case WM_CTLCOLORSTATIC:
        if (state) {
            HDC dc = reinterpret_cast<HDC>(w_param);
            HWND control = reinterpret_cast<HWND>(l_param);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, control == state->title_label ? kTitleColor : kTextColor);
            if (control == state->status_label) {
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, kCardColor);
                return reinterpret_cast<LRESULT>(state->card_brush);
            }
            return reinterpret_cast<LRESULT>(state->window_brush);
        }
        break;

    case WM_CTLCOLOREDIT:
        if (state) {
            HDC dc = reinterpret_cast<HDC>(w_param);
            SetTextColor(dc, kTextColor);
            SetBkColor(dc, kInputBg);
            return reinterpret_cast<LRESULT>(state->input_brush);
        }
        break;

    case WM_DRAWITEM:
        if (state && (w_param == IDC_BROWSE_BUTTON || w_param == IDC_START_BUTTON)) {
            draw_button(reinterpret_cast<const DRAWITEMSTRUCT*>(l_param));
            return TRUE;
        }
        break;

    case WM_NOTIFY:
        if (state && reinterpret_cast<NMHDR*>(l_param)->hwndFrom == state->extension_list &&
            reinterpret_cast<NMHDR*>(l_param)->code == NM_CUSTOMDRAW) {
            auto* custom_draw = reinterpret_cast<NMLVCUSTOMDRAW*>(l_param);
            if (custom_draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                return CDRF_NOTIFYITEMDRAW;
            }
            if (custom_draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                custom_draw->clrText = kTextColor;
                custom_draw->clrTextBk = (custom_draw->nmcd.dwItemSpec % 2 == 0)
                                             ? kCardColor
                                             : RGB(248, 248, 252);
                return CDRF_NEWFONT;
            }
        }
        break;

    case WM_COMMAND:
        if (!state || HIWORD(w_param) != BN_CLICKED) {
            break;
        }
        if (LOWORD(w_param) == IDC_BROWSE_BUTTON) {
            browse_for_directory(window, state);
            return 0;
        }
        if (LOWORD(w_param) == IDC_START_BUTTON) {
            start_scan(window, state);
            return 0;
        }
        break;

    case WM_TIMER:
        if (state && w_param == kProgressTimer) {
            const auto discovered = state->discovered_files.load(std::memory_order_relaxed);
            set_status(state, L"正在扫描，已提交任务: " + std::to_wstring(discovered));
            return 0;
        }
        break;

    case WM_SCAN_COMPLETE:
        if (state) {
            handle_scan_complete(window, state, reinterpret_cast<ScanMessage*>(l_param));
            return 0;
        }
        delete reinterpret_cast<ScanMessage*>(l_param);
        return 0;

    case WM_DESTROY:
        if (state) {
            state->closing.store(true, std::memory_order_relaxed);
            state->cancel.store(true, std::memory_order_relaxed);
            KillTimer(window, kProgressTimer);
            if (state->worker.joinable()) {
                state->worker.join();
            }
            DeleteObject(state->window_brush);
            DeleteObject(state->card_brush);
            DeleteObject(state->input_brush);
            DeleteObject(state->title_font);
            DeleteObject(state->section_font);
            delete state;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&common_controls);

    WNDCLASSW window_class{};
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.lpszClassName = kWindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&window_class)) {
        CoUninitialize();
        return 1;
    }

    // 注册 Treemap 窗口类
    WNDCLASSW tm_class{};
    tm_class.hInstance = instance;
    tm_class.lpfnWndProc = treemap_proc;
    tm_class.lpszClassName = L"TreemapPanel";
    tm_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    tm_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&tm_class);

    auto* state = new GuiState();
    HWND window = CreateWindowExW(
        0,
        kWindowClassName,
        L"C++ 多线程文件扫描器",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance,
        state);

    if (!window) {
        delete state;
        CoUninitialize();
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CoUninitialize();
    return static_cast<int>(message.wParam);
}
