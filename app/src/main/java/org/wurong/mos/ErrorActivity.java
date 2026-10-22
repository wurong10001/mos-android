package org.wurong.mos;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import android.graphics.Typeface;

import java.io.File;
import java.util.Locale;

/**
 * 错误页 (Java 实现, 替代原 Native 光栅化错误页):
 *   - 标题/正文按系统语言本地化 (简体中文 / 繁體中文 / English)
 *   - 由 Native 层通过 Intent 拉起: error_key + details + log/crash 路径
 *   - 两个操作: 重试 (finish 回到 NativeActivity, native 循环重试 wasm)
 *     和 清除崩溃日志并重试 (删除 crash.log 后 finish)
 *   - 沉浸式全屏 (隐藏状态栏 + 导航栏)
 *
 * 对应 native 侧: cpp/java_ui.cpp (ui_show_error)
 */
public class ErrorActivity extends Activity {

    // Intent extras (与 java_ui.cpp 约定)
    public static final String EXTRA_ERROR_KEY = "org.wurong.mos.ERROR_KEY";
    public static final String EXTRA_DETAILS = "org.wurong.mos.DETAILS";
    public static final String EXTRA_LOG_PATH = "org.wurong.mos.LOG_PATH";
    public static final String EXTRA_CRASH_PATH = "org.wurong.mos.CRASH_PATH";

    private static final int BG_COLOR = 0xFF101418;
    private static final int TITLE_COLOR = 0xFFFF4D4D;
    private static final int TEXT_COLOR = 0xFFE6E6E6;
    private static final int HINT_COLOR = 0xFFA0A4A8;

    // 换行常量 (避免源码里的转义序列)
    private static final String NL = String.valueOf((char) 10);

    // 完整错误文本 (标题 + 正文), 供复制按钮使用
    private String mFullText = "";

    // ---- 本地化 (与 strings.cpp 相同逻辑) ----

    private static boolean isChinese() {
        return "zh".equals(Locale.getDefault().getLanguage());
    }

    private static boolean isTraditional() {
        if (!isChinese()) {
            return false;
        }
        String country = Locale.getDefault().getCountry();
        return "TW".equals(country) || "HK".equals(country) || "MO".equals(country);
    }

    private static String t(String key) {
        if (!isChinese()) {
            switch (key) {
                case "err_crash_prev": return "Crashed on last launch";
                case "err_wasm_load": return "Failed to load WASM module";
                case "err_wasm_init": return "WASM init failed";
                case "err_wasm_render": return "WASM render failed";
                case "err_wasm_empty": return "WASM module is empty";
                case "err_asset_missing": return "Failed to load WASM module";
                case "body_crash_prev": return "The app crashed on the last launch. Crash report:";
                case "body_details": return "Details:";
                case "body_asset_missing":
                    return "Cannot read assets/sample.wasm." + NL
                            + "Make sure the APK contains it, or regenerate:" + NL
                            + "  python3 tools/gen_sample_wasm.py";
                case "hint_retry": return "Retry";
                case "hint_copy": return "Copy error report";
                case "hint_clear": return "Clear crash log and retry";
                case "hint_full_log": return "Full log: ";
                case "toast_copied": return "Error report copied to clipboard";
                default: return key;
            }
        }
        if (!isTraditional()) {
            switch (key) {
                case "err_crash_prev": return "上次启动时崩溃";
                case "err_wasm_load": return "WASM 模块加载失败";
                case "err_wasm_init": return "WASM init 失败";
                case "err_wasm_render": return "WASM 渲染失败";
                case "err_wasm_empty": return "WASM 模块为空";
                case "err_asset_missing": return "WASM 模块加载失败";
                case "body_crash_prev": return "应用上次启动时崩溃。以下是崩溃报告:";
                case "body_details": return "错误详情:";
                case "body_asset_missing":
                    return "无法读取 assets/sample.wasm。" + NL
                            + "请确认 APK 内包含该文件, 或重新生成:" + NL
                            + "  python3 tools/gen_sample_wasm.py";
                case "hint_retry": return "重试";
                case "hint_copy": return "复制错误信息";
                case "hint_clear": return "清除崩溃日志并重试";
                case "hint_full_log": return "完整日志: ";
                case "toast_copied": return "错误报告已复制到剪贴板";
                default: return key;
            }
        }
        switch (key) {
            case "err_crash_prev": return "上次啟動時崩潰";
            case "err_wasm_load": return "WASM 模組載入失敗";
            case "err_wasm_init": return "WASM init 失敗";
            case "err_wasm_render": return "WASM 轉譯失敗";
            case "err_wasm_empty": return "WASM 模組為空";
            case "err_asset_missing": return "WASM 模組載入失敗";
            case "body_crash_prev": return "應用上次啟動時崩潰。以下是崩潰報告:";
            case "body_details": return "錯誤詳情:";
            case "body_asset_missing":
                return "無法讀取 assets/sample.wasm。" + NL
                        + "請確認 APK 內包含該檔案, 或重新產生:" + NL
                        + "  python3 tools/gen_sample_wasm.py";
            case "hint_retry": return "重試";
            case "hint_copy": return "複製錯誤資訊";
            case "hint_clear": return "清除日誌並重試";
            case "hint_full_log": return "完整日誌: ";
            case "toast_copied": return "錯誤報告已複製到剪貼板";
            default: return key;
        }
    }

    // ---- UI ----

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        hideSystemBars();

        // 第三层防护: 监听系统栏可见性变化 (滑动呼出后被系统清除标志时立即重设)
        getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(
                new View.OnSystemUiVisibilityChangeListener() {
                    @Override
                    public void onSystemUiVisibilityChange(int visibility) {
                        if ((visibility & View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) == 0) {
                            hideSystemBars();
                        }
                    }
                });

        final String key = getIntent().getStringExtra(EXTRA_ERROR_KEY);
        final String details = getIntent().getStringExtra(EXTRA_DETAILS);
        final String logPath = getIntent().getStringExtra(EXTRA_LOG_PATH);
        final String crashPath = getIntent().getStringExtra(EXTRA_CRASH_PATH);
        final String errorKey = (key == null) ? "crash_prev" : key;

        // 标题
        TextView titleView = new TextView(this);
        titleView.setText(t("err_" + errorKey));
        titleView.setTextColor(TITLE_COLOR);
        titleView.setTextSize(20);
        titleView.setTypeface(Typeface.DEFAULT_BOLD);

        // 正文 (等宽, 可滚动, 可长按选中复制)
        TextView bodyView = new TextView(this);
        bodyView.setText(buildBody(errorKey, details, logPath));
        bodyView.setTextColor(TEXT_COLOR);
        bodyView.setTextSize(13);
        bodyView.setTypeface(Typeface.MONOSPACE);
        bodyView.setTextIsSelectable(true);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(bodyView);

        // 完整错误文本 (复制按钮用): 标题 + 正文
        mFullText = titleView.getText() + NL + NL + buildBody(errorKey, details, logPath);

        // 按钮: 复制错误信息 / 重试 / 清除崩溃日志并重试
        Button copyBtn = new Button(this);
        copyBtn.setText(t("hint_copy"));
        copyBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                if (cm != null) {
                    cm.setPrimaryClip(ClipData.newPlainText("error report", mFullText));
                    Toast.makeText(ErrorActivity.this, t("toast_copied"),
                            Toast.LENGTH_SHORT).show();
                }
            }
        });

        Button retryBtn = new Button(this);
        retryBtn.setText(t("hint_retry"));
        retryBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                finish();
            }
        });

        Button clearBtn = new Button(this);
        clearBtn.setText(t("hint_clear"));
        clearBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (crashPath != null) {
                    new File(crashPath).delete();
                }
                finish();
            }
        });

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG_COLOR);
        int pad = dp(16);
        root.setPadding(pad, dp(28), pad, pad);
        root.addView(titleView);
        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        root.addView(scroll, scrollLp);
        root.addView(copyBtn);
        root.addView(retryBtn);
        root.addView(clearBtn);
        setContentView(root);
    }

    private String buildBody(String errorKey, String details, String logPath) {
        StringBuilder sb = new StringBuilder();
        if ("asset_missing".equals(errorKey)) {
            sb.append(t("body_asset_missing"));
        } else if ("crash_prev".equals(errorKey)) {
            sb.append(t("body_crash_prev"));
            if (details != null) {
                sb.append(NL).append(details);
            }
        } else {
            sb.append(t("body_details"));
            if (details != null) {
                sb.append(NL).append(details);
            }
        }
        if (logPath != null) {
            sb.append(NL).append(NL).append(t("hint_full_log")).append(logPath);
        }
        return sb.toString();
    }

    private void hideSystemBars() {
        View decor = getWindow().getDecorView();
        decor.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    // 第一层: onResume + 延迟兜底 (某些 ROM 在 resume 后才恢复系统栏)
    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        getWindow().getDecorView().postDelayed(new Runnable() {
            @Override
            public void run() {
                hideSystemBars();
            }
        }, 300);
    }

    // 第二层: 窗口焦点变化时重设 (切后台再切回, 标志会被系统清掉)
    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
            getWindow().getDecorView().postDelayed(new Runnable() {
                @Override
                public void run() {
                    hideSystemBars();
                }
            }, 300);
        }
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density);
    }
}
