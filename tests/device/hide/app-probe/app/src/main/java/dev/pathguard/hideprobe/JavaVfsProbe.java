package dev.pathguard.hideprobe;

import android.content.Context;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

final class JavaVfsProbe {
    private JavaVfsProbe() {}

    static String run(Context context, String[] observedPaths) throws IOException {
        StringBuilder output = new StringBuilder();
        File sandbox = new File(
                context.getNoBackupFilesDir(),
                "pathguard-hide-h0-java-" + System.currentTimeMillis());
        if (!sandbox.mkdir()) {
            append(output, "java.sandbox.mkdir", "java_file", sandbox.getPath(),
                    -1, false, "setup_error");
            return output.toString();
        }

        File hidden = new File(sandbox, "hidden");
        File canary = new File(hidden, "canary");
        try {
            if (!hidden.mkdir() || !canary.createNewFile()) {
                append(output, "java.fixture", "java_file", hidden.getPath(),
                        -1, false, "setup_error");
                return output.toString();
            }
            try (FileOutputStream stream = new FileOutputStream(canary)) {
                stream.write("pathguard-hide-canary".getBytes(StandardCharsets.UTF_8));
            }

            observe(output, "java.sandbox.hidden", hidden);
            observe(output, "java.sandbox.descendant", canary);
            runMutations(output, hidden, canary);
            for (int index = 0; index < observedPaths.length; ++index) {
                observe(output, "java.external." + index, new File(observedPaths[index]));
            }
        } finally {
            new File(hidden, "created").delete();
            new File(hidden, "created-dir").delete();
            new File(sandbox, "moved-canary").delete();
            canary.delete();
            hidden.delete();
            boolean clean = sandbox.delete();
            append(output, "java.probe.complete", "java_probe", sandbox.getPath(),
                    clean ? 0 : -1, !clean, clean ? "observed" : "setup_error");
        }
        return output.toString();
    }

    private static void observe(StringBuilder output, String label, File path) {
        append(output, label + ".exists", "java_file", path.getPath(),
                path.exists() ? 1 : 0, false, "observed");
        append(output, label + ".isDirectory", "java_file", path.getPath(),
                path.isDirectory() ? 1 : 0, false, "observed");
        File parent = path.getParentFile();
        String[] entries = parent == null ? null : parent.list();
        boolean listed = false;
        if (entries != null) {
            for (String entry : entries) {
                if (entry.equals(path.getName())) {
                    listed = true;
                    break;
                }
            }
        }
        append(output, label + ".list", "java_file", path.getPath(),
                listed ? 1 : 0, false, entries == null ? "unsupported" : "observed");

        Path nioPath = path.toPath();
        append(output, label + ".nio_exists", "java_nio", path.getPath(),
                Files.exists(nioPath) ? 1 : 0, false, "observed");
        append(output, label + ".nio_isDirectory", "java_nio", path.getPath(),
                Files.isDirectory(nioPath) ? 1 : 0, false, "observed");
        boolean nioListed = false;
        String listStatus = "observed";
        if (parent == null) {
            listStatus = "setup_error";
        } else {
            try (DirectoryStream<Path> stream = Files.newDirectoryStream(parent.toPath())) {
                for (Path entry : stream) {
                    if (entry.getFileName().toString().equals(path.getName())) {
                        nioListed = true;
                        break;
                    }
                }
            } catch (IOException | SecurityException error) {
                listStatus = "unsupported";
            }
        }
        append(output, label + ".nio_directoryStream", "java_nio", path.getPath(),
                nioListed ? 1 : 0, false, listStatus);
    }

    private static void runMutations(StringBuilder output, File hidden, File canary)
            throws IOException {
        File created = new File(hidden, "created");
        boolean createdResult = created.createNewFile();
        append(output, "java.createNewFile", "java_mutation", created.getPath(),
                createdResult ? 0 : -1, created.exists(), "observed");
        created.delete();

        Files.write(canary.toPath(), new byte[0], StandardOpenOption.TRUNCATE_EXISTING);
        boolean truncated = canary.length() == 0;
        append(output, "java.nio_truncate", "java_mutation", canary.getPath(),
                truncated ? 0 : -1, truncated, "observed");
        Files.write(
                canary.toPath(),
                "pathguard-hide-canary".getBytes(StandardCharsets.UTF_8));

        File createdDirectory = new File(hidden, "created-dir");
        boolean directoryCreated = createdDirectory.mkdir();
        append(output, "java.mkdir", "java_mutation", createdDirectory.getPath(),
                directoryCreated ? 0 : -1, createdDirectory.exists(), "observed");
        createdDirectory.delete();

        boolean deleted = canary.delete();
        append(output, "java.delete", "java_mutation", canary.getPath(),
                deleted ? 0 : -1, deleted && !canary.exists(), "observed");
        canary.createNewFile();
        Files.write(
                canary.toPath(),
                "pathguard-hide-canary".getBytes(StandardCharsets.UTF_8));

        File moved = new File(hidden.getParentFile(), "moved-canary");
        boolean renamed = canary.renameTo(moved);
        append(output, "java.renameTo", "java_mutation", canary.getPath(),
                renamed ? 0 : -1, renamed && moved.exists(), "observed");
        if (renamed) moved.renameTo(canary);
    }

    private static void append(
            StringBuilder output,
            String test,
            String surface,
            String path,
            long returnValue,
            boolean sideEffect,
            String status) {
        output.append(JsonObservation.render(
                test, surface, path, returnValue, 0, sideEffect, status));
        output.append('\n');
    }
}
