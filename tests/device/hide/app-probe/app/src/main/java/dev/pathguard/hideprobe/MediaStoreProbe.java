package dev.pathguard.hideprobe;

import android.content.ContentResolver;
import android.content.ContentUris;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.CancellationSignal;
import android.provider.MediaStore;

import java.io.FileNotFoundException;

final class MediaStoreProbe {
    private MediaStoreProbe() {}

    static String run(Context context, String[] observedPaths) {
        StringBuilder output = new StringBuilder();
        ContentResolver resolver = context.getContentResolver();
        Uri collection = MediaStore.Images.Media.getContentUri(
                MediaStore.VOLUME_EXTERNAL);
        for (int index = 0; index < observedPaths.length; ++index) {
            String relative = StoragePath.relativeDirectory(observedPaths[index]);
            if (relative == null) {
                append(output, "media." + index + ".path", observedPaths[index],
                        -1, "setup_error");
                continue;
            }
            queryLegacy(output, resolver, collection, index, relative);
            queryBundle(output, resolver, collection, index, relative);
        }
        return output.toString();
    }

    private static void queryLegacy(
            StringBuilder output,
            ContentResolver resolver,
            Uri collection,
            int index,
            String relative) {
        String[] projection = {
                MediaStore.Images.Media._ID,
                MediaStore.Images.Media.RELATIVE_PATH,
                MediaStore.Images.Media.DISPLAY_NAME,
        };
        String selection = MediaStore.Images.Media.RELATIVE_PATH + " = ? OR "
                + MediaStore.Images.Media.RELATIVE_PATH + " LIKE ?";
        String[] arguments = {relative, relative + "%"};
        try (Cursor cursor = resolver.query(
                collection, projection, selection, arguments, null)) {
            int count = cursor == null ? -1 : cursor.getCount();
            append(output, "media." + index + ".legacy_query", relative,
                    count, cursor == null ? "unsupported" : "observed");
            if (cursor != null && cursor.moveToFirst()) {
                long id = cursor.getLong(0);
                Uri item = ContentUris.withAppendedId(collection, id);
                try (android.os.ParcelFileDescriptor ignored =
                             resolver.openFileDescriptor(item, "r")) {
                    append(output, "media." + index + ".direct_uri_open",
                            item.toString(), ignored == null ? -1 : 0,
                            ignored == null ? "unsupported" : "observed");
                } catch (FileNotFoundException | SecurityException error) {
                    append(output, "media." + index + ".direct_uri_open",
                            item.toString(), -1, "observed");
                } catch (Exception error) {
                    append(output, "media." + index + ".direct_uri_open",
                            item.toString(), -1, "setup_error");
                }
            }
        } catch (RuntimeException error) {
            append(output, "media." + index + ".legacy_query", relative,
                    -1, "setup_error");
        }
    }

    private static void queryBundle(
            StringBuilder output,
            ContentResolver resolver,
            Uri collection,
            int index,
            String relative) {
        Bundle arguments = new Bundle();
        arguments.putString(
                ContentResolver.QUERY_ARG_SQL_SELECTION,
                MediaStore.Images.Media.RELATIVE_PATH + " = ? OR "
                        + MediaStore.Images.Media.RELATIVE_PATH + " LIKE ?");
        arguments.putStringArray(
                ContentResolver.QUERY_ARG_SQL_SELECTION_ARGS,
                new String[]{relative, relative + "%"});
        try (Cursor cursor = resolver.query(
                collection,
                new String[]{MediaStore.Images.Media._ID},
                arguments,
                new CancellationSignal())) {
            int count = cursor == null ? -1 : cursor.getCount();
            append(output, "media." + index + ".bundle_query", relative,
                    count, cursor == null ? "unsupported" : "observed");
        } catch (RuntimeException error) {
            append(output, "media." + index + ".bundle_query", relative,
                    -1, "setup_error");
        }
    }

    private static void append(
            StringBuilder output,
            String test,
            String path,
            long returnValue,
            String status) {
        output.append(JsonObservation.render(
                test, "media_store", path, returnValue, 0, false, status));
        output.append('\n');
    }
}
