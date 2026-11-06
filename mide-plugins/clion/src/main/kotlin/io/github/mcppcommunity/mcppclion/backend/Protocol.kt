package io.github.mcppcommunity.mcppclion.backend

import com.google.gson.JsonObject
import java.math.BigDecimal

/** Check the handshake before issuing any namespaced extension requests. */
fun requireProtocol(initialized: JsonObject) {
    val value = initialized["protocol"]
    val compatible = runCatching {
        value != null && value.isJsonPrimitive && value.asJsonPrimitive.isNumber &&
            value.asBigDecimal.compareTo(BigDecimal(2)) == 0
    }.getOrDefault(false)
    check(compatible) {
        "The selected mcpp-ide backend does not support protocol 2. Update the backend or clear mcpp.ide.backendPath to use the bundled backend."
    }
}
