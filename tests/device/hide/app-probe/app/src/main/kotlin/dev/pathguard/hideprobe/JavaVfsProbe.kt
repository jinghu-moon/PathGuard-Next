package dev.pathguard.hideprobe

import android.content.Context
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardOpenOption

internal object JavaVfsProbe {
    fun run(context: Context, observedPaths: Array<String>): String {
        val output = StringBuilder()
        val sandbox = File(context.noBackupFilesDir, "pathguard-hide-h0-java-${System.nanoTime()}")
        if (!sandbox.mkdir()) return row(output, "java.fixture", "setup_error", sandbox.path).toString()
        val hidden = File(sandbox, "hidden")
        val canary = File(hidden, "canary")
        try {
            if (!hidden.mkdir() || !canary.createNewFile()) return row(output, "java.fixture", "setup_error", hidden.path).toString()
            canary.writeText("pathguard-hide-canary")
            observe(output, "java.sandbox.hidden", hidden)
            observe(output, "java.sandbox.descendant", canary)
            mutate(output, hidden, canary)
            observedPaths.forEachIndexed { index, path -> observe(output, "java.external.$index", File(path)) }
        } finally {
            File(hidden, "created").delete(); File(hidden, "created-dir").delete()
            File(sandbox, "moved-canary").delete(); canary.delete(); hidden.delete()
            row(output, "java.probe.complete", if (sandbox.delete()) "observed" else "setup_error", sandbox.path)
        }
        return output.toString()
    }

    private fun observe(out: StringBuilder, label: String, path: File) {
        row(out, "$label.exists", "observed", path.path, if (path.exists()) 1 else 0)
        row(out, "$label.isDirectory", "observed", path.path, if (path.isDirectory) 1 else 0)
        val entries = path.parentFile?.list()
        row(out, "$label.list", if (entries == null) "unsupported" else "observed", path.path,
            if (entries?.contains(path.name) == true) 1 else 0)
        val nio = path.toPath()
        row(out, "$label.nio_exists", "observed", path.path, if (Files.exists(nio)) 1 else 0)
        row(out, "$label.nio_isDirectory", "observed", path.path, if (Files.isDirectory(nio)) 1 else 0)
        val listed = runCatching { path.parentFile?.toPath()?.let { Files.list(it).use { stream -> stream.anyMatch { it.fileName.toString() == path.name } } } ?: false }.getOrDefault(false)
        row(out, "$label.nio_directoryStream", "observed", path.path, if (listed) 1 else 0)
    }

    private fun mutate(out: StringBuilder, hidden: File, canary: File) {
        val created = File(hidden, "created"); val createdResult = created.createNewFile()
        row(out, "java.createNewFile", "observed", created.path, if (createdResult) 0 else -1, if (created.exists()) 1 else 0); created.delete()
        Files.write(canary.toPath(), ByteArray(0), StandardOpenOption.TRUNCATE_EXISTING)
        row(out, "java.nio_truncate", "observed", canary.path, 0, if (canary.length() == 0L) 1 else 0)
        canary.writeText("pathguard-hide-canary")
        val directory = File(hidden, "created-dir"); val made = directory.mkdir()
        row(out, "java.mkdir", "observed", directory.path, if (made) 0 else -1, if (directory.exists()) 1 else 0); directory.delete()
        val deleted = canary.delete(); row(out, "java.delete", "observed", canary.path, if (deleted) 0 else -1, if (deleted) 1 else 0)
        canary.createNewFile(); canary.writeText("pathguard-hide-canary")
        val moved = File(hidden.parentFile, "moved-canary"); val renamed = canary.renameTo(moved)
        row(out, "java.renameTo", "observed", canary.path, if (renamed) 0 else -1, if (renamed) 1 else 0); if (renamed) moved.renameTo(canary)
    }

    private fun row(out: StringBuilder, test: String, status: String, path: String, value: Int = -1, sideEffect: Int = 0): StringBuilder {
        out.append(JsonObservation.render(test, if (test.contains("mutation") || test.contains("create") || test.contains("delete") || test.contains("rename") || test.contains("mkdir") || test.contains("truncate")) "java_mutation" else "java_file", path, value.toLong(), 0, sideEffect != 0, status)).append('\n')
        return out
    }
}
