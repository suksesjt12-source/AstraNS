package org.astrans.emulator

import java.io.IOException
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.CodingErrorAction
import java.nio.charset.StandardCharsets

internal data class Pfs0Entry(
    val name: String,
    val offset: Long,
    val size: Long
)

internal data class Pfs0Report(
    val archiveSize: Long,
    val entryCount: Int,
    val ncaCount: Int,
    val ticketCount: Int,
    val certificateCount: Int,
    val cnmtNcaCount: Int,
    val entries: List<Pfs0Entry>
) {
    fun toDisplayText(): String = buildString {
        appendLine("Format: PFS0 package directory")
        appendLine("Package size: ${formatBytes(archiveSize)}")
        appendLine("Directory entries: $entryCount")
        appendLine("NCA files: $ncaCount")
        appendLine("CNMT NCA candidates: $cnmtNcaCount")
        appendLine("Ticket files: $ticketCount")
        appendLine("Certificate files: $certificateCount")
        appendLine()
        appendLine("Package contents (directory only):")
        entries.forEach { entry ->
            appendLine("• ${entry.name}  —  ${formatBytes(entry.size)}")
        }
        if (entryCount > entries.size) {
            appendLine("… and ${entryCount - entries.size} more entries")
        }
        appendLine()
        append(
            "Inspection only. Encrypted NCA contents, title metadata and game execution " +
                "are not implemented. No package payload was extracted or decrypted."
        )
    }

    private fun formatBytes(value: Long): String = when {
        value >= 1024L * 1024L * 1024L ->
            String.format(java.util.Locale.US, "%.2f GiB", value / (1024.0 * 1024.0 * 1024.0))
        value >= 1024L * 1024L ->
            String.format(java.util.Locale.US, "%.1f MiB", value / (1024.0 * 1024.0))
        value >= 1024L ->
            String.format(java.util.Locale.US, "%.1f KiB", value / 1024.0)
        else -> "$value B"
    }
}

/**
 * Performs bounded, read-only validation of the PFS0 directory at the start of an NSP.
 * It deliberately does not inspect encrypted NCA payloads or extract package contents.
 */
internal object Pfs0Inspector {
    private const val HEADER_SIZE = 0x10
    private const val ENTRY_SIZE = 0x18
    private const val MAX_ENTRY_COUNT = 100_000L
    private const val MAX_STRING_TABLE_SIZE = 16L * 1024L * 1024L
    private const val MAX_DIRECTORY_SIZE = 32L * 1024L * 1024L
    private const val PREVIEW_LIMIT = 30

    @Throws(IOException::class)
    fun inspect(input: InputStream, archiveSize: Long): Pfs0Report {
        if (archiveSize < HEADER_SIZE) {
            throw IOException("NSP is too small to contain a PFS0 header.")
        }

        val header = readExact(input, HEADER_SIZE)
        if (!header.copyOfRange(0, 4).contentEquals(byteArrayOf(0x50, 0x46, 0x53, 0x30))) {
            throw IOException("Invalid NSP directory: PFS0 signature was not found.")
        }

        val headerBuffer = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN)
        headerBuffer.position(4)
        val entryCountLong = readU32(headerBuffer)
        val stringTableSizeLong = readU32(headerBuffer)
        headerBuffer.int // Reserved field.

        if (entryCountLong == 0L || entryCountLong > MAX_ENTRY_COUNT) {
            throw IOException("Invalid PFS0 directory entry count: $entryCountLong.")
        }
        if (stringTableSizeLong == 0L || stringTableSizeLong > MAX_STRING_TABLE_SIZE) {
            throw IOException("Invalid PFS0 string table size: $stringTableSizeLong bytes.")
        }

        val entriesSize = entryCountLong * ENTRY_SIZE
        val directorySize = HEADER_SIZE.toLong() + entriesSize + stringTableSizeLong
        if (directorySize > MAX_DIRECTORY_SIZE || directorySize > archiveSize) {
            throw IOException("PFS0 directory exceeds the allowed size or the package boundary.")
        }

        val rawEntries = readExact(input, entriesSize.toInt())
        val stringTable = readExact(input, stringTableSizeLong.toInt())
        val entriesBuffer = ByteBuffer.wrap(rawEntries).order(ByteOrder.LITTLE_ENDIAN)
        val dataSize = archiveSize - directorySize
        val preview = ArrayList<Pfs0Entry>(minOf(entryCountLong.toInt(), PREVIEW_LIMIT))
        var ncaCount = 0
        var ticketCount = 0
        var certificateCount = 0
        var cnmtNcaCount = 0

        repeat(entryCountLong.toInt()) { index ->
            val offset = readU64(entriesBuffer, "entry $index offset")
            val size = readU64(entriesBuffer, "entry $index size")
            val nameOffsetLong = readU32(entriesBuffer)
            entriesBuffer.int // Reserved field.

            if (offset > dataSize || size > dataSize - offset) {
                throw IOException("PFS0 entry $index points outside the package data region.")
            }
            if (nameOffsetLong >= stringTable.size.toLong()) {
                throw IOException("PFS0 entry $index has an invalid filename offset.")
            }

            val name = decodeName(stringTable, nameOffsetLong.toInt(), index)
            val lowerName = name.lowercase(java.util.Locale.ROOT)
            when {
                lowerName.endsWith(".cnmt.nca") -> {
                    ncaCount++
                    cnmtNcaCount++
                }
                lowerName.endsWith(".nca") -> ncaCount++
                lowerName.endsWith(".tik") -> ticketCount++
                lowerName.endsWith(".cert") -> certificateCount++
            }
            if (preview.size < PREVIEW_LIMIT) {
                preview.add(Pfs0Entry(name, offset, size))
            }
        }

        return Pfs0Report(
            archiveSize = archiveSize,
            entryCount = entryCountLong.toInt(),
            ncaCount = ncaCount,
            ticketCount = ticketCount,
            certificateCount = certificateCount,
            cnmtNcaCount = cnmtNcaCount,
            entries = preview
        )
    }

    private fun readExact(input: InputStream, size: Int): ByteArray {
        val result = ByteArray(size)
        var position = 0
        while (position < size) {
            val count = input.read(result, position, size - position)
            if (count < 0) throw IOException("Truncated PFS0 directory.")
            if (count == 0) {
                val single = input.read()
                if (single < 0) throw IOException("Truncated PFS0 directory.")
                result[position++] = single.toByte()
            } else {
                position += count
            }
        }
        return result
    }

    private fun readU32(buffer: ByteBuffer): Long =
        buffer.int.toLong() and 0xFFFF_FFFFL

    private fun readU64(buffer: ByteBuffer, field: String): Long {
        val value = buffer.long
        if (value < 0L) throw IOException("PFS0 $field exceeds the supported signed 64-bit range.")
        return value
    }

    private fun decodeName(table: ByteArray, start: Int, index: Int): String {
        var end = start
        while (end < table.size && table[end].toInt() != 0) end++
        if (end == start || end == table.size) {
            throw IOException("PFS0 entry $index has an empty or unterminated filename.")
        }
        return try {
            StandardCharsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(table, start, end - start))
                .toString()
        } catch (_: Exception) {
            throw IOException("PFS0 entry $index has an invalid UTF-8 filename.")
        }
    }
}
