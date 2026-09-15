package dev.pathguard.hideprobe

import android.app.Activity
import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.DocumentsContract
import android.provider.MediaStore
import android.provider.OpenableColumns
import java.io.File

internal object SelectorProbe {
    const val REQUEST_PHOTO_PICKER = 1001
    const val REQUEST_OPEN_IMAGE = 1002
    const val REQUEST_PICTURES_TREE = 1003
    const val REQUEST_DCIM_TREE = 1004
    private const val AUTHORITY = "com.android.externalstorage.documents"

    fun photoPickerIntent() = if (Build.VERSION.SDK_INT >= 33) Intent(MediaStore.ACTION_PICK_IMAGES).setType("image/*") else openImageIntent()
    fun openImageIntent() = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("image/*")
    fun picturesTreeIntent() = treeIntent("primary:Pictures")
    fun dcimTreeIntent() = treeIntent("primary:DCIM")
    fun labelForRequest(code: Int) = when (code) {
        REQUEST_PHOTO_PICKER -> "photo_picker"
        REQUEST_OPEN_IMAGE -> "saf_image"
        REQUEST_PICTURES_TREE -> "saf_pictures_tree"
        REQUEST_DCIM_TREE -> "saf_dcim_tree"
        else -> "unknown"
    }
    fun expectedChildForRequest(code: Int) = when (code) {
        REQUEST_PICTURES_TREE -> "Nagram"
        REQUEST_DCIM_TREE -> "Screenshots"
        else -> null
    }

    fun reset(context: Context) {
        evidenceFile(context).delete()
        append(context, listOf(
            capability(context, "photo_picker", photoPickerIntent()),
            capability(context, "saf_image", openImageIntent()),
            capability(context, "saf_pictures_tree", picturesTreeIntent()),
            capability(context, "saf_dcim_tree", dcimTreeIntent()),
        ).joinToString("\n", postfix = "\n"))
    }

    fun recordResult(context: Context, code: Int, resultCode: Int, data: Intent?) {
        val label = labelForRequest(code); val uri = data?.data
        val rows = mutableListOf(JsonObservation.render("selector.$label.result", "selector", uri?.toString() ?: "", if (resultCode == Activity.RESULT_OK) 0 else -1, 0, false, "observed"))
        if (resultCode == Activity.RESULT_OK && uri != null) {
            rows += JsonObservation.render("selector.$label.authority", "selector", uri.authority ?: "", 0, 0, false, "observed")
            runCatching {
                context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null).use { cursor ->
                    rows += JsonObservation.render("selector.$label.query", "selector_query", cursor?.use { if (it.moveToFirst()) it.getString(0) ?: "" else "" } ?: "", cursor?.count?.toLong() ?: -1, 0, false, if (cursor == null) "unsupported" else "observed")
                }
            }.onFailure { rows += JsonObservation.render("selector.$label.query", "selector_query", uri.toString(), -1, 0, false, "observed") }
            val child = expectedChildForRequest(code)
            if (child != null) rows += enumerateTree(context.contentResolver, uri, label, child)
            else rows += JsonObservation.render("selector.$label.open", "selector_open", uri.toString(), runCatching { context.contentResolver.openFileDescriptor(uri, "r")?.use { 0 } ?: -1 }.getOrDefault(-1).toLong(), 0, false, "observed")
        }
        append(context, rows.joinToString("\n", postfix = "\n"))
    }

    private fun treeIntent(id: String) = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).putExtra(DocumentsContract.EXTRA_INITIAL_URI, DocumentsContract.buildDocumentUri(AUTHORITY, id))
    private fun capability(context: Context, label: String, intent: Intent) = JsonObservation.render("selector.$label.available", "selector_capability", intent.action ?: "", if (intent.resolveActivity(context.packageManager) != null) 1 else 0, 0, false, "observed")
    private fun enumerateTree(resolver: ContentResolver, uri: Uri, label: String, expected: String): String {
        return runCatching {
            val id = DocumentsContract.getTreeDocumentId(uri)
            val children = DocumentsContract.buildChildDocumentsUriUsingTree(uri, id)
            resolver.query(children, arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME), null, null, null).use { cursor ->
                var found = false; var count = 0
                while (cursor?.moveToNext() == true) { count++; found = found || expected == cursor.getString(0) }
                JsonObservation.render("selector.$label.children", "selector_tree", uri.toString(), count.toLong(), 0, false, if (cursor == null) "unsupported" else "observed") + "\n" + JsonObservation.render("selector.$label.expected_child", "selector_tree", expected, if (found) 1 else 0, 0, false, "observed")
            }
        }.getOrElse { JsonObservation.render("selector.$label.children", "selector_tree", uri.toString(), -1, 0, false, "observed") }
    }
    private fun append(context: Context, value: String) { evidenceFile(context).apply { parentFile?.mkdirs(); appendText(value) } }
    private fun evidenceFile(context: Context) = File(context.filesDir, "hide-h0/selector-observations.jsonl")
}
