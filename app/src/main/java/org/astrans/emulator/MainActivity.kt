package org.astrans.emulator

import android.app.Activity
import android.os.Bundle
import android.graphics.Color
import android.view.Gravity
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {
    private external fun nativeRunSelfTest(): Int

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
                if (bold) setTypeface(typeface, android.graphics.Typeface.BOLD)
                setPadding(0, 10, 0, 10)
            }

        root.addView(label("AstraNS", 34f, Color.WHITE, true))
        root.addView(label("Native Android emulator research project", 14f, Color.LTGRAY))
        val result = runCatching { nativeRunSelfTest() }
        val testText = result.fold(
            onSuccess = { value ->
                if (value == 12) "ARM64 subset test: PASS (result = $value)"
                else "ARM64 subset test: FAIL (result = $value)"
            },
            onFailure = { "Native core error: ${it.message}" }
        )
        root.addView(label(testText, 16f, if (result.getOrNull() == 12) Color.GREEN else Color.YELLOW, true))
        root.addView(label(
            "Milestone 0.1: Android shell + tiny AArch64 instruction interpreter. " +
                "This is not yet a Nintendo Switch emulator and cannot run commercial games.",
            14f, Color.LTGRAY
        ))
        root.addView(label(
            "Next: memory model, instruction coverage, GPU command translation, audio and system services.",
            13f, Color.rgb(160, 180, 210)
        ))
        setContentView(root)
    }
}
