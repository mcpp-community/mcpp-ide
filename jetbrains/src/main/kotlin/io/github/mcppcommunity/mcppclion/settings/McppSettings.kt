// The mcpp settings of this IDE: the same keys as VS Code's (spec/settings.json), each value kept as
// JSON. What a key may hold, its default and its renames are the backend's to say; this only stores.
package io.github.mcppcommunity.mcppclion.settings

import com.google.gson.JsonElement
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.PersistentStateComponent
import com.intellij.openapi.components.Service
import com.intellij.openapi.components.State
import com.intellij.openapi.components.Storage
import com.intellij.util.messages.Topic

@Service(Service.Level.APP)
@State(name = "McppIdeSettings", storages = [Storage("mcpp-ide.xml")])
class McppSettings : PersistentStateComponent<McppSettings.State> {
    class State {
        var values: MutableMap<String, String> = sortedMapOf()
    }

    private var state = State()

    override fun getState(): State = state
    override fun loadState(s: State) { state = s }

    fun values(): JsonObject = JsonObject().also { o ->
        for ((k, v) in state.values) runCatching { o.add(k, JsonParser.parseString(v)) }
    }

    fun get(key: String): JsonElement? = state.values[key]?.let { runCatching { JsonParser.parseString(it) }.getOrNull() }

    fun string(key: String, default: String = ""): String = get(key)?.takeIf { it.isJsonPrimitive }?.asString ?: default

    fun set(key: String, value: JsonElement?) {
        if (value == null || value.isJsonNull) state.values.remove(key) else state.values[key] = value.toString()
        ApplicationManager.getApplication().messageBus.syncPublisher(CHANGED).changed(key)
    }

    fun interface Listener { fun changed(key: String) }

    companion object {
        val CHANGED: Topic<Listener> = Topic.create("mcpp settings", Listener::class.java)
        fun get(): McppSettings = ApplicationManager.getApplication().getService(McppSettings::class.java)
    }
}
