# TP_CoordAlign ― 座標整列パネル（AviUtl2 汎用プラグイン）

After Effects の「整列」パネルのように、**複数選択したオブジェクト**の
X / Y / Z 座標（中心座標）を、揃える・画面中央へ配置する・均等配置するプラグイン。

対象: **AviUtl ExEdit2（AviUtl2）**。旧 AviUtl では動作しない。

> **タイムライン方向の整列ではありません。**
> AviUtl2 には標準で「オブジェクトの開始点を揃える」「オブジェクトを均等配置」といった
> **フレーム方向**の整列機能が右クリックメニューにあります。
> 本プラグインが扱うのは、それとは別の**画面上の座標 (X/Y/Z)** の整列です。

## できること

専用パネルウィンドウのボタンで操作する。すべて **中心座標ベース**。

- **整列（選択内で揃える）**
  - X: 左（最小X）/ 中央（選択群の中点）/ 右（最大X）
  - Y: 上（最小Y）/ 中央 / 下（最大Y）
  - Z: 手前（最小Z）/ 中央 / 奥（最大Z）
- **画面中央へ**: `X=0` / `Y=0` / `Z=0` / `完全中央`（X=Y=Z=0）
- **均等配置（両端を基準に等間隔）**: X分配 / Y分配 / Z分配
  （その軸で最小・最大のオブジェクトを両端に固定し、中間を等間隔化）
- **診断ダンプ**: 先頭選択オブジェクトのエイリアスをログに出力（不具合調査用）

キーフレーム（中間点）を持つオブジェクトは、**差分移動**により動き（経路）を
保ったまま平行移動する。1 回の操作はまとめて 1 回の Undo（Ctrl+Z）で戻せる。

## インストール

1. [Releases](../../releases) から `TP_CoordAlign.aux2` をダウンロードする。
2. AviUtl2 の **Plugin フォルダ**へコピーする。
   - 例: `C:\ProgramData\aviutl2\Plugin\`
   - もしくは AviUtl2 本体フォルダ内の `Plugin\`
3. AviUtl2 を再起動し、ウィンドウメニュー等から「整列パネル」を表示する。

## 使い方

1. タイムラインでオブジェクトを複数選択する。
2. 整列パネルの目的のボタンを押す。
3. 選択中オブジェクトの標準描画 X/Y/Z が一括で調整される。

- 「標準描画」を持たないオブジェクト（音声など）は自動的に無視される。
- 均等配置は 3 個以上選択している場合に機能する。

## ビルド方法

### 必要環境
- **Visual Studio 2022**（ワークロード「C++ によるデスクトップ開発」）
  → MSVC・Windows SDK・CMake が同時に入る
- ターゲットは **x64**

### 手順
1. このフォルダで `build.bat` を実行（または下記コマンド）。

   ```
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```

2. 生成物 `build\Release\TP_CoordAlign.aux2` を Plugin フォルダへコピーする。

## 仕様・制限

- **中心座標ベース**の整列。AE のようなバウンディングボックス（見た目の端）
  基準ではない。SDK にオブジェクトの描画サイズを取得する API が無いため。
- キーフレーム値の書式は環境により差異があり得る。意図しない結果が出た場合は
  「診断ダンプ」でエイリアスを確認できる（ログ出力）。

## ファイル構成

```
TP_CoordAlign/
├─ src/AlignPanel.cpp          … プラグイン本体
├─ include/aviutl2_sdk/*.h      … SDK ヘッダ（同梱・MIT）
├─ CMakeLists.txt               … ビルド定義（出力: TP_CoordAlign.aux2）
├─ build.bat                    … ワンクリックビルド
├─ LICENSE                      … 本プラグインのライセンス (MIT)
└─ README.md
```

## ライセンス

本プラグインは **MIT License**（[LICENSE](LICENSE)）。

`include/aviutl2_sdk/` に同梱している SDK ヘッダ
（`plugin2.h` / `logger2.h` / `config2.h` / `cache2.h`）は
**AviUtl ExEdit2 Plugin SDK**（MIT License, Copyright (c) 2025 Kenkun）の一部。
詳細は [include/aviutl2_sdk/LICENSE](include/aviutl2_sdk/LICENSE) を参照。
配布元: https://github.com/aviutl2/aviutl2_sdk_mirror
