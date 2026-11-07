package io.github.mcppcommunity.mcppclion.backend

import com.google.gson.JsonObject
import java.math.BigDecimal

/** The series of a version `X.Y.Z`: `X.Y`, shared by the backend and every plugin (mcpp design 2026-11-07, D15). */
fun seriesOf(version: String?): String? =
    version?.let { Regex("""^(\d+)\.(\d+)(\.\d+)?""").find(it.trim()) }?.let { "${it.groupValues[1]}.${it.groupValues[2]}" }

/**
 * Check the handshake before issuing any namespaced extension requests: protocol 2, and a backend of
 * this plugin's series. A backend before 1.2 states no series; its version's `X.Y` is taken, and it is
 * another series.
 */
fun requireProtocol(initialized: JsonObject, pluginVersion: String) {
    val value = initialized["protocol"]
    val compatible = runCatching {
        value != null && value.isJsonPrimitive && value.asJsonPrimitive.isNumber &&
            value.asBigDecimal.compareTo(BigDecimal(2)) == 0
    }.getOrDefault(false)
    check(compatible) {
        "The selected mcpp-ide backend does not support protocol 2. Update the backend or clear mcpp.ide.backendPath to use the bundled backend."
    }
    val backendVersion = runCatching { initialized.getAsJsonObject("serverInfo")["version"].asString }.getOrNull() ?: "unknown"
    val backendSeries = runCatching { initialized["series"].asString }.getOrNull() ?: seriesOf(backendVersion)
    val own = seriesOf(pluginVersion)
    check(own != null && backendSeries == own) {
        "The mcpp-ide backend $backendVersion is of series ${backendSeries ?: "unknown"}, and mcpp-clion $pluginVersion of series $own. " +
            "Install the mcpp-ide backend $own (xlings install mcpp-ide@$own.0), or clear mcpp.ide.backendPath to use the bundled backend."
    }
}
