# Murmur Firmware

M5Stack AtomS3R + Atomic EchoBase で動作する、OpenAI Realtime API を使った音声対話ファームウェアです。

## 必要なデバイス

- [M5Stack AtomS3R](https://docs.m5stack.com/en/core/AtomS3R)
- [M5Stack Atomic EchoBase](https://docs.m5stack.com/en/atom/Atomic%20EchoBase)

## 環境構築

[ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/) (v5.5) のインストールが必要です。

## ビルド・書き込み

```bash
# サブモジュールの取得
git submodule update --init --recursive

# 環境変数を設定
export WIFI_SSID="your_ssid"
export WIFI_PASSWORD="your_password"
export OPENAI_API_KEY="sk-xxx"

# ESP-IDF 環境を有効化
. ~/esp/esp-idf/export.sh

# ターゲット設定（初回のみ）
idf.py set-target esp32s3

# ボード選択（初回のみ）
# M5 AtomS3R を選択して保存
idf.py menuconfig

# ビルド＆書き込み
idf.py flash monitor
```
