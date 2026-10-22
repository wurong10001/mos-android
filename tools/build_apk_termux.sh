#!/usr/bin/env bash
# 在 arm64 Termux 上手工组装并签名 APK (不经过 Gradle)
#
# 工具链 (全部 arm64/bionic 兼容):
#   aapt2       ~/rendiix/prebuilt-binary/arm64/aapt2   (rendiix/termux-aapt)
#   android.jar ~/android-sdk/platforms/android-11/android.jar
#   apksigner   ~/apktools/lib/apksigner.jar            (纯 Java)
#
# 用法: bash tools/build_apk_termux.sh [libmosandroid.so路径]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SO="${1:-$ROOT/app/src/main/cpp/libmosandroid.so}"
PKG="org.wurong.mos"

AAPT2="${AAPT2:-$HOME/rendiix/prebuilt-binary/arm64/aapt2}"
ANDROID_JAR="${ANDROID_JAR:-$HOME/android-sdk/platforms/android-11/android.jar}"
APKSIGNER="${APKSIGNER:-$HOME/apktools/lib/apksigner.jar}"
KEYSTORE="${KEYSTORE:-$HOME/.android/debug.keystore}"

OUT="$ROOT/mos-debug.apk"
UNSIGNED="$ROOT/.apk-unsigned.apk"
STAGING="$ROOT/.apk-staging"

for t in "$AAPT2" "$ANDROID_JAR" "$APKSIGNER" "$SO"; do
    [ -e "$t" ] || { echo "missing: $t" >&2; exit 1; }
done

# 1) 生成带 package 属性的临时清单 (旧版 aapt2 不支持 --rename-manifest-package)
mkdir -p "$STAGING"
sed 's|<manifest xmlns:android="http://schemas.android.com/apk/res/android">|<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="'"$PKG"'">|' \
    "$ROOT/app/src/main/AndroidManifest.xml" > "$STAGING/AndroidManifest.xml"

# 2) aapt2 link: 编译清单 + 打包 assets + 生成基础 APK
"$AAPT2" link -o "$UNSIGNED" \
    --manifest "$STAGING/AndroidManifest.xml" \
    -I "$ANDROID_JAR" \
    -A "$ROOT/app/src/main/assets" \
    --min-sdk-version 24 \
    --target-sdk-version 35 \
    --version-code 1 \
    --version-name 1.0
echo "aapt2 link done"

# 3) 把 native 库加进 APK (extractNativeLibs=true, 压缩存储即可)
python3 - "$UNSIGNED" "$SO" <<'PYEOF'
import sys, zipfile
apk, so = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(apk, "a", zipfile.ZIP_DEFLATED) as z:
    z.write(so, "lib/arm64-v8a/libmosandroid.so")
print("added lib/arm64-v8a/libmosandroid.so")
PYEOF

# 4) 生成 debug 签名密钥 (首次)
if [ ! -f "$KEYSTORE" ]; then
    mkdir -p "$(dirname "$KEYSTORE")"
    keytool -genkeypair -v -keystore "$KEYSTORE" -alias androiddebugkey \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -storepass android -keypass android \
        -dname "CN=Android Debug,O=Android,C=US" 2>/dev/null
    echo "keystore created: $KEYSTORE"
fi

# 5) 签名 (v1+v2)
java -jar "$APKSIGNER" sign \
    --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android \
    --out "$OUT" "$UNSIGNED"
rm -f "$UNSIGNED"
rm -rf "$STAGING"
echo "signed: $OUT"

# 6) 验证
java -jar "$APKSIGNER" verify --print-certs "$OUT" | head -5
echo "--- badging ---"
"$AAPT2" dump badging "$OUT" | head -10
ls -la "$OUT"
