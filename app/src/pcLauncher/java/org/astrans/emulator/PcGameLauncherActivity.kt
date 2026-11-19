package org.astrans.emulator

import android.app.Activity
import android.app.AlertDialog
import android.app.ProgressDialog
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.io.BufferedInputStream
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipInputStream

/** ZIP-import milestone. Windows execution requires a separately integrated Wine/Box64 runtime. */
class PcGameLauncherActivity : Activity() {
    private lateinit var statusText: TextView
    private lateinit var gamesContainer: LinearLayout
    private val prefs by lazy { getSharedPreferences("pc_launcher", MODE_PRIVATE) }
    private val importedGames = linkedSetOf<String>()

    companion object {
        private const val PICK_ARCHIVE = 5101
        private const val MAX_ENTRIES = 100_000
        private const val MAX_UNPACKED_BYTES = 25L * 1024L * 1024L * 1024L
        private const val BUFFER_SIZE = 64 * 1024
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        importedGames.addAll(prefs.getStringSet("imported_games", emptySet()) ?: emptySet())
        buildUi()
        renderGames()
    }

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(18), dp(18), dp(18), dp(14))
            setBackgroundColor(Color.rgb(10, 15, 27))
        }
        root.addView(text("AstraNS", 25f, Color.WHITE, true))
        root.addView(text("PC GAME LAUNCHER", 11f, Color.rgb(105, 151, 255), true))
        root.addView(text(
            "Import arsip ZIP game Windows milikmu. Launcher akan mengekstrak file dan mencari executable game.",
            14f, Color.rgb(185, 197, 218), false
        ).apply { setPadding(0, dp(12), 0, dp(16)) })
        root.addView(Button(this).apply {
            text = "Pilih ZIP game"
            setOnClickListener { pickArchive() }
        }, LinearLayout.LayoutParams(-1, -2))

        statusText = text("Belum ada game yang diimpor.", 13f, Color.rgb(242, 190, 105), false)
        root.addView(statusText.apply { setPadding(0, dp(14), 0, dp(10)) })
        root.addView(text("GAME LIBRARY", 11f, Color.rgb(105, 151, 255), true)
            .apply { setPadding(0, dp(10), 0, dp(8)) })

        val scroll = ScrollView(this)
        gamesContainer = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        scroll.addView(gamesContainer)
        root.addView(scroll, LinearLayout.LayoutParams(-1, 0, 1f))
        root.addView(text(
            "STATUS: Import ZIP tersedia. Eksekusi EXE belum aktif. Tahap berikutnya memerlukan integrasi Wine + Box64 dan backend grafis.",
            12f, Color.rgb(242, 190, 105), false
        ).apply { setPadding(0, dp(12), 0, 0) })
        setContentView(root)
    }

    private fun pickArchive() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream"))
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        try {
            startActivityForResult(intent, PICK_ARCHIVE)
        } catch (_: Exception) {
            Toast.makeText(this, "Pemilih file tidak tersedia.", Toast.LENGTH_LONG).show()
        }
    }

    @Deprecated("Uses Android document picker for broad compatibility.")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != PICK_ARCHIVE || resultCode != RESULT_OK) return
        data?.data?.let { extractArchive(it) }
    }

    private fun extractArchive(uri: Uri) {
        val base = getExternalFilesDir("pc-games") ?: File(filesDir, "pc-games")
        if (!base.exists() && !base.mkdirs()) {
            statusText.text = "Gagal membuat folder penyimpanan game."
            return
        }
        val displayName = queryDisplayName(uri) ?: "windows-game.zip"
        val safeName = displayName.substringBeforeLast('.', displayName)
            .replace(Regex("[^A-Za-z0-9._-]"), "_")
            .trim('.', '_').take(48).ifBlank { "windows-game" }
        val target = File(base, safeName + "_" + System.currentTimeMillis())
        val staging = File(base, target.name + ".partial")
        val progress = ProgressDialog(this).apply {
            setTitle("Mengimpor game")
            setMessage("Mengekstrak arsip ZIP…")
            setCancelable(false)
            show()
        }

        Thread {
            val result = try {
                if (!staging.mkdirs()) throw IllegalStateException("Tidak dapat membuat folder sementara.")
                val exeFound = extractZip(uri, staging)
                if (!staging.renameTo(target)) throw IllegalStateException("Tidak dapat menyelesaikan folder hasil ekstraksi.")
                val record = target.absolutePath + "|" + if (exeFound) "exe" else "no-exe"
                importedGames.add(record)
                prefs.edit().putStringSet("imported_games", importedGames.toSet()).apply()
                if (exeFound) "Selesai. NSUNSR.exe ditemukan. File siap untuk tahap integrasi runtime Windows."
                else "Arsip berhasil diekstrak, tetapi NSUNSR.exe tidak ditemukan. Periksa struktur folder ZIP."
            } catch (error: Exception) {
                staging.deleteRecursively()
                "Impor gagal: " + (error.message ?: error.javaClass.simpleName)
            }
            runOnUiThread {
                progress.dismiss()
                statusText.text = result
                renderGames()
                AlertDialog.Builder(this).setTitle("Hasil impor").setMessage(result)
                    .setPositiveButton("OK", null).show()
            }
        }.start()
    }

    private fun extractZip(uri: Uri, destination: File): Boolean {
        val rootPath = destination.canonicalPath + File.separator
        val buffer = ByteArray(BUFFER_SIZE)
        var entryCount = 0
        var totalBytes = 0L
        var exeFound = false
        val input = contentResolver.openInputStream(uri)
            ?: throw IllegalStateException("Android tidak dapat membaca arsip.")

        ZipInputStream(BufferedInputStream(input)).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                entryCount++
                if (entryCount > MAX_ENTRIES) throw IllegalStateException("Jumlah file ZIP melewati batas keamanan.")
                val normalized = entry.name.replace('\\\\', '/')
                if (normalized.startsWith("/") || normalized.split('/').any { it == ".." } ||
                    Regex("^[A-Za-z]:").containsMatchIn(normalized)) {
                    throw IllegalStateException("Arsip berisi jalur file yang tidak aman.")
                }
                val outputFile = File(destination, normalized).canonicalFile
                if (!outputFile.path.startsWith(rootPath)) {
                    throw IllegalStateException("Jalur ekstraksi keluar dari folder tujuan.")
                }
                if (entry.isDirectory) {
                    if (!outputFile.exists() && !outputFile.mkdirs()) {
                        throw IllegalStateException("Gagal membuat folder arsip.")
                    }
                } else {
                    val parent = outputFile.parentFile ?: throw IllegalStateException("Jalur ZIP tidak valid.")
                    if (!parent.exists() && !parent.mkdirs()) throw IllegalStateException("Gagal membuat subfolder.")
                    FileOutputStream(outputFile).use { output ->
                        while (true) {
                            val count = zip.read(buffer)
                            if (count < 0) break
                            totalBytes += count.toLong()
                            if (totalBytes > MAX_UNPACKED_BYTES) {
                                throw IllegalStateException("Ukuran hasil ekstraksi melewati batas 25 GiB.")
                            }
                            output.write(buffer, 0, count)
                        }
                    }
                    if (outputFile.name.equals("NSUNSR.exe", ignoreCase = true)) exeFound = true
                }
                zip.closeEntry()
            }
        }
        return exeFound
    }

    private fun renderGames() {
        gamesContainer.removeAllViews()
        if (importedGames.isEmpty()) {
            gamesContainer.addView(text("Belum ada arsip game.", 14f, Color.LTGRAY, false))
            return
        }
        importedGames.toList().forEach { record ->
            val parts = record.split('|', limit = 2)
            val folder = File(parts[0])
            val hasExe = parts.getOrNull(1) == "exe"
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(dp(13), dp(12), dp(13), dp(12))
                setBackgroundColor(Color.rgb(24, 34, 52))
            }
            row.addView(text(folder.name, 15f, Color.WHITE, true))
            row.addView(text(
                if (hasExe) "NSUNSR.exe ditemukan" else "Executable belum ditemukan",
                12f, if (hasExe) Color.rgb(115, 205, 160) else Color.rgb(242, 190, 105), false
            ).apply { setPadding(0, dp(5), 0, dp(8)) })
            row.addView(Button(this).apply {
                text = "Hapus entri"
                setOnClickListener {
                    importedGames.remove(record)
                    prefs.edit().putStringSet("imported_games", importedGames.toSet()).apply()
                    renderGames()
                }
            })
            gamesContainer.addView(row, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(9) })
        }
    }

    private fun queryDisplayName(uri: Uri): String? = try {
        contentResolver.query(uri, arrayOf("_display_name"), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) cursor.getString(0) else null
        }
    } catch (_: Exception) { null }

    private fun text(value: String, size: Float, color: Int, bold: Boolean): TextView =
        TextView(this).apply {
            text = value
            textSize = size
            setTextColor(color)
            if (bold) setTypeface(typeface, android.graphics.Typeface.BOLD)
        }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()
}
