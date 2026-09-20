package dev.pathguard.hideprobe

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.Environment
import android.os.Process
import android.provider.Settings
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.io.File
import java.nio.file.Files
import java.util.concurrent.Executors
import java.util.concurrent.Future

class ProbeActivity : Activity() {
    companion object { const val EXTRA_OBSERVE_PATHS = "observe_paths" }
    private val executor = Executors.newSingleThreadExecutor()
    private val probeRunGate = ProbeRunGate()
    private var probeFuture: Future<*>? = null
    private lateinit var statusView: TextView
    private lateinit var permissionView: TextView

    override fun onCreate(state: Bundle?) {
        super.onCreate(state); setContentView(contentView())
        if (state == null) runCatching { SelectorProbe.reset(this) }
        startProbeFromIntent()
    }
    override fun onNewIntent(newIntent: Intent) {
        super.onNewIntent(newIntent); setIntent(newIntent); startProbeFromIntent()
    }
    override fun onDestroy() { probeRunGate.invalidate(); executor.shutdownNow(); super.onDestroy() }
    override fun onResume() { super.onResume(); if (::permissionView.isInitialized) permissionView.text = "role=${BuildConfig.OBSERVER_ROLE} | media=${permissionState(Manifest.permission.READ_MEDIA_IMAGES)} | all-files=${Environment.isExternalStorageManager()}" }
    override fun onActivityResult(code: Int, result: Int, data: Intent?) { super.onActivityResult(code, result, data); executor.execute { SelectorProbe.recordResult(this, code, result, data) } }

    private fun contentView(): View {
        val padding = (16 * resources.displayMetrics.density).toInt(); val content = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(padding, padding, padding, padding) }
        content.addView(TextView(this).apply { text = "PathGuard HideLab ${BuildConfig.OBSERVER_ROLE}"; textSize = 20f })
        permissionView = TextView(this); content.addView(permissionView)
        statusView = TextView(this).apply { text = "HideLab probe running"; minHeight = (48 * resources.displayMetrics.density).toInt() }; content.addView(statusView)
        addButton(content, "Photo Picker") { startActivityForResult(SelectorProbe.photoPickerIntent(), SelectorProbe.REQUEST_PHOTO_PICKER) }
        addButton(content, "SAF image") { startActivityForResult(SelectorProbe.openImageIntent(), SelectorProbe.REQUEST_OPEN_IMAGE) }
        addButton(content, "SAF Pictures") { startActivityForResult(SelectorProbe.picturesTreeIntent(), SelectorProbe.REQUEST_PICTURES_TREE) }
        addButton(content, "SAF DCIM") { startActivityForResult(SelectorProbe.dcimTreeIntent(), SelectorProbe.REQUEST_DCIM_TREE) }
        addButton(content, "All files settings") { startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).setData(android.net.Uri.parse("package:$packageName"))) }
        return ScrollView(this).apply { addView(content) }
    }
    private fun addButton(parent: LinearLayout, label: String, action: () -> Unit) { parent.addView(Button(this).apply { text = label; isAllCaps = false; minHeight = (48 * resources.displayMetrics.density).toInt(); setOnClickListener { action() } }) }
    private fun permissionState(permission: String) = if (checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED) "granted" else "denied"
    private fun startProbeFromIntent() {
        val paths = intent.getStringArrayExtra(EXTRA_OBSERVE_PATHS)?.takeIf { it.isNotEmpty() }
            ?: arrayOf("/storage/emulated/0/Pictures/Nagram", "/storage/emulated/0/DCIM/Screenshots")
        val attackMutations = intent.getBooleanExtra("attack_mutations", false)
        val scenario = intent.getStringExtra("scenario") ?: "baseline"
        val runId = intent.getStringExtra("run_id") ?: ""
        val runToken = probeRunGate.begin()
        File(filesDir, "hide-h0").mkdirs()
        File(filesDir, "hide-h0/status").writeText("running:$runToken:$runId")
        probeFuture?.cancel(true)
        probeFuture = executor.submit { runProbe(runToken, paths, scenario, attackMutations, runId) }
    }
    private fun runProbe(runToken: Long, paths: Array<String>, scenario: String, attackMutations: Boolean, runId: String) {
        val dir = File(filesDir, "hide-h0"); dir.mkdirs(); val status = File(dir, "status")
        runCatching {
            status.writeText("running"); val aliases = StoragePath.expandVfsAliases(paths); val nativeSandbox = File(noBackupFilesDir, "pathguard-hide-h0-native-${System.nanoTime()}").apply { mkdirs() }
            val output = JavaVfsProbe.run(this, aliases, attackMutations) + (NativeProbe.run(nativeSandbox.canonicalPath, aliases, attackMutations, scenario) ?: error("native probe returned null")) + MediaStoreProbe.run(this, paths)
            if (!probeRunGate.isCurrent(runToken)) return@runCatching
            File(dir, "observations.jsonl").writeText(output); File(dir, "metadata.json").writeText(metadata(paths, aliases, scenario, attackMutations, runId)); status.writeText("complete"); runOnUiThread { statusView.text = "HideLab complete: ${BuildConfig.OBSERVER_ROLE}" }
        }.onFailure { error ->
            if (probeRunGate.isCurrent(runToken)) {
                status.writeText("failed: $error")
                runOnUiThread { statusView.text = "HideLab failed: $error" }
            }
        }
    }
    private fun metadata(paths: Array<String>, aliases: Array<String>, scenario: String, attackMutations: Boolean, runId: String): String {
        val ns = runCatching { Files.readSymbolicLink(File("/proc/self/ns/mnt").toPath()).toString() }.getOrDefault("")
        return "{\"schema\":2,\"kind\":\"hidelab_run\",\"run_id\":\"${JsonObservation.escape(runId)}\",\"scenario\":\"${JsonObservation.escape(scenario)}\",\"attack_mutations\":$attackMutations,\"observer\":\"${BuildConfig.OBSERVER_ROLE}\",\"package\":\"$packageName\",\"uid\":${Process.myUid()},\"sdk\":${android.os.Build.VERSION.SDK_INT},\"fingerprint\":\"${JsonObservation.escape(android.os.Build.FINGERPRINT)}\",\"mount_namespace\":\"${JsonObservation.escape(ns)}\",\"observe_paths\":${JsonObservation.renderStringArray(paths)},\"vfs_observe_paths\":${JsonObservation.renderStringArray(aliases)}}"
    }
}
