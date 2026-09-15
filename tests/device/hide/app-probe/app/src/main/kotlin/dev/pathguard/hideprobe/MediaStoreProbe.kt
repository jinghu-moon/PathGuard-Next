package dev.pathguard.hideprobe

import android.content.Context
import android.provider.MediaStore

internal object MediaStoreProbe {
    fun run(context: Context, observedPaths: Array<String>): String = buildString {
        val resolver = context.contentResolver
        observedPaths.forEachIndexed { index, path ->
            val relative = StoragePath.relativeDirectory(path) ?: return@forEachIndexed
            val uri = MediaStore.Images.Media.EXTERNAL_CONTENT_URI
            runCatching {
                resolver.query(uri, arrayOf(MediaStore.Images.Media._ID, MediaStore.Images.Media.RELATIVE_PATH),
                    "${MediaStore.Images.Media.RELATIVE_PATH} LIKE ?", arrayOf("$relative%"), null).use { cursor ->
                        append(JsonObservation.render("media.$index.query", "media_store", relative, cursor?.count?.toLong() ?: -1L, 0, false, if (cursor == null) "unsupported" else "observed")).append('\n')
                    }
            }.onFailure {
                append(JsonObservation.render("media.$index.query", "media_store", relative, -1, 0, false, "observed")).append('\n')
            }
        }
    }
}
