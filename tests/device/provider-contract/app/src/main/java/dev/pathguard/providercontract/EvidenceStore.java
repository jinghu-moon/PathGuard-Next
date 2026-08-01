package dev.pathguard.providercontract;

import android.content.Context;
import android.os.Build;
import android.os.Process;

import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardOpenOption;

final class EvidenceStore {
    private final File directory;
    private final File observations;
    private final File status;

    EvidenceStore(Context context) {
        directory = new File(context.getFilesDir(), "provider-contract");
        observations = new File(directory, "observations.jsonl");
        status = new File(directory, "status");
    }

    synchronized void reset(Context context) throws IOException {
        if (!directory.exists() && !directory.mkdirs()) {
            throw new IOException("cannot create evidence directory");
        }
        Files.deleteIfExists(observations.toPath());
        write(status, "starting");
        String metadata = "{\"schema\":1,\"package\":"
                + JSONObject.quote(context.getPackageName())
                + ",\"uid\":" + Process.myUid()
                + ",\"sdk\":" + Build.VERSION.SDK_INT
                + ",\"fingerprint\":" + JSONObject.quote(Build.FINGERPRINT)
                + "}";
        write(new File(directory, "metadata.json"), metadata);
    }

    synchronized void record(
            String domain, String operation, boolean passed, String detail) {
        String row = "{\"schema\":1,\"domain\":" + JSONObject.quote(domain)
                + ",\"operation\":" + JSONObject.quote(operation)
                + ",\"passed\":" + passed
                + ",\"detail\":" + JSONObject.quote(detail == null ? "" : detail)
                + "}\n";
        try {
            Files.write(
                    observations.toPath(), row.getBytes(StandardCharsets.UTF_8),
                    StandardOpenOption.CREATE, StandardOpenOption.APPEND);
        } catch (IOException ignored) {
            // The status file and on-screen state still expose probe failure.
        }
    }

    synchronized void setStatus(String value) throws IOException {
        write(status, value);
    }

    private static void write(File file, String value) throws IOException {
        Files.write(file.toPath(), value.getBytes(StandardCharsets.UTF_8));
    }
}
