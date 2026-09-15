package dev.pathguard.hideprobe

internal object StoragePath {
    private const val PRIMARY_ROOT = "/storage/emulated/0/"

    fun relativeDirectory(absolutePath: String): String? {
        if (!absolutePath.startsWith(PRIMARY_ROOT) || absolutePath.length <= PRIMARY_ROOT.length) return null
        val relative = absolutePath.removePrefix(PRIMARY_ROOT).trimEnd { it == '/' }
        return relative.takeIf { it.isNotEmpty() }?.plus('/')
    }

    fun expandVfsAliases(canonicalPaths: Array<String>): Array<String> = buildList {
        canonicalPaths.forEach { path ->
            add(path)
            val relative = relativeDirectory(path)?.removeSuffix("/") ?: return@forEach
            add("/sdcard/$relative")
            add("/storage/self/primary/$relative")
            add("/mnt/user/0/primary/$relative")
            add("/mnt/runtime/default/emulated/0/$relative")
            add("/mnt/runtime/read/emulated/0/$relative")
            add("/mnt/runtime/write/emulated/0/$relative")
            add("/data/media/0/$relative")
        }
    }.distinct().toTypedArray()
}
