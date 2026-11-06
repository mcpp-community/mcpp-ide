package io.github.mcppcommunity.mcppclion

import com.google.gson.JsonObject
import org.junit.Assert.assertThrows
import org.junit.Test

class ProtocolTest {
    @Test
    fun theHandshakeRejectsIncompatibleProtocolsBeforeCustomRequests() {
        for (protocol in listOf("1", "3", "null", "\"2\"", "{}", "[]", "true", "2.5")) {
            val initialized = com.google.gson.JsonParser.parseString("""{"protocol":$protocol}""").asJsonObject
            assertThrows(IllegalStateException::class.java) {
                io.github.mcppcommunity.mcppclion.backend.requireProtocol(initialized)
            }
        }
        assertThrows(IllegalStateException::class.java) {
            io.github.mcppcommunity.mcppclion.backend.requireProtocol(JsonObject())
        }
        io.github.mcppcommunity.mcppclion.backend.requireProtocol(JsonObject().apply { addProperty("protocol", 2) })
    }

}
