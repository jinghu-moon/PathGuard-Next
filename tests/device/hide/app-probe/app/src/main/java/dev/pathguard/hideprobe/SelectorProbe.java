package dev.pathguard.hideprobe;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

final class SelectorProbe {
    static final int REQUEST_PHOTO_PICKER = 1001;
    static final int REQUEST_OPEN_IMAGE = 1002;
    static final int REQUEST_PICTURES_TREE = 1003;
    static final int REQUEST_DCIM_TREE = 1004;

    private static final String EXTERNAL_STORAGE_AUTHORITY =
            "com.android.externalstorage.documents";

    private SelectorProbe() {}

    static Intent photoPickerIntent() {
        if (Build.VERSION.SDK_INT >= 33) {
            return new Intent(MediaStore.ACTION_PICK_IMAGES).setType("image/*");
        }
        return openImageIntent();
    }

    static Intent openImageIntent() {
        return new Intent(Intent.ACTION_OPEN_DOCUMENT)
                .addCategory(Intent.CATEGORY_OPENABLE)
                .setType("image/*");
    }

    static Intent picturesTreeIntent() {
        return treeIntent("primary:Pictures");
    }

    static Intent dcimTreeIntent() {
        return treeIntent("primary:DCIM");
    }

    static String labelForRequest(int requestCode) {
        switch (requestCode) {
            case REQUEST_PHOTO_PICKER: return "photo_picker";
            case REQUEST_OPEN_IMAGE: return "saf_image";
            case REQUEST_PICTURES_TREE: return "saf_pictures_tree";
            case REQUEST_DCIM_TREE: return "saf_dcim_tree";
            default: return "unknown";
        }
    }

    static String expectedChildForRequest(int requestCode) {
        if (requestCode == REQUEST_PICTURES_TREE) return "Nagram";
        if (requestCode == REQUEST_DCIM_TREE) return "Screenshots";
        return null;
    }

    static void reset(Context context) throws IOException {
        File evidence = evidenceFile(context);
        if (evidence.exists() && !evidence.delete()) {
            throw new IOException("cannot reset selector evidence");
        }
        append(context, capabilityObservations(context));
    }

    static void recordResult(
            Context context, int requestCode, int resultCode, Intent data)
            throws IOException {
        String label = labelForRequest(requestCode);
        Uri uri = data == null ? null : data.getData();
        StringBuilder output = new StringBuilder();
        add(output, "selector." + label + ".result", "selector",
                uri == null ? "" : uri.toString(),
                resultCode == Activity.RESULT_OK ? 0 : -1,
                resultCode == Activity.RESULT_CANCELED ? "observed" : "observed");
        if (resultCode != Activity.RESULT_OK || uri == null) {
            append(context, output.toString());
            return;
        }

        add(output, "selector." + label + ".authority", "selector",
                uri.getAuthority() == null ? "" : uri.getAuthority(), 0, "observed");
        queryDisplayName(context.getContentResolver(), uri, label, output);
        String expectedChild = expectedChildForRequest(requestCode);
        if (expectedChild != null) {
            enumerateTree(context.getContentResolver(), uri, label, expectedChild, output);
        } else {
            openDocument(context.getContentResolver(), uri, label, output);
        }
        append(context, output.toString());
    }

    private static Intent treeIntent(String documentId) {
        Uri initialUri = DocumentsContract.buildDocumentUri(
                EXTERNAL_STORAGE_AUTHORITY, documentId);
        return new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
                .putExtra(DocumentsContract.EXTRA_INITIAL_URI, initialUri);
    }

    private static String capabilityObservations(Context context) {
        StringBuilder output = new StringBuilder();
        capability(context, output, "photo_picker", photoPickerIntent());
        capability(context, output, "saf_image", openImageIntent());
        capability(context, output, "saf_pictures_tree", picturesTreeIntent());
        capability(context, output, "saf_dcim_tree", dcimTreeIntent());
        return output.toString();
    }

    private static void capability(
            Context context, StringBuilder output, String label, Intent intent) {
        boolean available = intent.resolveActivity(context.getPackageManager()) != null;
        add(output, "selector." + label + ".available", "selector_capability",
                intent.getAction() == null ? "" : intent.getAction(),
                available ? 1 : 0, "observed");
    }

    private static void queryDisplayName(
            ContentResolver resolver, Uri uri, String label, StringBuilder output) {
        try (Cursor cursor = resolver.query(
                uri,
                new String[]{OpenableColumns.DISPLAY_NAME},
                null,
                new CancellationSignal())) {
            String name = cursor != null && cursor.moveToFirst()
                    ? cursor.getString(0)
                    : "";
            add(output, "selector." + label + ".query", "selector_query",
                    name == null ? "" : name,
                    cursor == null ? -1 : cursor.getCount(),
                    cursor == null ? "unsupported" : "observed");
        } catch (RuntimeException error) {
            add(output, "selector." + label + ".query", "selector_query",
                    uri.toString(), -1, "observed");
        }
    }

    private static void openDocument(
            ContentResolver resolver, Uri uri, String label, StringBuilder output) {
        try (ParcelFileDescriptor descriptor = resolver.openFileDescriptor(uri, "r")) {
            add(output, "selector." + label + ".open", "selector_open",
                    uri.toString(), descriptor == null ? -1 : 0,
                    descriptor == null ? "unsupported" : "observed");
        } catch (IOException | SecurityException error) {
            add(output, "selector." + label + ".open", "selector_open",
                    uri.toString(), -1, "observed");
        }
    }

    private static void enumerateTree(
            ContentResolver resolver,
            Uri treeUri,
            String label,
            String expectedChild,
            StringBuilder output) {
        try {
            String documentId = DocumentsContract.getTreeDocumentId(treeUri);
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                    treeUri, documentId);
            int count = 0;
            boolean found = false;
            try (Cursor cursor = resolver.query(
                    children,
                    new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME},
                    null,
                    new CancellationSignal())) {
                if (cursor == null) {
                    add(output, "selector." + label + ".children",
                            "selector_tree", treeUri.toString(), -1, "unsupported");
                    return;
                }
                while (cursor.moveToNext()) {
                    ++count;
                    if (expectedChild.equals(cursor.getString(0))) found = true;
                }
            }
            add(output, "selector." + label + ".children", "selector_tree",
                    treeUri.toString(), count, "observed");
            add(output, "selector." + label + ".expected_child", "selector_tree",
                    expectedChild, found ? 1 : 0, "observed");
        } catch (RuntimeException error) {
            add(output, "selector." + label + ".children", "selector_tree",
                    treeUri.toString(), -1, "observed");
        }
    }

    private static void add(
            StringBuilder output,
            String test,
            String surface,
            String path,
            long returnValue,
            String status) {
        output.append(JsonObservation.render(
                test, surface, path, returnValue, 0, false, status));
        output.append('\n');
    }

    private static void append(Context context, String output) throws IOException {
        try (FileOutputStream stream = new FileOutputStream(evidenceFile(context), true)) {
            stream.write(output.getBytes(StandardCharsets.UTF_8));
        }
    }

    private static File evidenceFile(Context context) {
        File directory = new File(context.getFilesDir(), "hide-h0");
        directory.mkdir();
        return new File(directory, "selector-observations.jsonl");
    }
}
