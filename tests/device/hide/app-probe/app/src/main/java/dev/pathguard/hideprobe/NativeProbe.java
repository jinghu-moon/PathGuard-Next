package dev.pathguard.hideprobe;

final class NativeProbe {
    static {
        System.loadLibrary("pathguard_hide_app_probe");
    }

    private NativeProbe() {}

    static native String run(String sandbox, String[] observedPaths);
}
