package dev.pathguard.hideprobe

internal object JsonObservation {
    fun render(test: String, surface: String, path: String, returnValue: Long,
               errno: Int, sideEffect: Boolean, status: String): String =
        "{\"schema\":1,\"kind\":\"observation\",\"test\":\"${escape(test)}\"," +
            "\"surface\":\"${escape(surface)}\",\"path\":\"${escape(path)}\"," +
            "\"return_value\":$returnValue,\"errno\":$errno," +
            "\"side_effect\":$sideEffect,\"status\":\"${escape(status)}\"}"

    fun escape(value: String): String = buildString(value.length) {
        value.forEach { ch ->
            when (ch) {
                '"' -> append("\\\"")
                '\\' -> append("\\\\")
                '\b' -> append("\\b")
                '\u000c' -> append("\\f")
                '\n' -> append("\\n")
                '\r' -> append("\\r")
                '\t' -> append("\\t")
                else -> if (ch < ' ') append("\\u%04x".format(ch.code)) else append(ch)
            }
        }
    }

    fun renderStringArray(values: Array<String>) = values.joinToString(
        separator = ",", prefix = "[", postfix = "]",
    ) { "\"${escape(it)}\"" }
}
