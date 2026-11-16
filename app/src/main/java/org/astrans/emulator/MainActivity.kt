package org.astrans.emulator

import android.app.Activity
import android.os.Bundle
import android.graphics.Color
import android.view.Gravity
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {
    private external fun nativeRunSelfTest(): Int
    private external fun nativeVulkanInfo(): String

    companion object {
        init { System.loadLibrary("astrans_core") }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(28, 28, 28, 28)
            setBackgroundColor(Color.rgb(14, 18, 30))
        }
        fun label(text: String, size: Float, color: Int, bold: Boolean = false) =
            TextView(this).apply {
                this.text = text
                textSize = size
                setTextColor(color)
                gravity = Gravity.CENTER
                setPadding(0, 8, 0, 8)
                if (bold) setTypeface(typeface, android.graphics.Typeface.BOLD)
            }

        root.addView(label("AstraNS", 34f, Color.WHITE, true))
        root.addView(label("Native Android emulator research project", 14f, Color.LTGRAY))
        val result = runCatching { nativeRunSelfTest() }
        val testText = result.fold(
            onSuccess = { value ->
                if (value == 12) "Guest memory + AArch64 execution: PASS (program returned $value)"
                else "Guest execution test: FAIL (result = $value)"
            },
            onFailure = { "Native core error: ${it.message}" }
        )
        root.addView(label(testText, 16f, if (result.getOrNull() == 12) Color.GREEN else Color.YELLOW, true))

        val gpuInfo = runCatching { nativeVulkanInfo() }
            .getOrElse { "Vulkan probe failed: ${it.message}" }
        root.addView(label("GPU / Vulkan diagnostic", 16f, Color.WHITE, true))
        root.addView(label(gpuInfo, 12f, Color.rgb(190, 205, 225)))

        root.addView(label(
            "Milestone 0.5: mapped guest memory, bounded AArch64 execution and Vulkan capability probe. " +
                "This is not yet a Nintendo Switch emulator and cannot run commercial games.",
            13f, Color.LTGRAY
        ))
        root.addView(label(
            "Next: ARM MMU/page tables, SIMD/NEON, Switch system services and a GPU translation backend.",
            12f, Color.rgb(160, 180, 210)
        ))
        setContentView(root)
    }
}
