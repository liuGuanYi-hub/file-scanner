#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
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

#include "scanner/domain/types.h"
#include "scanner/domain/utilities.h"
#include "scanner/infrastructure/file_scanner.h"

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kWindowClassName[] = L"CppFileScannerWindow";
constexpr int kWindowWidth = 780;
constexpr int kWindowHeight = 580;

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
constexpr UINT_PTR kProgressTimer = 1;
constexpr UINT WM_SCAN_COMPLETE = WM_APP + 1;

constexpr COLORREF kWindowColor = RGB(245, 247, 250);
constexpr COLORREF kPanelColor = RGB(232, 240, 254);
constexpr COLORREF kTitleColor = RGB(30, 64, 115);
constexpr COLORREF kTextColor = RGB(31, 41, 55);
constexpr COLORREF kAccentColor = RGB(37, 99, 235);
constexpr COLORREF kButtonTextColor = RGB(255, 255, 255);
constexpr COLORREF kInputColor = RGB(255, 255, 255);

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
    HBRUSH panel_brush = nullptr;
    HBRUSH input_brush = nullptr;
    HFONT title_font = nullptr;
    HFONT section_font = nullptr;

    std::atomic_bool cancel{false};
    std::atomic_bool closing{false};
    std::atomic<std::uint64_t> discovered_files{0};
    std::thread worker;
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

    COLORREF fill = is_start_button ? kAccentColor : kPanelColor;
    if (!enabled) {
        fill = RGB(203, 213, 225);
    } else if (pressed && is_start_button) {
        fill = RGB(29, 78, 216);
    }

    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(draw_item->hDC, &draw_item->rcItem, brush);
    DeleteObject(brush);

    HBRUSH border_brush = CreateSolidBrush(RGB(148, 163, 184));
    FrameRect(draw_item->hDC, &draw_item->rcItem, border_brush);
    DeleteObject(border_brush);
    SetBkMode(draw_item->hDC, TRANSPARENT);
    SetTextColor(draw_item->hDC, is_start_button && enabled ? kButtonTextColor : kTextColor);
    SelectObject(draw_item->hDC, GetStockObject(DEFAULT_GUI_FONT));

    wchar_t text[64]{};
    GetWindowTextW(draw_item->hwndItem, text, _countof(text));
    RECT text_rect = draw_item->rcItem;
    DrawTextW(draw_item->hDC, text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        auto* state = reinterpret_cast<GuiState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    GuiState* state = get_state(window);

    switch (message) {
    case WM_CREATE: {
        state->window_brush = CreateSolidBrush(kWindowColor);
        state->panel_brush = CreateSolidBrush(kPanelColor);
        state->input_brush = CreateSolidBrush(kInputColor);
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

        std::wstring current_path(MAX_PATH, L'\0');
        const DWORD length = GetCurrentDirectoryW(
            static_cast<DWORD>(current_path.size()), current_path.data());
        if (length > 0 && length < current_path.size()) {
            current_path.resize(length);
            SetWindowTextW(state->path_edit, current_path.c_str());
        }

        layout_controls(window, state);
        return 0;
    }

    case WM_SIZE:
        if (state) {
            layout_controls(window, state);
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
                SetBkColor(dc, kPanelColor);
                return reinterpret_cast<LRESULT>(state->panel_brush);
            }
            return reinterpret_cast<LRESULT>(state->window_brush);
        }
        break;

    case WM_CTLCOLOREDIT:
        if (state) {
            HDC dc = reinterpret_cast<HDC>(w_param);
            SetTextColor(dc, kTextColor);
            SetBkColor(dc, kInputColor);
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
                                             ? kInputColor
                                             : RGB(248, 250, 252);
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
            DeleteObject(state->panel_brush);
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
