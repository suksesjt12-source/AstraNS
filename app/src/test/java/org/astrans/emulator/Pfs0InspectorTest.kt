package org.astrans.emulator

import java.io.ByteArrayInputStream
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Pfs0InspectorTest {
    @Test
    fun parsesDirectoryEntriesAndClassifiesCommonExtensions() {
        val packageBytes = createPackage(
            listOf(
                "00000000000000000000000000000001.cnmt.nca",
                "00000000000000000000000000000002.nca",
                "title.tik",
                "title.cert"
            ),
            listOf(40L, 80L, 12L, 24L)
        )

        val report = Pfs0Inspector.inspect(
            ByteArrayInputStream(packageBytes),
            packageBytes.size.toLong()
        )

        assertEquals(4, report.entryCount)
        assertEquals(2, report.ncaCount)
        assertEquals(1, report.cnmtNcaCount)
        assertEquals(1, report.ticketCount)
        assertEquals(1, report.certificateCount)
        assertEquals(4, report.entries.size)
        assertTrue(report.toDisplayText().contains("Inspection only"))
    }

    @Test(expected = IOException::class)
    fun rejectsInvalidPfs0Magic() {
        val bytes = ByteArray(64)
        Pfs0Inspector.inspect(ByteArrayInputStream(bytes), bytes.size.toLong())
    }

    @Test(expected = IOException::class)
    fun rejectsEntryThatExceedsPackageDataRegion() {
        val bytes = createPackage(listOf("broken.nca"), listOf(4096L))
        Pfs0Inspector.inspect(ByteArrayInputStream(bytes), bytes.size.toLong())
    }

    @Test(expected = IOException::class)
    fun rejectsTruncatedDirectory() {
        val bytes = createPackage(listOf("one.nca"), listOf(8L)).copyOf(20)
        Pfs0Inspector.inspect(ByteArrayInputStream(bytes), bytes.size.toLong())
    }

    private fun createPackage(names: List<String>, sizes: List<Long>): ByteArray {
        require(names.size == sizes.size)
        val encodedNames = names.map { it.toByteArray(Charsets.UTF_8) }
        val stringTableSize = encodedNames.sumOf { it.size + 1 }
        val directorySize = 0x10 + names.size * 0x18 + stringTableSize
        val payloadSize = sizes.sum()
        require(payloadSize <= Int.MAX_VALUE)
        val output = ByteArray(directorySize + payloadSize.toInt())
        val buffer = ByteBuffer.wrap(output).order(ByteOrder.LITTLE_ENDIAN)

        buffer.put(byteArrayOf(0x50, 0x46, 0x53, 0x30))
        buffer.putInt(names.size)
        buffer.putInt(stringTableSize)
        buffer.putInt(0)

        var nameOffset = 0
        var dataOffset = 0L
        for (index in names.indices) {
            buffer.putLong(dataOffset)
            buffer.putLong(sizes[index])
            buffer.putInt(nameOffset)
            buffer.putInt(0)
            nameOffset += encodedNames[index].size + 1
            dataOffset += sizes[index]
        }

        encodedNames.forEach { name ->
            buffer.put(name)
            buffer.put(0)
        }

        return output
    }
}
