package org.astrans.emulator

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.res.ColorStateList
import android.database.Cursor
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.view.Gravity
import android.view.View
import android.widget.*
import java.util.Locale

class MainActivity : Activity() {
    private lateinit var contentHost: FrameLayout
    private lateinit var libraryTab: TextView
    private lateinit var settingsTab: TextView
    private val gameUris = linkedSetOf<String>()
    private val prefs by lazy { getSharedPreferences("astrans_ui", MODE_PRIVATE) }
    private var currentTab = "library"

    private val bg = Color.rgb(10, 15, 27)
    private val panel = Color.rgb(20, 28, 44)
    private val raised = Color.rgb(29, 41, 62)
    private val blue = Color.rgb(105, 151, 255)
    private val white = Color.rgb(240, 244, 255)
    private val muted = Color.rgb(155, 170, 195)
    private val green = Color.rgb(115, 205, 160)
    private val amber = Color.rgb(242, 190, 105)

    private external fun nativeRunSelfTest(): Int
    private external fun nativeVulkanInfo(): String

    companion object {
        private const val PICK_GAME = 4102
        init { System.loadLibrary("astrans_core") }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = bg
        window.navigationBarColor = bg
        gameUris.addAll(prefs.getStringSet("game_uris", emptySet()) ?: emptySet())
        buildShell()
        showLibrary()
    }

    private fun buildShell() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(bg)
        }
        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(20), dp(15), dp(20), dp(12))
        }
        header.addView(label("A", 20f, Color.WHITE, true).apply {
            gravity = Gravity.CENTER
            background = rounded(blue, 13)
        }, LinearLayout.LayoutParams(dp(42), dp(42)))
        val brand = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(12), 0, 0, 0)
        }
        brand.addView(label("AstraNS", 20f, white, true))
        brand.addView(label("SWITCH EMULATOR WORKSPACE", 9f, muted, true))
        header.addView(brand, LinearLayout.LayoutParams(0, -2, 1f))
        header.addView(label("0.7", 11f, muted, true).apply {
            background = rounded(raised, 18)
            setPadding(dp(12), dp(7), dp(12), dp(7))
        })
        root.addView(header)
        contentHost = FrameLayout(this)
        root.addView(contentHost, LinearLayout.LayoutParams(-1, 0, 1f))

        val tabs = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            background = rounded(panel, 18)
            setPadding(dp(8), dp(7), dp(8), dp(7))
        }
        libraryTab = navLabel("▦\nLibrary")
        settingsTab = navLabel("⚙\nSettings")
        libraryTab.setOnClickListener { showLibrary() }
        settingsTab.setOnClickListener { showSettings() }
        tabs.addView(libraryTab, LinearLayout.LayoutParams(0, dp(55), 1f))
        tabs.addView(settingsTab, LinearLayout.LayoutParams(0, dp(55), 1f))
        root.addView(LinearLayout(this).apply {
            setPadding(dp(12), dp(6), dp(12), dp(8))
            addView(tabs, LinearLayout.LayoutParams(-1, -2))
        })
        setContentView(root)
    }

    private fun showLibrary() {
        currentTab = "library"
        updateTabs()
        val page = column()
        page.addView(titleBlock("Game library", "Manage the game files you have imported."))
        val hero = card(raised)
        hero.addView(label("YOUR COLLECTION", 10f, blue, true))
        hero.addView(label("All your games, in one place", 21f, white, true).apply {
            setPadding(0, dp(8), 0, dp(5))
        })
        hero.addView(label("Import a game file to add it to your personal library.", 13f, muted))
        val stats = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(16), 0, 0)
        }
        stats.addView(stat(gameUris.size.toString(), "GAMES ADDED"), LinearLayout.LayoutParams(0, -2, 1f))
        stats.addView(stat("ARM64", "ANDROID BUILD"), LinearLayout.LayoutParams(0, -2, 1f))
        hero.addView(stats)
        page.addView(hero)

        val import = action("+   Import game file", blue, Color.WHITE)
        import.setOnClickListener { pickGameFile() }
        page.addView(import, margins(top = 15, bottom = 12))
        page.addView(label("MY GAMES", 11f, muted, true).apply {
            setPadding(dp(2), dp(12), 0, dp(8))
        })

        if (gameUris.isEmpty()) {
            val empty = card(panel)
            empty.addView(label("＋", 34f, blue, true).apply {
                gravity = Gravity.CENTER
                background = rounded(raised, 16)
            }, LinearLayout.LayoutParams(dp(60), dp(60)))
            empty.addView(label("No games imported yet", 17f, white, true).apply {
                setPadding(0, dp(13), 0, dp(5))
            })
            empty.addView(label("Tap Import game file to select a game from your device.", 13f, muted))
            page.addView(empty)
        } else {
            gameUris.toList().forEach { raw ->
                val uri = Uri.parse(raw)
                page.addView(gameRow(uri), margins(bottom = 10))
            }
        }
        page.addView(label(
            "CORE STATUS  •  Game loading and rendering are still being developed. Imported files are catalogued, not launched.",
            11f, amber
        ).apply { setPadding(dp(3), dp(16), dp(3), dp(22)) })
        setPage(page)
    }

    private fun gameRow(uri: Uri): View {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            background = rounded(panel, 17)
            setPadding(dp(12), dp(12), dp(12), dp(12))
            isClickable = true
            isFocusable = true
        }
        row.addView(label("NS", 17f, Color.WHITE, true).apply {
            gravity = Gravity.CENTER
            background = rounded(Color.rgb(76, 94, 149), 12)
        }, LinearLayout.LayoutParams(dp(56), dp(65)))
        val details = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(12), 0, dp(6), 0)
        }
        val name = fileName(uri)
        details.addView(label(name, 14f, white, true))
        details.addView(label(
            name.substringAfterLast('.', "FILE").uppercase(Locale.ROOT) + "  •  " + fileSize(uri),
            11f, muted
        ).apply { setPadding(0, dp(4), 0, dp(5)) })
        details.addView(label("IMPORTED  ·  NOT PLAYABLE YET", 9f, amber, true))
        row.addView(details, LinearLayout.LayoutParams(0, -2, 1f))
        row.addView(label("›", 25f, muted, true))
        row.setOnClickListener { gameInfo(uri) }
        row.setOnLongClickListener {
            AlertDialog.Builder(this)
                .setTitle("Remove game entry?")
                .setMessage("The original file on your device will not be deleted.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Remove") { _, _ -> removeGame(uri) }
                .show()
            true
        }
        return row
    }

    private fun gameInfo(uri: Uri) {
        val name = fileName(uri)
        AlertDialog.Builder(this)
            .setTitle(name)
            .setMessage(
                "File: " + name + "\nSize: " + fileSize(uri) +
                "\n\nThis file is saved in your AstraNS library. The current core does not yet " +
                "include the Switch title loader, Horizon system services or graphics renderer. " +
                "It cannot launch this game yet."
            )
            .setPositiveButton("OK", null)
            .setNeutralButton("Remove") { _, _ -> removeGame(uri) }
            .show()
    }

    private fun pickGameFile() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        }
        try {
            startActivityForResult(intent, PICK_GAME)
        } catch (_: Exception) {
            Toast.makeText(this, "File picker unavailable.", Toast.LENGTH_LONG).show()
        }
    }

    @Deprecated("Platform document picker is used for broad Android compatibility.")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != PICK_GAME || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        try {
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (_: SecurityException) {
            // A provider may grant temporary rather than persistable access.
        }
        gameUris.add(uri.toString())
        saveLibrary()
        showLibrary()
        Toast.makeText(this, "Game added to library.", Toast.LENGTH_SHORT).show()
    }

    private fun removeGame(uri: Uri) {
        gameUris.remove(uri.toString())
        saveLibrary()
        showLibrary()
    }

    private fun saveLibrary() {
        prefs.edit().putStringSet("game_uris", gameUris.toSet()).apply()
    }

    private fun fileName(uri: Uri): String {
        var name = uri.lastPathSegment ?: "Game file"
        var cursor: Cursor? = null
        try {
            cursor = contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            if (cursor != null && cursor.moveToFirst()) {
                val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (index >= 0) name = cursor.getString(index) ?: name
            }
        } catch (_: Exception) {
            // Keep fallback filename.
        } finally {
            cursor?.close()
        }
        return name
    }

    private fun fileSize(uri: Uri): String {
        var cursor: Cursor? = null
        try {
            cursor = contentResolver.query(uri, arrayOf(OpenableColumns.SIZE), null, null, null)
            if (cursor != null && cursor.moveToFirst()) {
                val index = cursor.getColumnIndex(OpenableColumns.SIZE)
                if (index >= 0 && !cursor.isNull(index)) {
                    val bytes = cursor.getLong(index)
                    if (bytes >= 1024L * 1024L * 1024L) return String.format(Locale.US, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0))
                    if (bytes >= 1024L * 1024L) return String.format(Locale.US, "%.1f MB", bytes / (1024.0 * 1024.0))
                    return bytes.toString() + " B"
                }
            }
        } catch (_: Exception) {
            // File size metadata is optional.
        } finally {
            cursor?.close()
        }
        return "Size unknown"
    }

    private fun showSettings() {
        currentTab = "settings"
        updateTabs()
        val page = column()
        page.addView(titleBlock("Settings", "Preferences and device diagnostics."))
        val graphics = card(panel)
        graphics.addView(label("GRAPHICS", 10f, blue, true))
        graphics.addView(label("Graphics profile", 16f, white, true).apply {
            setPadding(0, dp(9), 0, dp(4))
        })
        graphics.addView(label(
            "Saved preference. It will control rendering after the GPU backend is connected.",
            12f, muted
        ))
        val profiles = arrayOf("Performance", "Balanced", "Quality")
        val selected = prefs.getString("graphics_profile", "Balanced") ?: "Balanced"
        val pickProfile = action("Profile: " + selected + "   ▾", raised, white)
        pickProfile.setOnClickListener {
            AlertDialog.Builder(this)
                .setTitle("Graphics profile")
                .setSingleChoiceItems(profiles, profiles.indexOf(selected).coerceAtLeast(0)) { dialog, which ->
                    prefs.edit().putString("graphics_profile", profiles[which]).apply()
                    dialog.dismiss()
                    showSettings()
                }
                .setNegativeButton("Cancel", null)
                .show()
        }
        graphics.addView(pickProfile, margins(top = 12))
        page.addView(graphics)

        val options = card(panel)
        options.addView(label("EMULATION OPTIONS", 10f, blue, true))
        addToggle(options, "VSync", "Synchronize frames when supported.", "vsync")
        divider(options)
        addToggle(options, "FPS counter", "Show frame rate when a renderer exists.", "fps")
        divider(options)
        addToggle(options, "Audio output", "Enable sound after the audio system is added.", "audio")
        page.addView(options, margins(top = 13))

        val diag = card(panel)
        diag.addView(label("DEVICE DIAGNOSTICS", 10f, blue, true))
        val test = runCatching { nativeRunSelfTest() }.getOrNull()
        diag.addView(label(
            if (test == 12) "Native CPU + memory tests: PASS" else "Native tests unavailable",
            14f, if (test == 12) green else amber, true
        ).apply { setPadding(0, dp(10), 0, dp(8)) })
        diag.addView(label("Vulkan capability probe", 14f, white, true))
        diag.addView(label(
            runCatching { nativeVulkanInfo() }.getOrElse { "Vulkan probe unavailable: " + it.message },
            11f, muted
        ).apply { setPadding(0, dp(6), 0, 0) })
        page.addView(diag, margins(top = 13))
        page.addView(label(
            "Not all settings are connected to game execution yet. The emulator core is still under development.",
            11f, amber
        ).apply { setPadding(dp(3), dp(15), dp(3), dp(22)) })
        setPage(page)
    }

    private fun addToggle(parent: LinearLayout, title: String, subtitle: String, key: String) {
        val line = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(9), 0, dp(9))
        }
        val copy = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        copy.addView(label(title, 14f, white, true))
        copy.addView(label(subtitle, 11f, muted).apply { setPadding(0, dp(3), dp(5), 0) })
        line.addView(copy, LinearLayout.LayoutParams(0, -2, 1f))
        val toggle = Switch(this).apply {
            isChecked = prefs.getBoolean(key, true)
            thumbTintList = ColorStateList.valueOf(if (isChecked) blue else muted)
            setOnCheckedChangeListener { _, checked ->
                prefs.edit().putBoolean(key, checked).apply()
                thumbTintList = ColorStateList.valueOf(if (checked) blue else muted)
            }
        }
        line.addView(toggle)
        parent.addView(line)
    }

    private fun titleBlock(title: String, subtitle: String): View {
        val box = column()
        box.addView(label(title, 27f, white, true))
        box.addView(label(subtitle, 13f, muted).apply { setPadding(0, dp(4), 0, dp(14)) })
        return box
    }

    private fun stat(value: String, caption: String): View {
        val box = column()
        box.addView(label(value, 18f, white, true))
        box.addView(label(caption, 9f, muted, true).apply { setPadding(0, dp(2), 0, 0) })
        return box
    }

    private fun column() = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(18), dp(9), dp(18), dp(12))
    }

    private fun card(color: Int) = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        background = rounded(color, 18)
        setPadding(dp(17), dp(17), dp(17), dp(17))
    }

    private fun setPage(page: LinearLayout) {
        val scroll = ScrollView(this).apply {
            isFillViewport = true
            addView(page)
        }
        contentHost.removeAllViews()
        contentHost.addView(scroll, FrameLayout.LayoutParams(-1, -1))
    }

    private fun updateTabs() {
        libraryTab.setTextColor(if (currentTab == "library") blue else muted)
        settingsTab.setTextColor(if (currentTab == "settings") blue else muted)
        libraryTab.background = rounded(if (currentTab == "library") raised else Color.TRANSPARENT, 13)
        settingsTab.background = rounded(if (currentTab == "settings") raised else Color.TRANSPARENT, 13)
    }

    private fun navLabel(value: String) = label(value, 12f, muted, true).apply {
        gravity = Gravity.CENTER
        isClickable = true
        isFocusable = true
    }

    private fun action(value: String, color: Int, foreground: Int) =
        label(value, 14f, foreground, true).apply {
            gravity = Gravity.CENTER
            background = rounded(color, 14)
            setPadding(dp(15), dp(14), dp(15), dp(14))
            isClickable = true
            isFocusable = true
        }

    private fun label(value: String, size: Float, color: Int, bold: Boolean = false) =
        TextView(this).apply {
            text = value
            textSize = size
            setTextColor(color)
            if (bold) setTypeface(typeface, Typeface.BOLD)
            gravity = Gravity.CENTER_VERTICAL
        }

    private fun divider(parent: LinearLayout) {
        parent.addView(View(this).apply { setBackgroundColor(Color.rgb(48, 60, 80)) },
            LinearLayout.LayoutParams(-1, dp(1)))
    }

    private fun margins(top: Int = 0, bottom: Int = 0) =
        LinearLayout.LayoutParams(-1, -2).apply {
            topMargin = dp(top)
            bottomMargin = dp(bottom)
        }

    private fun rounded(color: Int, radius: Int) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = dp(radius).toFloat()
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()
}
