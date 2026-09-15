package dev.pathguard.hideprobe

internal class NativeProbe private constructor() {
    companion object {
        init { System.loadLibrary("pathguard_hide_app_probe") }
        @JvmStatic external fun run(
            sandbox: String,
            observedPaths: Array<String>,
            attackMutations: Boolean,
            scenario: String,
        ): String?
    }
}
