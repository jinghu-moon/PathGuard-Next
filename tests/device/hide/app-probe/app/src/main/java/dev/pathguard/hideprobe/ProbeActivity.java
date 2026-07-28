package dev.pathguard.hideprobe;

import android.app.Activity;
import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Process;
import android.provider.Settings;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class ProbeActivity extends Activity {
    static final String EXTRA_OBSERVE_PATHS = "observe_paths";
    private static final String[] DEFAULT_PATHS = {
            "/storage/emulated/0/Pictures/Nagram",
            "/storage/emulated/0/DCIM/Screenshots",
    };

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private TextView statusView;
    private TextView permissionView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(createContentView());

        if (savedInstanceState == null) {
            try {
                SelectorProbe.reset(this);
            } catch (IOException error) {
                statusView.setText("Selector evidence reset failed: " + error);
            }
        }

        String[] paths = getIntent().getStringArrayExtra(EXTRA_OBSERVE_PATHS);
        if (paths == null || paths.length == 0) paths = DEFAULT_PATHS;
        String[] observedPaths = paths.clone();
        executor.execute(() -> runProbe(observedPaths));
    }

    @Override
    protected void onDestroy() {
        executor.shutdownNow();
        super.onDestroy();
    }

    @Override
    protected void onResume() {
        super.onResume();
        permissionView.setText(
                "Media images: " + permissionState(Manifest.permission.READ_MEDIA_IMAGES)
                        + " | All files: "
                        + (Environment.isExternalStorageManager() ? "granted" : "denied"));
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        executor.execute(() -> {
            try {
                SelectorProbe.recordResult(this, requestCode, resultCode, data);
                runOnUiThread(() -> statusView.setText(
                        "Recorded " + SelectorProbe.labelForRequest(requestCode)
                                + " result"));
            } catch (IOException error) {
                runOnUiThread(() -> statusView.setText(
                        "Selector result write failed: " + error));
            }
        });
    }

    private View createContentView() {
        int padding = Math.round(16 * getResources().getDisplayMetrics().density);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(padding, padding, padding, padding);

        TextView title = new TextView(this);
        title.setText("PathGuard Hide H0 Probe");
        title.setTextSize(20);
        content.addView(title);

        permissionView = new TextView(this);
        permissionView.setPadding(0, padding, 0, padding);
        content.addView(permissionView);

        statusView = new TextView(this);
        statusView.setText("H0 app-domain probe running...");
        statusView.setMinHeight(Math.round(48 * getResources().getDisplayMetrics().density));
        content.addView(statusView);

        addCommand(content, "Photo Picker", view -> startActivityForResult(
                SelectorProbe.photoPickerIntent(), SelectorProbe.REQUEST_PHOTO_PICKER));
        addCommand(content, "SAF image document", view -> startActivityForResult(
                SelectorProbe.openImageIntent(), SelectorProbe.REQUEST_OPEN_IMAGE));
        addCommand(content, "SAF Pictures tree", view -> startActivityForResult(
                SelectorProbe.picturesTreeIntent(), SelectorProbe.REQUEST_PICTURES_TREE));
        addCommand(content, "SAF DCIM tree", view -> startActivityForResult(
                SelectorProbe.dcimTreeIntent(), SelectorProbe.REQUEST_DCIM_TREE));
        addCommand(content, "All files access settings", view -> startActivity(
                new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                        .setData(Uri.parse("package:" + getPackageName()))));

        ScrollView container = new ScrollView(this);
        container.addView(content);
        return container;
    }

    private void addCommand(
            LinearLayout content, String label, View.OnClickListener listener) {
        Button command = new Button(this);
        command.setText(label);
        command.setAllCaps(false);
        command.setMinHeight(Math.round(48 * getResources().getDisplayMetrics().density));
        command.setOnClickListener(listener);
        content.addView(command);
    }

    private String permissionState(String permission) {
        return checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED
                ? "granted"
                : "denied";
    }

    private void runProbe(String[] observedPaths) {
        File resultDirectory = new File(getFilesDir(), "hide-h0");
        resultDirectory.mkdir();
        File status = new File(resultDirectory, "status");
        try {
            writeText(status, "running");
            String[] vfsPaths = StoragePath.expandVfsAliases(observedPaths);
            String javaOutput = JavaVfsProbe.run(this, vfsPaths);

            File nativeSandbox = new File(
                    getNoBackupFilesDir(),
                    "pathguard-hide-h0-native-" + System.currentTimeMillis());
            if (!nativeSandbox.mkdir()) {
                throw new IOException("cannot create native sandbox");
            }
            String nativeOutput = NativeProbe.run(
                    nativeSandbox.getCanonicalPath(), vfsPaths);
            if (nativeOutput == null) throw new IOException("native probe returned null");

            String mediaOutput = MediaStoreProbe.run(this, observedPaths);
            writeText(
                    new File(resultDirectory, "observations.jsonl"),
                    javaOutput + nativeOutput + mediaOutput);
            writeText(
                    new File(resultDirectory, "metadata.json"),
                    metadata(observedPaths, vfsPaths));
            writeText(status, "complete");
            runOnUiThread(() -> statusView.setText(
                    "H0 app-domain probe complete\n" + resultDirectory));
        } catch (Exception error) {
            try {
                writeText(status, "failed: " + error);
            } catch (IOException ignored) {
                // The on-screen error remains available when the result file fails.
            }
            runOnUiThread(() -> statusView.setText("H0 probe failed\n" + error));
        }
    }

    private String metadata(String[] paths, String[] vfsPaths) throws IOException {
        StringBuilder pathJson = new StringBuilder();
        for (int index = 0; index < paths.length; ++index) {
            if (index > 0) pathJson.append(',');
            pathJson.append('"').append(JsonObservation.escape(paths[index])).append('"');
        }
        String mountNamespace = Files.readSymbolicLink(
                new File("/proc/self/ns/mnt").toPath()).toString();
        String selinux = new String(
                Files.readAllBytes(new File("/proc/self/attr/current").toPath()),
                StandardCharsets.UTF_8).trim();
        return "{\"schema\":1,\"execution_context\":\"android_app\","
                + "\"package\":\"" + getPackageName() + "\","
                + "\"uid\":" + Process.myUid() + ","
                + "\"sdk\":" + android.os.Build.VERSION.SDK_INT + ","
                + "\"fingerprint\":\"" + JsonObservation.escape(android.os.Build.FINGERPRINT)
                + "\",\"selinux\":\"" + JsonObservation.escape(selinux)
                + "\",\"mount_namespace\":\"" + JsonObservation.escape(mountNamespace.trim())
                + "\",\"read_media_images\":"
                + (checkSelfPermission(Manifest.permission.READ_MEDIA_IMAGES)
                        == PackageManager.PERMISSION_GRANTED)
                + ",\"external_storage_manager\":"
                + Environment.isExternalStorageManager()
                + ",\"observe_paths\":[" + pathJson + "]"
                + ",\"vfs_observe_paths\":" + renderStringArray(vfsPaths) + "}";
    }

    private static String renderStringArray(String[] values) {
        StringBuilder output = new StringBuilder("[");
        for (int index = 0; index < values.length; ++index) {
            if (index > 0) output.append(',');
            output.append('"').append(JsonObservation.escape(values[index])).append('"');
        }
        return output.append(']').toString();
    }

    private static void writeText(File file, String value) throws IOException {
        Files.write(file.toPath(), value.getBytes(StandardCharsets.UTF_8));
    }
}
