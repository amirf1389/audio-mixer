package com.amfb.amixos.remote

/** A small JSON reader / writer (objects, arrays, strings, numbers, booleans, null) so the remote needs no library: the daemon's
 *  documents are flat and small. Parsed values: Map<String, Any?>, List<Any?>, String, Double, Boolean, null. */
object MiniJson {
    fun parse(text: String): Any? { val p = Parser(text); val v = p.value(); p.ws(); if (p.i != text.length) throw IllegalArgumentException("trailing data at ${p.i}"); return v }

    fun stringify(v: Any?): String { val sb = StringBuilder(); write(v, sb); return sb.toString() }

    private fun write(v: Any?, sb: StringBuilder) {
        when (v) {
            null -> sb.append("null")
            is String -> quote(v, sb)
            is Boolean -> sb.append(if (v) "true" else "false")
            is Int, is Long -> sb.append(v.toString())
            is Number -> { val d = v.toDouble(); if (d == Math.floor(d) && Math.abs(d) < 1e15) sb.append(d.toLong().toString()) else sb.append(d.toString()) }
            is Map<*, *> -> { sb.append('{'); var first = true; for ((k, x) in v) { if (!first) sb.append(','); first = false; quote(k.toString(), sb); sb.append(':'); write(x, sb) }; sb.append('}') }
            is List<*> -> { sb.append('['); var first = true; for (x in v) { if (!first) sb.append(','); first = false; write(x, sb) }; sb.append(']') }
            else -> quote(v.toString(), sb)
        }
    }
    private fun quote(s: String, sb: StringBuilder) {
        sb.append('"')
        for (c in s) when (c) { '"' -> sb.append("\\\""); '\\' -> sb.append("\\\\"); '\n' -> sb.append("\\n"); '\r' -> sb.append("\\r"); '\t' -> sb.append("\\t"); else -> if (c < ' ') sb.append(String.format("\\u%04x", c.code)) else sb.append(c) }
        sb.append('"')
    }

    private class Parser(val s: String) {
        var i = 0
        fun ws() { while (i < s.length && s[i].isWhitespace()) i++ }
        fun value(): Any? {
            ws(); if (i >= s.length) throw IllegalArgumentException("unexpected end")
            return when (s[i]) {
                '{' -> obj(); '[' -> arr(); '"' -> str()
                't' -> { expect("true"); true }; 'f' -> { expect("false"); false }; 'n' -> { expect("null"); null }
                else -> num()
            }
        }
        fun expect(w: String) { if (!s.startsWith(w, i)) throw IllegalArgumentException("bad token at $i"); i += w.length }
        fun obj(): Map<String, Any?> { val m = LinkedHashMap<String, Any?>(); i++; ws(); if (s[i] == '}') { i++; return m }
            while (true) { ws(); val k = str(); ws(); if (s[i] != ':') throw IllegalArgumentException("':' expected at $i"); i++; m[k] = value(); ws(); if (s[i] == ',') { i++; continue }; if (s[i] == '}') { i++; return m }; throw IllegalArgumentException("',' or '}' expected at $i") } }
        fun arr(): List<Any?> { val l = ArrayList<Any?>(); i++; ws(); if (s[i] == ']') { i++; return l }
            while (true) { l.add(value()); ws(); if (s[i] == ',') { i++; continue }; if (s[i] == ']') { i++; return l }; throw IllegalArgumentException("',' or ']' expected at $i") } }
        fun str(): String { if (s[i] != '"') throw IllegalArgumentException("string expected at $i"); i++; val sb = StringBuilder()
            while (true) { val c = s[i++]; when (c) { '"' -> return sb.toString(); '\\' -> { val e = s[i++]; when (e) { 'n' -> sb.append('\n'); 'r' -> sb.append('\r'); 't' -> sb.append('\t'); 'b' -> sb.append('\b'); 'f' -> sb.append('\u000C'); 'u' -> { sb.append(s.substring(i, i + 4).toInt(16).toChar()); i += 4 }; else -> sb.append(e) } }; else -> sb.append(c) } } }
        fun num(): Double { val st = i; while (i < s.length && (s[i].isDigit() || s[i] in "+-.eE")) i++; if (st == i) throw IllegalArgumentException("value expected at $i"); return s.substring(st, i).toDouble() }
    }
}

/** typed access helpers over the parsed maps */
fun Map<String, Any?>.str(k: String, d: String = ""): String = this[k]?.toString() ?: d
fun Map<String, Any?>.num(k: String, d: Double = 0.0): Double = (this[k] as? Number)?.toDouble() ?: d
fun Map<String, Any?>.int(k: String, d: Int = 0): Int = (this[k] as? Number)?.toInt() ?: d
fun Map<String, Any?>.bool(k: String, d: Boolean = false): Boolean = this[k] as? Boolean ?: d
@Suppress("UNCHECKED_CAST") fun Map<String, Any?>.obj(k: String): Map<String, Any?> = this[k] as? Map<String, Any?> ?: emptyMap()
@Suppress("UNCHECKED_CAST") fun Map<String, Any?>.list(k: String): List<Map<String, Any?>> = (this[k] as? List<Any?>)?.mapNotNull { it as? Map<String, Any?> } ?: emptyList()
