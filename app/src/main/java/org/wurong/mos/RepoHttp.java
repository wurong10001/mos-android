package org.wurong.mos;

// mos-repo 软件源 JNI 桥: 给 native 层提供 HTTP GET / SHA-256 / 写文件能力，
// 避免在 NDK 侧引入 curl/OpenSSL。由 repo_client.cpp 通过 JNI 静态调用，
// 运行在 native_app_glue 工作线程（非主线程，无 NetworkOnMainThread 限制）。
// 本轮只提供下载逻辑，安装逻辑后续在本类扩展。

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

public final class RepoHttp {

    /** 单文件下载上限，防止异常大文件耗尽内存 */
    private static final int MAX_BYTES = 64 * 1024 * 1024;

    private RepoHttp() {
    }

    public static byte[] httpGet(String urlStr, int timeoutMs) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(urlStr).openConnection();
        c.setConnectTimeout(timeoutMs);
        c.setReadTimeout(timeoutMs);
        c.setInstanceFollowRedirects(true);
        c.setRequestProperty("User-Agent", "MOS-Android/repo-client");
        try {
            int code = c.getResponseCode();
            if (code < 200 || code >= 300) {
                throw new IOException("HTTP " + code + " " + urlStr);
            }
            InputStream in = c.getInputStream();
            try {
                ByteArrayOutputStream out = new ByteArrayOutputStream(Math.max(8192, c.getContentLength()));
                byte[] buf = new byte[16384];
                int n;
                long total = 0;
                while ((n = in.read(buf)) > 0) {
                    total += n;
                    if (total > MAX_BYTES) {
                        throw new IOException("file too large (>64MB): " + urlStr);
                    }
                    out.write(buf, 0, n);
                }
                return out.toByteArray();
            } finally {
                in.close();
            }
        } finally {
            c.disconnect();
        }
    }

    /** 小写十六进制 SHA-256，用于与索引中 sha256 字段比对 */
    public static String sha256Hex(byte[] data) {
        try {
            byte[] d = MessageDigest.getInstance("SHA-256").digest(data);
            StringBuilder sb = new StringBuilder(64);
            for (byte b : d) {
                sb.append(Character.forDigit((b >> 4) & 0xF, 16));
                sb.append(Character.forDigit(b & 0xF, 16));
            }
            return sb.toString();
        } catch (NoSuchAlgorithmException e) {
            throw new RuntimeException("SHA-256 unavailable", e);
        }
    }

    /** 写入私有目录文件（自动创建父目录，含 user/tmp），同步落盘 */
    public static void writeFile(String path, byte[] data) throws IOException {
        File f = new File(path);
        File parent = f.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("mkdirs failed: " + parent.getAbsolutePath());
        }
        FileOutputStream o = new FileOutputStream(f);
        try {
            o.write(data);
            o.getFD().sync();
        } finally {
            o.close();
        }
    }
}
