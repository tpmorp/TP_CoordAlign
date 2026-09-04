//----------------------------------------------------------------------------------
//	整列パネル  ―  AviUtl ExEdit2 用 汎用プラグイン (.aux2)
//
//	After Effects の「整列」パネル風に、複数選択したオブジェクトの
//	X / Y / Z 座標（中心座標）を揃える・画面中央へ配置する・均等配置する。
//
//	方式: 選択オブジェクトを列挙し、各オブジェクトの標準描画 X/Y/Z を
//	      「現在値との差分（オフセット）」で書き換える。差分方式のため、
//	      キーフレーム（中間点）を持つオブジェクトの動きを保ったまま平行移動する。
//----------------------------------------------------------------------------------
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "plugin2.h"
#include "logger2.h"
#include "config2.h"

//----------------------------------------------------------------------------------
//	グローバル
//----------------------------------------------------------------------------------
static EDIT_HANDLE*   g_edit   = nullptr;
static LOG_HANDLE*    g_logger = nullptr;
static CONFIG_HANDLE* g_config = nullptr;

#define PanelClassName L"AlignPanelWindowClient"

// 標準描画エフェクト名と座標項目名（実データで確定済み）
static LPCWSTR const EFFECT_DRAW = L"標準描画";
static LPCWSTR const AXIS_KEY[3] = { L"X", L"Y", L"Z" };

// 操作種別
enum Op {
	OP_MIN,        // 最小に揃える（左/上/手前）
	OP_MAX,        // 最大に揃える（右/下/奥）
	OP_CENTER_SEL, // 選択群の中央（min/max の中点）に揃える
	OP_SCREEN0,    // 画面中央（該当軸=0）へ
	OP_CENTER_ALL, // 完全中央（X=Y=Z=0）へ
	OP_DISTRIBUTE, // 均等配置（両端固定・中間を等間隔）
	OP_DUMP,       // 診断ダンプ（先頭選択オブジェクトのエイリアスをログ出力）
};

// ボタン → 操作 の対応
struct Cmd {
	int id;
	int axis;   // 0=X,1=Y,2=Z  （OP_CENTER_ALL / OP_DUMP では未使用）
	int op;     // Op
};
static std::vector<Cmd> g_cmds;
static int g_next_id = 1001;

//----------------------------------------------------------------------------------
//	ログ補助
//----------------------------------------------------------------------------------
static void logw(LPCWSTR msg) {
	if (g_logger) g_logger->log(g_logger, msg);
}

// UTF-8 文字列を分割しつつワイド変換してログ出力（1024文字制限に配慮）
static void log_utf8_chunked(const char* utf8) {
	if (!g_logger || !utf8) return;
	int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
	if (wlen <= 0) return;
	std::wstring w(wlen, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], wlen);
	if (!w.empty() && w.back() == L'\0') w.pop_back();
	const size_t chunk = 800;
	for (size_t i = 0; i < w.size(); i += chunk) {
		std::wstring part = w.substr(i, chunk);
		g_logger->log(g_logger, part.c_str());
	}
}

//----------------------------------------------------------------------------------
//	数値整形・オフセット適用
//----------------------------------------------------------------------------------

// double を末尾ゼロを整理した文字列にする（例: 12.3400 -> "12.34", 0.0000 -> "0"）
static std::string fmt_num(double v) {
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%.4f", v);
	std::string s(buf);
	size_t dot = s.find('.');
	if (dot != std::string::npos) {
		size_t last = s.find_last_not_of('0');
		if (last == dot) s.erase(dot);      // 小数部が全て0 -> ドットごと削除
		else             s.erase(last + 1);
	}
	if (s == "-0") s = "0";
	return s;
}

static inline std::string trim(const std::string& s) {
	size_t a = s.find_first_not_of(" \t");
	if (a == std::string::npos) return "";
	size_t b = s.find_last_not_of(" \t");
	return s.substr(a, b - a + 1);
}

// トークンが「まるごと数値」なら true、その値を out_val へ
static bool parse_full_number(const std::string& tok, double* out_val) {
	std::string t = trim(tok);
	if (t.empty()) return false;
	const char* c = t.c_str();
	char* end = nullptr;
	double v = std::strtod(c, &end);
	if (end == c) return false;
	while (*end == ' ' || *end == '\t') ++end;
	if (*end != '\0') return false;
	*out_val = v;
	return true;
}

// 設定値文字列（エイリアス書式）中の数値トークン全てに delta を加算した文字列を返す
//  - 単一数値（静止値）      : その値 + delta          … 厳密対応
//  - カンマ区切り（KF等）    : 各数値トークン + delta   … 移動方法名など非数値は温存
static std::string offset_numbers(const std::string& in, double delta) {
	std::string out;
	size_t pos = 0;
	while (true) {
		size_t comma = in.find(',', pos);
		std::string tok = (comma == std::string::npos)
			? in.substr(pos)
			: in.substr(pos, comma - pos);
		double v;
		if (parse_full_number(tok, &v)) out += fmt_num(v + delta);
		else                            out += tok;   // 非数値トークンはそのまま
		if (comma == std::string::npos) break;
		out += ',';
		pos = comma + 1;
	}
	return out;
}

// 1オブジェクトの指定軸座標を delta だけずらす
static void apply_offset(EDIT_SECTION* edit, OBJECT_HANDLE obj, LPCWSTR key, double delta) {
	if (std::fabs(delta) < 1e-9) return;
	LPCSTR cur = edit->get_object_item_value(obj, EFFECT_DRAW, key);
	if (!cur) return;
	std::string next = offset_numbers(std::string(cur), delta);
	edit->set_object_item_value(obj, EFFECT_DRAW, key, next.c_str());
}

//----------------------------------------------------------------------------------
//	整列処理本体（編集セクション内で呼ばれる）
//----------------------------------------------------------------------------------
struct AlignItem {
	OBJECT_HANDLE obj;
	double        cur;   // 現在フレームでの座標値
};

// 選択オブジェクトの指定軸座標を収集する（標準描画X/Y/Zを持たないものは除外）
static std::vector<AlignItem> collect(EDIT_SECTION* edit, int axis, int frame) {
	std::vector<AlignItem> items;
	int n = edit->get_selected_object_num();
	for (int i = 0; i < n; ++i) {
		OBJECT_HANDLE o = edit->get_selected_object(i);
		if (!o) continue;
		double v = 0.0;
		if (!edit->get_object_track_value(o, EFFECT_DRAW, AXIS_KEY[axis], (double)frame, &v))
			continue;   // 標準描画/該当軸を持たないオブジェクトはスキップ
		items.push_back({ o, v });
	}
	return items;
}

// 1軸に対する揃え／画面中央／均等配置
static void process_axis(EDIT_SECTION* edit, int axis, int op, int frame) {
	std::vector<AlignItem> items = collect(edit, axis, frame);
	if (items.empty()) return;

	double mn = items[0].cur, mx = items[0].cur;
	for (const auto& it : items) { mn = std::min(mn, it.cur); mx = std::max(mx, it.cur); }

	if (op == OP_DISTRIBUTE) {
		if (items.size() < 3) return;   // 両端しかなければ動かさない
		std::sort(items.begin(), items.end(),
			[](const AlignItem& a, const AlignItem& b) { return a.cur < b.cur; });
		int m = (int)items.size();
		double span = mx - mn;
		for (int i = 0; i < m; ++i) {
			double target = mn + span * (double)i / (double)(m - 1);
			apply_offset(edit, items[i].obj, AXIS_KEY[axis], target - items[i].cur);
		}
		return;
	}

	for (const auto& it : items) {
		double target;
		switch (op) {
			case OP_MIN:        target = mn;              break;
			case OP_MAX:        target = mx;              break;
			case OP_CENTER_SEL: target = (mn + mx) / 2.0; break;
			case OP_SCREEN0:    target = 0.0;             break;
			default:            target = it.cur;          break;
		}
		apply_offset(edit, it.obj, AXIS_KEY[axis], target - it.cur);
	}
}

// 診断ダンプ: 先頭選択オブジェクトのエイリアス全文をログへ
static void dump_selected(EDIT_SECTION* edit) {
	int n = edit->get_selected_object_num();
	if (n <= 0) { logw(L"[整列パネル] 選択オブジェクトがありません"); return; }
	OBJECT_HANDLE o = edit->get_selected_object(0);
	if (!o) return;
	LPCSTR alias = edit->get_object_alias(o);
	if (!alias) { logw(L"[整列パネル] エイリアス取得に失敗"); return; }
	logw(L"[整列パネル] ---- 先頭選択オブジェクトのエイリアス ----");
	log_utf8_chunked(alias);
	logw(L"[整列パネル] ---- ここまで ----");
}

// 編集セクションのコールバック（param = Cmd*）
static void proc_edit(void* param, EDIT_SECTION* edit) {
	const Cmd* c = reinterpret_cast<const Cmd*>(param);
	int frame = (edit->info) ? edit->info->frame : 0;

	switch (c->op) {
		case OP_DUMP:
			dump_selected(edit);
			break;
		case OP_CENTER_ALL:
			process_axis(edit, 0, OP_SCREEN0, frame);
			process_axis(edit, 1, OP_SCREEN0, frame);
			process_axis(edit, 2, OP_SCREEN0, frame);
			break;
		default:
			process_axis(edit, c->axis, c->op, frame);
			break;
	}
}

// ボタン押下時のディスパッチ
static void dispatch(int id) {
	if (!g_edit) return;
	for (const auto& c : g_cmds) {
		if (c.id != id) continue;
		// 診断ダンプは参照のみなので read section、それ以外は編集 section
		if (c.op == OP_DUMP) {
			g_edit->call_read_section_param(const_cast<Cmd*>(&c), proc_edit);
		} else {
			if (!g_edit->call_edit_section_param(const_cast<Cmd*>(&c), proc_edit)) {
				logw(L"[整列パネル] 編集できません（出力中などの可能性）");
			}
		}
		return;
	}
}

//----------------------------------------------------------------------------------
//	UI 構築
//----------------------------------------------------------------------------------
static int g_bh = 24;   // ボタン高さ（style.conf の SettingItemHeight）

static void make_label(HWND parent, int x, int y, int w, LPCWSTR text) {
	CreateWindowEx(0, L"STATIC", text, WS_VISIBLE | WS_CHILD | SS_LEFT,
		x, y, w, g_bh, parent, nullptr, GetModuleHandle(0), nullptr);
}

static void make_button(HWND parent, int x, int y, int w, LPCWSTR label, int axis, int op) {
	int id = g_next_id++;
	CreateWindowEx(0, WC_BUTTON, label, WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
		x, y, w, g_bh, parent, (HMENU)(INT_PTR)id, GetModuleHandle(0), nullptr);
	g_cmds.push_back({ id, axis, op });
}

static void build_ui(HWND hwnd) {
	if (g_config) {
		int h = g_config->get_layout_size(g_config, "SettingItemHeight");
		if (h > 0) g_bh = h;
	}
	const int MX = 10, GAP = 4;
	const int LBLW = 34;                 // 行頭ラベル幅
	const int BW = 62;                    // ボタン幅
	const int col0 = MX + LBLW + GAP;
	int y = 8;
	const int rowh = g_bh + GAP;

	// A. 揃え（選択内）
	make_label(hwnd, MX, y, LBLW + BW * 3, L"整列（選択内で揃える）"); y += rowh;
	struct { LPCWSTR lbl; int axis; } rows[3][3] = {
		{ { L"左",   0 }, { L"中央", 0 }, { L"右",   0 } },
		{ { L"上",   1 }, { L"中央", 1 }, { L"下",   1 } },
		{ { L"手前", 2 }, { L"中央", 2 }, { L"奥",   2 } },
	};
	int rowop[3] = { OP_MIN, OP_CENTER_SEL, OP_MAX };
	LPCWSTR axislabel[3] = { L"X", L"Y", L"Z" };
	for (int r = 0; r < 3; ++r) {
		make_label(hwnd, MX, y, LBLW, axislabel[r]);
		for (int col = 0; col < 3; ++col)
			make_button(hwnd, col0 + col * (BW + GAP), y, BW,
				rows[r][col].lbl, rows[r][col].axis, rowop[col]);
		y += rowh;
	}

	// B. 画面中央へ
	y += GAP;
	make_label(hwnd, MX, y, 300, L"画面中央へ"); y += rowh;
	{
		int x = MX;
		make_button(hwnd, x, y, BW, L"X=0",     0, OP_SCREEN0);    x += BW + GAP;
		make_button(hwnd, x, y, BW, L"Y=0",     1, OP_SCREEN0);    x += BW + GAP;
		make_button(hwnd, x, y, BW, L"Z=0",     2, OP_SCREEN0);    x += BW + GAP;
		make_button(hwnd, x, y, BW + 12, L"完全中央", 0, OP_CENTER_ALL);
		y += rowh;
	}

	// C. 均等配置（両端基準）
	y += GAP;
	make_label(hwnd, MX, y, 300, L"均等配置（両端を基準に等間隔）"); y += rowh;
	{
		int x = MX;
		make_button(hwnd, x, y, BW, L"X分配", 0, OP_DISTRIBUTE); x += BW + GAP;
		make_button(hwnd, x, y, BW, L"Y分配", 1, OP_DISTRIBUTE); x += BW + GAP;
		make_button(hwnd, x, y, BW, L"Z分配", 2, OP_DISTRIBUTE);
		y += rowh;
	}

	// D. 診断
	y += GAP;
	make_button(hwnd, MX, y, BW * 2, L"診断ダンプ", 0, OP_DUMP);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_COMMAND:
			if (HIWORD(wp) == BN_CLICKED) {
				dispatch(LOWORD(wp));
				SetFocus(NULL);   // ボタンのフォーカスを外す
				return 0;
			}
			break;
	}
	return DefWindowProc(hwnd, msg, wp, lp);
}

//----------------------------------------------------------------------------------
//	汎用プラグイン エクスポート
//----------------------------------------------------------------------------------
static COMMON_PLUGIN_TABLE common_plugin_table = {
	L"整列パネル",
	L"整列パネル version 1.00 ― 複数オブジェクトのX/Y/Z整列・分配 (AE風)",
};

EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable(void) {
	return &common_plugin_table;
}

EXTERN_C __declspec(dllexport) DWORD RequiredVersion() {
	return 2003300;
}

EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle) {
	g_logger = handle;
}

EXTERN_C __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* handle) {
	g_config = handle;
}

EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
	return true;
}

EXTERN_C __declspec(dllexport) void UninitializePlugin() {
}

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	WNDCLASSEXW wc = {};
	wc.cbSize        = sizeof(WNDCLASSEXW);
	wc.lpszClassName = PanelClassName;
	wc.lpfnWndProc   = wnd_proc;
	wc.hInstance     = GetModuleHandle(0);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	if (!RegisterClassExW(&wc)) return;

	HWND hwnd = CreateWindowExW(
		0, PanelClassName, PanelClassName,
		WS_POPUP,   // 親未指定のため一旦 WS_POPUP（登録時に WS_CHILD 化される）
		CW_USEDEFAULT, CW_USEDEFAULT, 320, 360,
		nullptr, nullptr, GetModuleHandle(0), nullptr);
	if (!hwnd) return;

	build_ui(hwnd);

	host->register_window_client(PanelClassName, hwnd);
	g_edit = host->create_edit_handle();
}
