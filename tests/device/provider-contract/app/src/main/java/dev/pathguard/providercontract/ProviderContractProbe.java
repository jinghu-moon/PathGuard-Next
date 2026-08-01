package dev.pathguard.providercontract;

import android.content.ContentResolver;
import android.content.ContentUris;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.MediaStore;

import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

final class ProviderContractProbe {
    private static final byte[] PAYLOAD =
            "pathguard-provider-contract-v1".getBytes(StandardCharsets.US_ASCII);

    private ProviderContractProbe() {}

    static boolean runMediaStore(Context context, EvidenceStore evidence) {
        ContentResolver resolver = context.getContentResolver();
        Uri collection = MediaStore.Images.Media.getContentUri(
                MediaStore.VOLUME_EXTERNAL_PRIMARY);
        String nonce = Long.toString(System.currentTimeMillis());
        String originalName = "pg-contract-" + nonce + ".jpg";
        String renamedName = "pg-contract-renamed-" + nonce + ".jpg";
        Uri item = null;
        boolean passed = true;
        try {
            ContentValues values = new ContentValues();
            values.put(MediaStore.MediaColumns.DISPLAY_NAME, originalName);
            values.put(MediaStore.MediaColumns.MIME_TYPE, "image/jpeg");
            values.put(
                    MediaStore.MediaColumns.RELATIVE_PATH,
                    "Pictures/PathGuardContract/");
            values.put(MediaStore.MediaColumns.IS_PENDING, 1);
            item = resolver.insert(collection, values);
            boolean inserted = item != null;
            evidence.record("media_store", "insert", inserted, String.valueOf(item));
            if (!inserted) return false;

            boolean wrote = writePayload(resolver, item);
            evidence.record("media_store", "open_write", wrote, item.toString());
            passed &= wrote;

            ContentValues publish = new ContentValues();
            publish.put(MediaStore.MediaColumns.IS_PENDING, 0);
            boolean published = resolver.update(item, publish, null, null) == 1;
            evidence.record("media_store", "publish", published, item.toString());
            passed &= published;

            boolean queried = queryMedia(
                    resolver, item, originalName, "Pictures/PathGuardContract/");
            evidence.record("media_store", "query", queried, item.toString());
            passed &= queried;

            boolean read = readPayload(resolver, item);
            evidence.record("media_store", "open_read", read, item.toString());
            passed &= read;

            ContentValues rename = new ContentValues();
            rename.put(MediaStore.MediaColumns.DISPLAY_NAME, renamedName);
            rename.put(
                    MediaStore.MediaColumns.RELATIVE_PATH,
                    "Pictures/PathGuardContractMoved/");
            boolean renamed = resolver.update(item, rename, null, null) == 1
                    && queryMedia(
                            resolver, item, renamedName,
                            "Pictures/PathGuardContractMoved/");
            evidence.record("media_store", "rename", renamed, item.toString());
            passed &= renamed;
        } catch (Exception error) {
            evidence.record("media_store", "exception", false, error.toString());
            passed = false;
        } finally {
            if (item != null) {
                try {
                    boolean deleted = resolver.delete(item, null, null) == 1;
                    evidence.record("media_store", "delete", deleted, item.toString());
                    passed &= deleted;
                } catch (Exception error) {
                    evidence.record("media_store", "delete", false, error.toString());
                    passed = false;
                }
            }
        }
        return passed;
    }

    static boolean runDocumentsProvider(
            Context context, Uri treeUri, EvidenceStore evidence) {
        ContentResolver resolver = context.getContentResolver();
        String nonce = Long.toString(System.currentTimeMillis());
        String originalName = "pg-contract-" + nonce + ".bin";
        String renamedName = "pg-contract-renamed-" + nonce + ".bin";
        Uri document = null;
        boolean passed = true;
        try {
            String parentId = DocumentsContract.getTreeDocumentId(treeUri);
            Uri parent = DocumentsContract.buildDocumentUriUsingTree(treeUri, parentId);
            document = DocumentsContract.createDocument(
                    resolver, parent, "application/octet-stream", originalName);
            boolean created = document != null;
            evidence.record("documents_provider", "create", created,
                    String.valueOf(document));
            if (!created) return false;

            String documentId = DocumentsContract.getDocumentId(document);
            boolean queried = queryDocument(resolver, document, originalName, documentId);
            evidence.record("documents_provider", "query", queried, documentId);
            passed &= queried;

            boolean wrote = writePayload(resolver, document);
            evidence.record("documents_provider", "open_write", wrote, documentId);
            passed &= wrote;

            boolean read = readPayload(resolver, document);
            evidence.record("documents_provider", "open_read", read, documentId);
            passed &= read;

            Uri renamedDocument = DocumentsContract.renameDocument(
                    resolver, document, renamedName);
            boolean renamed = renamedDocument != null;
            if (renamed) document = renamedDocument;
            String renamedId = renamed
                    ? DocumentsContract.getDocumentId(document) : "";
            renamed &= queryDocument(resolver, document, renamedName, renamedId);
            evidence.record("documents_provider", "rename", renamed, renamedId);
            passed &= renamed;
        } catch (Exception error) {
            evidence.record("documents_provider", "exception", false, error.toString());
            passed = false;
        } finally {
            if (document != null) {
                try {
                    DocumentsContract.deleteDocument(resolver, document);
                    evidence.record("documents_provider", "delete", true,
                            document.toString());
                } catch (Exception error) {
                    evidence.record("documents_provider", "delete", false,
                            error.toString());
                    passed = false;
                }
            }
        }
        return passed;
    }

    private static boolean writePayload(ContentResolver resolver, Uri uri)
            throws IOException {
        try (ParcelFileDescriptor descriptor =
                     resolver.openFileDescriptor(uri, "wt", null)) {
            if (descriptor == null) return false;
            try (FileOutputStream output =
                         new FileOutputStream(descriptor.getFileDescriptor())) {
                output.write(PAYLOAD);
                output.flush();
            }
        }
        return true;
    }

    private static boolean readPayload(ContentResolver resolver, Uri uri)
            throws IOException {
        try (ParcelFileDescriptor descriptor =
                     resolver.openFileDescriptor(uri, "r", null)) {
            if (descriptor == null) return false;
            try (FileInputStream input =
                         new FileInputStream(descriptor.getFileDescriptor())) {
                byte[] actual = new byte[PAYLOAD.length];
                int offset = 0;
                while (offset < actual.length) {
                    int count = input.read(actual, offset, actual.length - offset);
                    if (count < 0) break;
                    offset += count;
                }
                return offset == PAYLOAD.length && Arrays.equals(actual, PAYLOAD);
            }
        }
    }

    private static boolean queryMedia(
            ContentResolver resolver, Uri uri, String name, String relativePath) {
        String[] projection = {
                MediaStore.MediaColumns._ID,
                MediaStore.MediaColumns.DISPLAY_NAME,
                MediaStore.MediaColumns.RELATIVE_PATH,
        };
        try (Cursor cursor = resolver.query(uri, projection, null, null, null)) {
            return cursor != null && cursor.moveToFirst()
                    && ContentUris.parseId(uri) == cursor.getLong(0)
                    && name.equals(cursor.getString(1))
                    && relativePath.equals(cursor.getString(2));
        }
    }

    private static boolean queryDocument(
            ContentResolver resolver, Uri uri, String name, String documentId) {
        String[] projection = {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        };
        try (Cursor cursor = resolver.query(uri, projection, null, null, null)) {
            return cursor != null && cursor.moveToFirst()
                    && documentId.equals(cursor.getString(0))
                    && name.equals(cursor.getString(1));
        }
    }
}
