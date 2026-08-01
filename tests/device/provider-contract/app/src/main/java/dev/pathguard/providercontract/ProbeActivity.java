package dev.pathguard.providercontract;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class ProbeActivity extends Activity {
    private static final String EXTRA_AUTO_SELECT_SAF = "auto_select_saf";
    private static final int REQUEST_TREE = 1;

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private EvidenceStore evidence;
    private TextView status;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        evidence = new EvidenceStore(this);
        setContentView(createContentView());
        if (savedInstanceState == null) {
            executor.execute(this::runMediaStoreProbe);
        }
    }

    @Override
    protected void onDestroy() {
        executor.shutdownNow();
        super.onDestroy();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_TREE) return;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            evidence.record("documents_provider", "tree_selection", false, "cancelled");
            setStatus("SAF tree selection cancelled", "cancelled");
            return;
        }
        Uri tree = data.getData();
        int takeFlags = data.getFlags()
                & (Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        try {
            getContentResolver().takePersistableUriPermission(tree, takeFlags);
        } catch (SecurityException error) {
            evidence.record("documents_provider", "persist_grant", false,
                    error.toString());
        }
        executor.execute(() -> {
            boolean passed = ProviderContractProbe.runDocumentsProvider(
                    this, tree, evidence);
            setStatus(
                    passed ? "Provider contract probe complete"
                            : "DocumentsProvider contract probe failed",
                    passed ? "complete" : "failed");
        });
    }

    private View createContentView() {
        int padding = Math.round(16 * getResources().getDisplayMetrics().density);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(padding, padding, padding, padding);

        TextView title = new TextView(this);
        title.setText("PathGuard Provider Contract Probe");
        title.setTextSize(20);
        content.addView(title);

        status = new TextView(this);
        status.setText("MediaStore contract probe running");
        status.setPadding(0, padding, 0, padding);
        status.setMinHeight(Math.round(48 * getResources().getDisplayMetrics().density));
        content.addView(status);

        Button selectTree = new Button(this);
        selectTree.setText("Select SAF test directory");
        selectTree.setAllCaps(false);
        selectTree.setMinHeight(Math.round(48 * getResources().getDisplayMetrics().density));
        selectTree.setOnClickListener(this::selectTree);
        content.addView(selectTree);
        return content;
    }

    private void runMediaStoreProbe() {
        try {
            evidence.reset(this);
            boolean passed = ProviderContractProbe.runMediaStore(this, evidence);
            setStatus(
                    passed ? "MediaStore complete; select SAF test directory"
                            : "MediaStore contract probe failed",
                    passed ? "waiting_saf" : "failed");
            if (passed && getIntent().getBooleanExtra(EXTRA_AUTO_SELECT_SAF, false)) {
                runOnUiThread(() -> selectTree(null));
            }
        } catch (IOException error) {
            evidence.record("probe", "initialize", false, error.toString());
            setStatus("Provider contract probe initialization failed", "failed");
        }
    }

    private void selectTree(View ignored) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                        | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                        | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                        | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_TREE);
    }

    private void setStatus(String display, String persisted) {
        try {
            evidence.setStatus(persisted);
        } catch (IOException error) {
            display += " (status write failed)";
        }
        String finalDisplay = display;
        runOnUiThread(() -> status.setText(finalDisplay));
    }
}
