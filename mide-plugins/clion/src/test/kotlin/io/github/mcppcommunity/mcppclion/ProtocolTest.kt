package io.github.mcppcommunity.mcppclion

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import io.github.mcppcommunity.mcppclion.backend.requireProtocol
import io.github.mcppcommunity.mcppclion.backend.seriesOf
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class ProtocolTest {
    private fun answer(protocol: String, version: String = "1.2.0", series: String? = "1.2") =
        JsonParser.parseString("""{"protocol":$protocol,"serverInfo":{"name":"mcpp-ide","version":"$version"}${series?.let { ",\"series\":\"$it\"" } ?: ""}}""").asJsonObject

    @Test
    fun theHandshakeRejectsIncompatibleProtocolsBeforeCustomRequests() {
        for (protocol in listOf("1", "3", "null", "\"2\"", "{}", "[]", "true", "2.5")) {
            assertThrows(IllegalStateException::class.java) { requireProtocol(answer(protocol), "1.2.0") }
        }
        assertThrows(IllegalStateException::class.java) { requireProtocol(JsonObject(), "1.2.0") }
        requireProtocol(answer("2"), "1.2.0")
    }

    // D15: one series, any patch of either side.
    @Test
    fun theHandshakeAcceptsOneSeriesAndRefusesAnother() {
        requireProtocol(answer("2", "1.2.0"), "1.2.3")
        requireProtocol(answer("2", "1.2.4"), "1.2.0")
        requireProtocol(answer("2", "1.2.1", series = null), "1.2.0")
        val older = assertThrows(IllegalStateException::class.java) { requireProtocol(answer("2", "0.2.1", series = null), "1.2.0") }
        assertTrue(older.message!!.contains("0.2.1") && older.message!!.contains("series 0.2") && older.message!!.contains("mcpp-ide@1.2.0"))
        assertThrows(IllegalStateException::class.java) { requireProtocol(answer("2", "1.3.0", "1.3"), "1.2.0") }
        assertEquals("1.2", seriesOf("1.2.0"))
        assertEquals("1.2", seriesOf("1.2"))
        assertEquals(null, seriesOf("unknown"))
    }
}
