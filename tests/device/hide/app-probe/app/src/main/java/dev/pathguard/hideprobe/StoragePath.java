package dev.pathguard.hideprobe;

import java.util.LinkedHashSet;

final class StoragePath {
    private static final String PRIMARY_ROOT = "/storage/emulated/0/";

    private StoragePath() {}

    static String relativeDirectory(String absolutePath) {
        if (!absolutePath.startsWith(PRIMARY_ROOT)
                || absolutePath.length() <= PRIMARY_ROOT.length()) {
            return null;
        }
        String relative = absolutePath.substring(PRIMARY_ROOT.length());
        while (relative.endsWith("/")) {
            relative = relative.substring(0, relative.length() - 1);
        }
        return relative.isEmpty() ? null : relative + "/";
    }

    static String[] expandVfsAliases(String[] canonicalPaths) {
        LinkedHashSet<String> paths = new LinkedHashSet<>();
        for (String canonicalPath : canonicalPaths) {
            paths.add(canonicalPath);
            String relative = relativeDirectory(canonicalPath);
            if (relative == null) continue;
            String withoutTrailingSlash = relative.substring(0, relative.length() - 1);
            paths.add("/sdcard/" + withoutTrailingSlash);
            paths.add("/storage/self/primary/" + withoutTrailingSlash);
            paths.add("/mnt/user/0/primary/" + withoutTrailingSlash);
            paths.add("/mnt/runtime/default/emulated/0/" + withoutTrailingSlash);
            paths.add("/mnt/runtime/read/emulated/0/" + withoutTrailingSlash);
            paths.add("/mnt/runtime/write/emulated/0/" + withoutTrailingSlash);
            paths.add("/data/media/0/" + withoutTrailingSlash);
        }
        return paths.toArray(new String[0]);
    }
}
