//----------------------------------------------------------------------------------
//	TP_CoordAlign  ―  AviUtl ExEdit2 用 汎用プラグイン (.aux2)
//
//	After Effects の「整列」パネル風に、複数選択したオブジェクトの
//	X / Y / Z 座標（中心座標）を揃える・画面中央へ配置する・均等配置する。
//	さらに、指定したピクセル間隔で並べ直すことも出来る。
//
//	方式: 選択オブジェクトを列挙し、各オブジェクトの標準描画 X/Y/Z を
//	      「現在値との差分（オフセット）」で書き換える。差分方式のため、
//	      キーフレーム（中間点）を持つオブジェクトの動きを保ったまま平行移動する。
//
//	外観: 配色・フォントは style.conf から取得して本体UIに合わせる。
//	      装飾（角丸・グラデーション・影）は一切用いず、直角・単色で描画する。
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
#include <cwchar>

#include "plugin2.h"
#include "logger2.h"
#include "config2.h"

#pragma comment(lib, "comctl32.lib")

//----------------------------------------------------------------------------------
//	グローバル
//----------------------------------------------------------------------------------
static EDIT_HANDLE*   g_edit   = nullptr;
static LOG_HANDLE*    g_logger = nullptr;
static CONFIG_HANDLE* g_config = nullptr;

// AviUtl2 の UI に表示される名前（ウィンドウメニュー等）
#define PluginName      L"TP_CoordAlign"
// ウィンドウクラス名（内部登録用。UI には出ない）
#define PanelClassName  L"TP_CoordAlignWindowClient"

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
	OP_SPACING,    // 指定px間隔で配置（最小座標を固定）
};

// ボタン → 操作 の対応
struct Cmd {
	int id;
	int axis;   // 0=X,1=Y,2=Z （OP_CENTER_ALL では未使用）
	int op;     // Op
};
static std::vector<Cmd> g_cmds;
static int g_next_id = 1001;

// 編集セクションのコールバックへ渡すパラメータ
struct OpParam {
	int    axis;
	int    op;
	double step;   // OP_SPACING でのみ使用（px）
};

//----------------------------------------------------------------------------------
//	ログ補助
//----------------------------------------------------------------------------------
static void logw(LPCWSTR msg) {
	if (g_logger) g_logger->log(g_logger, msg);
}

//----------------------------------------------------------------------------------
//	外観（style.conf から取得）
//----------------------------------------------------------------------------------
struct Theme {
	COLORREF back, text, btn, btnHover, btnPress, border, editBack;
	HBRUSH   brBack, brBtn, brBtnHover, brBtnPress, brBorder, brEdit;
};
static Theme g_th = {};
static HFONT g_font = nullptr;
static int   g_bh   = 22;      // 1行（ボタン）の高さ
static HWND  g_hStep = nullptr; // 間隔(px) 入力欄
static HWND  g_hotBtn = nullptr;// マウスが乗っているボタン

// style.conf の色コード(0xRRGGBB / 0xRRGGBBAA)を COLORREF(0x00BBGGRR) へ変換
static COLORREF conf_color(LPCSTR key, COLORREF fallback) {
	if (!g_config) return fallback;
	unsigned u = (unsigned)g_config->get_color_code(g_config, key);
	if (u == 0) return fallback;          // 未定義
	if (u > 0xFFFFFFu) u >>= 8;           // RRGGBBAA -> RRGGBB
	return RGB((u >> 16) & 0xFF, (u >> 8) & 0xFF, u & 0xFF);
}

static void init_theme() {
	g_th.back     = conf_color("Background",      RGB(0x20, 0x20, 0x20));
	g_th.text     = conf_color("Text",            RGB(0xFF, 0xFF, 0xFF));
	g_th.btn      = conf_color("ButtonBody",      RGB(0x60, 0x60, 0x60));
	g_th.btnHover = conf_color("ButtonBodyHover", RGB(0x80, 0x80, 0x80));
	g_th.btnPress = conf_color("ButtonBodyPress", RGB(0xA0, 0xA0, 0xA0));
	g_th.border   = conf_color("Border",          RGB(0x90, 0x90, 0x90));
	g_th.editBack = conf_color("Grouping",        RGB(0x38, 0x38, 0x38));

	g_th.brBack     = CreateSolidBrush(g_th.back);
	g_th.brBtn      = CreateSolidBrush(g_th.btn);
	g_th.brBtnHover = CreateSolidBrush(g_th.btnHover);
	g_th.brBtnPress = CreateSolidBrush(g_th.btnPress);
	g_th.brBorder   = CreateSolidBrush(g_th.border);
	g_th.brEdit     = CreateSolidBrush(g_th.editBack);
}

// 本体UIのフォント（style.conf の [Font] Control）を生成する
static void init_font() {
	std::wstring face = L"Yu Gothic UI";
	int size = 13;
	if (g_config) {
		FONT_INFO* fi = g_config->get_font_info(g_config, "Control");
		if (fi) {                                   // ※次の呼び出しまでしか有効でないので即コピー
			if (fi->name && *fi->name) face = fi->name;
			if (fi->size > 0.0f)       size = (int)(fi->size + 0.5f);
		}
	}
	g_font = CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face.c_str());
}

// 現在のフォントでの文字列の描画サイズを実測する
static SIZE measure(LPCWSTR text) {
	SIZE sz = { 0, 0 };
	HDC dc = GetDC(NULL);
	HGDIOBJ old = SelectObject(dc, g_font ? (HGDIOBJ)g_font : GetStockObject(DEFAULT_GUI_FONT));
	GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
	SelectObject(dc, old);
	ReleaseDC(NULL, dc);
	return sz;
}

static const int PADX = 16;   // ボタンの左右余白
static int text_w(LPCWSTR s) { return (int)measure(s).cx; }
static int text_h(LPCWSTR s) { return (int)measure(s).cy; }
static int btn_w(LPCWSTR s)  { return text_w(s) + PADX; }

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

// 1軸に対する整列処理
//	step : OP_SPACING でのみ使用する間隔(px)
static void process_axis(EDIT_SECTION* edit, int axis, int op, int frame, double step) {
	std::vector<AlignItem> items = collect(edit, axis, frame);
	if (items.empty()) return;

	double mn = items[0].cur, mx = items[0].cur;
	for (const auto& it : items) { mn = std::min(mn, it.cur); mx = std::max(mx, it.cur); }

	// 並べ替えを伴う操作（分配・指定間隔）
	if (op == OP_DISTRIBUTE || op == OP_SPACING) {
		std::sort(items.begin(), items.end(),
			[](const AlignItem& a, const AlignItem& b) { return a.cur < b.cur; });
		const int m = (int)items.size();

		if (op == OP_SPACING) {
			if (m < 2) return;
			const double base = items[0].cur;          // 最小座標のオブジェクトを固定
			for (int i = 0; i < m; ++i) {
				double target = base + step * (double)i;
				apply_offset(edit, items[i].obj, AXIS_KEY[axis], target - items[i].cur);
			}
		} else {
			if (m < 3) return;                          // 両端しかなければ動かさない
			const double span = mx - mn;
			for (int i = 0; i < m; ++i) {
				double target = mn + span * (double)i / (double)(m - 1);
				apply_offset(edit, items[i].obj, AXIS_KEY[axis], target - items[i].cur);
			}
		}
		return;
	}

	// 端揃え・中央揃え・画面中央
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

// 編集セクションのコールバック（param = OpParam*）
static void proc_edit(void* param, EDIT_SECTION* edit) {
	const OpParam* p = reinterpret_cast<const OpParam*>(param);
	int frame = (edit->info) ? edit->info->frame : 0;

	if (p->op == OP_CENTER_ALL) {
		for (int a = 0; a < 3; ++a) process_axis(edit, a, OP_SCREEN0, frame, 0.0);
	} else {
		process_axis(edit, p->axis, p->op, frame, p->step);
	}
}

// 間隔(px)入力欄の値を読む（数値として解釈出来なければ0を返す）
static double read_step() {
	if (!g_hStep) return 0.0;
	wchar_t buf[64] = {};
	GetWindowTextW(g_hStep, buf, 63);
	wchar_t* end = nullptr;
	double v = wcstod(buf, &end);
	if (end == buf) return 0.0;
	return v;
}

// ボタン押下時のディスパッチ
static void dispatch(int id) {
	if (!g_edit) return;
	for (const auto& c : g_cmds) {
		if (c.id != id) continue;

		OpParam p{ c.axis, c.op, 0.0 };
		if (c.op == OP_SPACING) {
			p.step = read_step();
			if (std::fabs(p.step) < 1e-9) {   // 未入力・数値でない・0 は誤操作防止のため何もしない
				logw(L"[TP_CoordAlign] 「間隔(px)」に0以外の数値を入力してください");
				return;
			}
		}
		if (!g_edit->call_edit_section_param(&p, proc_edit)) {
			logw(L"[TP_CoordAlign] 編集できません（出力中などの可能性）");
		}
		return;
	}
}

//----------------------------------------------------------------------------------
//	UI 構築
//----------------------------------------------------------------------------------

// ボタンのホバー状態を取るためのサブクラス
static LRESULT CALLBACK btn_subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR id, DWORD_PTR) {
	switch (msg) {
		case WM_MOUSEMOVE:
			if (g_hotBtn != h) {
				HWND prev = g_hotBtn;
				g_hotBtn = h;
				if (prev) InvalidateRect(prev, nullptr, TRUE);
				InvalidateRect(h, nullptr, TRUE);
				TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, h, 0 };
				TrackMouseEvent(&tme);
			}
			break;
		case WM_MOUSELEAVE:
			if (g_hotBtn == h) {
				g_hotBtn = nullptr;
				InvalidateRect(h, nullptr, TRUE);
			}
			break;
		case WM_NCDESTROY:
			RemoveWindowSubclass(h, btn_subclass, id);
			break;
	}
	return DefSubclassProc(h, msg, wp, lp);
}

static void make_label(HWND parent, int x, int y, int w, LPCWSTR text) {
	HWND s = CreateWindowExW(0, L"STATIC", text, WS_VISIBLE | WS_CHILD | SS_LEFT,
		x, y, w, g_bh, parent, nullptr, GetModuleHandle(0), nullptr);
	if (s) SendMessageW(s, WM_SETFONT, (WPARAM)g_font, TRUE);
}

// テキスト幅に合わせた見出しラベル
static int make_head(HWND parent, int x, int y, LPCWSTR text) {
	int w = text_w(text) + 4;
	make_label(parent, x, y, w, text);
	return x + w;
}

static void make_button(HWND parent, int x, int y, int w, LPCWSTR label, int axis, int op) {
	int id = g_next_id++;
	HWND b = CreateWindowExW(0, WC_BUTTON, label,
		WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
		x, y, w, g_bh, parent, (HMENU)(INT_PTR)id, GetModuleHandle(0), nullptr);
	if (b) {
		SendMessageW(b, WM_SETFONT, (WPARAM)g_font, TRUE);
		SetWindowSubclass(b, btn_subclass, (UINT_PTR)id, 0);
	}
	g_cmds.push_back({ id, axis, op });
}

// 横一列にボタンを並べ、右端の x を返す
static int make_row(HWND parent, int x, int y, int gap,
                    const LPCWSTR* labels, const int* axes, const int* ops, int n) {
	for (int i = 0; i < n; ++i) {
		make_button(parent, x, y, btn_w(labels[i]), labels[i], axes[i], ops[i]);
		x += btn_w(labels[i]) + gap;
	}
	return x - gap;
}

static void build_ui(HWND hwnd) {
	// 行の高さ = 本体の設定項目高さ と 実測文字高 の大きい方
	int itemH = 22;
	if (g_config) {
		int h = g_config->get_layout_size(g_config, "SettingItemHeight");
		if (h > 0) itemH = h;
	}
	g_bh = std::max(itemH, text_h(L"Ag") + 6);

	const int MX = 10, MY = 8, GAP = 4;
	const int rowh = g_bh + GAP;
	int y = MY;
	int right = 0;
	auto note = [&](int r) { right = std::max(right, r); };

	// ── A. 整列（選択内で揃える）──────────────────────────
	note(make_head(hwnd, MX, y, L"整列（選択内で揃える）"));
	y += rowh;

	LPCWSTR grid[3][3] = {
		{ L"左",   L"中央", L"右" },
		{ L"上",   L"中央", L"下" },
		{ L"手前", L"中央", L"奥" },
	};
	const int rowop[3]      = { OP_MIN, OP_CENTER_SEL, OP_MAX };
	LPCWSTR   axisLbl[3]    = { L"X", L"Y", L"Z" };

	// 列幅は同じ列の全ラベルの実測値の最大に揃える
	int colw[3] = { 0, 0, 0 };
	for (int c = 0; c < 3; ++c)
		for (int r = 0; r < 3; ++r)
			colw[c] = std::max(colw[c], btn_w(grid[r][c]));

	int lblw = 0;
	for (int i = 0; i < 3; ++i) lblw = std::max(lblw, text_w(axisLbl[i]));
	lblw += 8;
	const int col0 = MX + lblw + GAP;

	for (int r = 0; r < 3; ++r) {
		make_label(hwnd, MX, y, lblw, axisLbl[r]);
		int x = col0;
		for (int c = 0; c < 3; ++c) {
			make_button(hwnd, x, y, colw[c], grid[r][c], r, rowop[c]);
			x += colw[c] + GAP;
		}
		note(x - GAP);
		y += rowh;
	}

	// ── B. 画面中央へ ──────────────────────────────────
	y += GAP;
	note(make_head(hwnd, MX, y, L"画面中央へ"));
	y += rowh;
	{
		LPCWSTR lbl[4] = { L"X=0", L"Y=0", L"Z=0", L"完全中央" };
		const int ax[4] = { 0, 1, 2, 0 };
		const int op[4] = { OP_SCREEN0, OP_SCREEN0, OP_SCREEN0, OP_CENTER_ALL };
		note(make_row(hwnd, MX, y, GAP, lbl, ax, op, 4));
		y += rowh;
	}

	// ── C. 均等配置（両端基準）─────────────────────────
	y += GAP;
	note(make_head(hwnd, MX, y, L"均等配置（両端を基準に等間隔）"));
	y += rowh;
	{
		LPCWSTR lbl[3] = { L"X分配", L"Y分配", L"Z分配" };
		const int ax[3] = { 0, 1, 2 };
		const int op[3] = { OP_DISTRIBUTE, OP_DISTRIBUTE, OP_DISTRIBUTE };
		note(make_row(hwnd, MX, y, GAP, lbl, ax, op, 3));
		y += rowh;
	}

	// ── D. 指定間隔で配置（最小座標を固定）──────────────
	y += GAP;
	note(make_head(hwnd, MX, y, L"指定間隔で配置（最小座標を固定）"));
	y += rowh;
	{
		LPCWSTR cap = L"間隔(px)";
		int capw = text_w(cap) + 6;
		make_label(hwnd, MX, y, capw, cap);
		int ew = std::max(60, text_w(L"-00000.00") + 12);
		g_hStep = CreateWindowExW(0, L"EDIT", L"100",
			WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
			MX + capw + GAP, y, ew, g_bh, hwnd, nullptr, GetModuleHandle(0), nullptr);
		if (g_hStep) SendMessageW(g_hStep, WM_SETFONT, (WPARAM)g_font, TRUE);
		note(MX + capw + GAP + ew);
		y += rowh;

		LPCWSTR lbl[3] = { L"X間隔", L"Y間隔", L"Z間隔" };
		const int ax[3] = { 0, 1, 2 };
		const int op[3] = { OP_SPACING, OP_SPACING, OP_SPACING };
		note(make_row(hwnd, MX, y, GAP, lbl, ax, op, 3));
		y += rowh;
	}

	// 内容が収まるサイズにウィンドウを合わせる
	RECT rc = { 0, 0, right + MX, y + MY };
	AdjustWindowRectEx(&rc, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE, 0);
	SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

//----------------------------------------------------------------------------------
//	ウィンドウプロシージャ
//----------------------------------------------------------------------------------
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_COMMAND:
			if (HIWORD(wp) == BN_CLICKED) {
				dispatch(LOWORD(wp));
				SetFocus(NULL);   // ボタンのフォーカスを外す（本体のショートカットを妨げない）
				return 0;
			}
			break;

		// ラベル: 背景は親と同色、文字は本体のテキスト色
		case WM_CTLCOLORSTATIC:
			SetTextColor((HDC)wp, g_th.text);
			SetBkMode((HDC)wp, TRANSPARENT);
			return (LRESULT)g_th.brBack;

		// 入力欄
		case WM_CTLCOLOREDIT:
			SetTextColor((HDC)wp, g_th.text);
			SetBkColor((HDC)wp, g_th.editBack);
			return (LRESULT)g_th.brEdit;

		// ボタンの自前描画（直角・単色。角丸やグラデーションは用いない）
		case WM_DRAWITEM: {
			DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
			if (!di || di->CtlType != ODT_BUTTON) break;

			const bool pressed = (di->itemState & ODS_SELECTED) != 0;
			const bool hot     = (di->hwndItem == g_hotBtn);
			HBRUSH body = pressed ? g_th.brBtnPress : (hot ? g_th.brBtnHover : g_th.brBtn);

			FillRect(di->hDC, &di->rcItem, body);            // 地
			FrameRect(di->hDC, &di->rcItem, g_th.brBorder);  // 1px の直角枠

			wchar_t buf[128] = {};
			GetWindowTextW(di->hwndItem, buf, 127);
			SetBkMode(di->hDC, TRANSPARENT);
			SetTextColor(di->hDC, g_th.text);
			HGDIOBJ old = SelectObject(di->hDC, g_font ? (HGDIOBJ)g_font
			                                          : GetStockObject(DEFAULT_GUI_FONT));
			DrawTextW(di->hDC, buf, -1, &di->rcItem,
				DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			SelectObject(di->hDC, old);
			return TRUE;
		}
	}
	return DefWindowProc(hwnd, msg, wp, lp);
}

//----------------------------------------------------------------------------------
//	汎用プラグイン エクスポート
//----------------------------------------------------------------------------------
static COMMON_PLUGIN_TABLE common_plugin_table = {
	PluginName,
	L"TP_CoordAlign version 1.11 ― 複数オブジェクトのX/Y/Z整列・分配・指定間隔配置",
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
	if (g_font) { DeleteObject(g_font); g_font = nullptr; }
	HBRUSH* brs[] = { &g_th.brBack, &g_th.brBtn, &g_th.brBtnHover,
	                  &g_th.brBtnPress, &g_th.brBorder, &g_th.brEdit };
	for (HBRUSH* b : brs) {
		if (*b) { DeleteObject(*b); *b = nullptr; }
	}
}

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	init_theme();
	init_font();

	WNDCLASSEXW wc = {};
	wc.cbSize        = sizeof(WNDCLASSEXW);
	wc.lpszClassName = PanelClassName;
	wc.lpfnWndProc   = wnd_proc;
	wc.hInstance     = GetModuleHandle(0);
	wc.hbrBackground = g_th.brBack;
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	if (!RegisterClassExW(&wc)) return;

	HWND hwnd = CreateWindowExW(
		0, PanelClassName, PanelClassName,
		WS_POPUP,   // 親未指定のため一旦 WS_POPUP（登録時に WS_CHILD 化される）
		CW_USEDEFAULT, CW_USEDEFAULT, 320, 360,
		nullptr, nullptr, GetModuleHandle(0), nullptr);
	if (!hwnd) return;

	build_ui(hwnd);

	host->register_window_client(PluginName, hwnd);
	g_edit = host->create_edit_handle();
}
