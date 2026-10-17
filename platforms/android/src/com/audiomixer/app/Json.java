package com.audiomixer.app;

import java.util.HashMap;
import java.util.Map;

/** Just enough JSON for the bridge protocol: flat objects in (strings, numbers, booleans, null; nested values are skipped), escaped strings out. No android.* classes: runs on a plain JVM. */
final class Json {
    private Json() {}

    static String esc(String s) {
        StringBuilder b = new StringBuilder(s == null ? 0 : s.length() + 8);
        if (s != null) for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c < 0x20) b.append(' ');
            else b.append(c);
        }
        return b.toString();
    }

    static String str(String s) { return "\"" + esc(s) + "\""; }

    /** Parses one flat JSON object; returns an empty map for anything else. Numbers become Double, true / false Boolean, strings String. */
    static Map<String, Object> parse(String text) {
        Map<String, Object> out = new HashMap<String, Object>();
        if (text == null) return out;
        int[] p = { 0 };
        skip(text, p);
        if (p[0] >= text.length() || text.charAt(p[0]) != '{') return out;
        p[0]++;
        while (true) {
            skip(text, p);
            if (p[0] >= text.length()) return new HashMap<String, Object>();
            char c = text.charAt(p[0]);
            if (c == '}') return out;
            if (c == ',') { p[0]++; continue; }
            if (c != '"') return new HashMap<String, Object>();
            String key = readString(text, p);
            skip(text, p);
            if (p[0] >= text.length() || text.charAt(p[0]) != ':') return new HashMap<String, Object>();
            p[0]++;
            skip(text, p);
            if (p[0] >= text.length()) return new HashMap<String, Object>();
            c = text.charAt(p[0]);
            if (c == '"') out.put(key, readString(text, p));
            else if (c == '{' || c == '[') skipNested(text, p);
            else {
                int s = p[0];
                while (p[0] < text.length() && ",} \t\r\n".indexOf(text.charAt(p[0])) < 0) p[0]++;
                String tok = text.substring(s, p[0]);
                if (tok.equals("true")) out.put(key, Boolean.TRUE);
                else if (tok.equals("false")) out.put(key, Boolean.FALSE);
                else if (!tok.equals("null")) { try { out.put(key, Double.valueOf(tok)); } catch (NumberFormatException e) { return new HashMap<String, Object>(); } }
            }
        }
    }

    static int intOf(Map<String, Object> m, String key, int def) {
        Object v = m.get(key);
        if (v instanceof Double) return (int) Math.round((Double) v);
        return def;
    }

    private static void skip(String t, int[] p) { while (p[0] < t.length() && " \t\r\n".indexOf(t.charAt(p[0])) >= 0) p[0]++; }

    private static String readString(String t, int[] p) {
        StringBuilder b = new StringBuilder();
        p[0]++;
        while (p[0] < t.length()) {
            char c = t.charAt(p[0]++);
            if (c == '"') break;
            if (c == '\\' && p[0] < t.length()) {
                char n = t.charAt(p[0]++);
                if (n == 'n') b.append('\n'); else if (n == 't') b.append('\t');
                else if (n == 'u' && p[0] + 4 <= t.length()) { try { b.append((char) Integer.parseInt(t.substring(p[0], p[0] + 4), 16)); } catch (NumberFormatException e) { /* skip */ } p[0] += 4; }
                else b.append(n);
            } else b.append(c);
        }
        return b.toString();
    }

    private static void skipNested(String t, int[] p) {
        int depth = 0; boolean inStr = false;
        while (p[0] < t.length()) {
            char c = t.charAt(p[0]++);
            if (inStr) { if (c == '\\') p[0]++; else if (c == '"') inStr = false; continue; }
            if (c == '"') inStr = true; else if (c == '{' || c == '[') depth++; else if (c == '}' || c == ']') { depth--; if (depth == 0) return; }
        }
    }
}
