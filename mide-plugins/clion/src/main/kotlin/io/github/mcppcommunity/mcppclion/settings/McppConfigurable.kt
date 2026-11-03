// Settings | Tools | mcpp, in CLion's own form (mcpp design 2026-10-30 part 2 §9): one field per
// setting of the backend's `settings/registry` -- its title, its description and the values it may
// hold, in the session's language -- under the registry's groups, the advanced ones last. Apply
// stores what changed (a value equal to the default is removed) and the session tells the backend;
// Reset shows what is stored. The search field narrows the form by key, title and description.
package io.github.mcppcommunity.mcppclion.settings

import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonNull
import com.google.gson.JsonObject
import com.google.gson.JsonPrimitive
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.options.SearchableConfigurable
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.ComboBox
import com.intellij.ui.SearchTextField
import com.intellij.ui.TitledSeparator
import com.intellij.ui.components.JBCheckBox
import com.intellij.ui.components.JBLabel
import com.intellij.ui.components.JBScrollPane
import com.intellij.ui.components.JBTextArea
import com.intellij.ui.components.JBTextField
import com.intellij.util.ui.FormBuilder
import com.intellij.util.ui.JBUI
import com.intellij.util.ui.UIUtil
import io.github.mcppcommunity.mcppclion.McppSession
import java.awt.BorderLayout
import javax.swing.DefaultComboBoxModel
import javax.swing.JComponent
import javax.swing.JPanel
import javax.swing.event.DocumentEvent

/** One setting's field: what it shows, how it reads and writes a JSON value. */
class SettingField(val entry: JsonObject) {
    val key: String = entry["key"].asString
    val type: String = entry["type"]?.asString ?: "string"
    val default: JsonElement = entry["default"] ?: JsonNull.INSTANCE
    private val enumValues: List<JsonElement> = entry["enum"]?.takeIf { it.isJsonArray }?.asJsonArray?.toList() ?: emptyList()
    val title: String = entry["title"]?.asString ?: key
    val description: String = entry["description"]?.asString ?: ""

    val component: JComponent = when {
        type == "boolean" -> JBCheckBox(title)
        enumValues.isNotEmpty() -> ComboBox(DefaultComboBoxModel(enumValues.map { label(it) }.toTypedArray()))
        type == "array" -> JBTextArea(2, 40).apply { lineWrap = false }
        else -> JBTextField(30)
    }
    val label: JBLabel? = if (type == "boolean") null else JBLabel("$title:")
    val comment: JBLabel = JBLabel("<html>${escape(description)}<br><code>$key</code></html>").apply {
        foreground = UIUtil.getContextHelpForeground()
        font = JBUI.Fonts.smallFont()
    }

    private fun label(v: JsonElement) = if (v.isJsonPrimitive && v.asJsonPrimitive.isString) v.asString else v.toString()

    /** Shows a stored value, else the default. */
    fun show(value: JsonElement?) {
        val v = value?.takeIf { !it.isJsonNull } ?: default
        when (val c = component) {
            is JBCheckBox -> c.isSelected = v.isJsonPrimitive && v.asBoolean
            is ComboBox<*> -> c.selectedItem = label(v)
            is JBTextArea -> c.text = if (v.isJsonArray) v.asJsonArray.joinToString("\n") { label(it) } else ""
            is JBTextField -> c.text = if (v.isJsonNull) "" else label(v)
        }
    }

    /** The value the field holds, as the registry types it; null when it is not one. */
    fun value(): JsonElement? = when (val c = component) {
        is JBCheckBox -> JsonPrimitive(c.isSelected)
        is ComboBox<*> -> enumValues.firstOrNull { label(it) == c.selectedItem } ?: default
        is JBTextArea -> JsonArray().apply { c.text.lines().map { it.trim() }.filter { it.isNotEmpty() }.forEach { add(it) } }
        is JBTextField -> if (type == "number") c.text.trim().toDoubleOrNull()?.let { d ->
            if (d == Math.floor(d) && !d.isInfinite()) JsonPrimitive(d.toLong()) else JsonPrimitive(d)
        } else JsonPrimitive(c.text)
        else -> null
    }

    fun matches(filter: String): Boolean = filter.isEmpty() ||
        listOf(key, title, description).any { it.contains(filter, ignoreCase = true) }

    fun setVisible(visible: Boolean) {
        component.isVisible = visible
        label?.isVisible = visible
        comment.isVisible = visible
    }

    private fun escape(s: String) = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
}

/** The form from a registry. Pure but for Swing: the tests build it from the backend's answer. */
class SettingsForm(registry: JsonObject, t: (String) -> String, private val stored: (String) -> JsonElement?) {
    val fields: List<SettingField>
    val component: JComponent
    private val search = SearchTextField(false)
    private val separators = mutableListOf<Pair<TitledSeparator, List<SettingField>>>()

    init {
        val entries = registry.getAsJsonArray("settings")?.map { it.asJsonObject }?.filter { it["deprecated"]?.asBoolean != true } ?: emptyList()
        val groups = registry.getAsJsonArray("groups")?.map { it.asJsonObject }?.sortedBy { it["order"]?.asInt ?: 0 } ?: emptyList()
        val builder = FormBuilder.createFormBuilder()
        val all = mutableListOf<SettingField>()
        // The public settings by group, then the advanced ones by group.
        for (advanced in listOf(false, true)) {
            for (g in groups) {
                val id = g["id"].asString
                val inGroup = entries.filter { it["group"]?.asString == id && (it["tier"]?.asString == "advanced") == advanced }
                if (inGroup.isEmpty()) continue
                val title = g["title"]?.asString ?: id
                val separator = TitledSeparator(if (advanced) "$title · ${t("Advanced")}" else title)
                builder.addComponent(separator)
                val fields = inGroup.map { SettingField(it) }
                for (f in fields) {
                    if (f.label != null) builder.addLabeledComponent(f.label, f.component) else builder.addComponent(f.component)
                    builder.addComponentToRightColumn(f.comment)
                }
                separators += separator to fields
                all += fields
            }
        }
        fields = all
        val form = builder.addComponentFillVertically(JPanel(), 0).panel
        search.addDocumentListener(object : com.intellij.ui.DocumentAdapter() {
            override fun textChanged(e: DocumentEvent) = filter(search.text.trim())
        })
        component = JPanel(BorderLayout(0, JBUI.scale(6))).apply {
            add(search, BorderLayout.NORTH)
            add(JBScrollPane(form).apply { border = JBUI.Borders.empty() }, BorderLayout.CENTER)
        }
        reset()
    }

    fun filter(text: String) {
        for ((separator, fields) in separators) {
            var any = false
            for (f in fields) {
                val shown = f.matches(text)
                f.setVisible(shown)
                any = any || shown
            }
            separator.isVisible = any
        }
    }

    /** The stored value a field stands for: stored, else its default. */
    private fun current(f: SettingField): JsonElement = stored(f.key)?.takeIf { !it.isJsonNull } ?: f.default

    fun isModified(): Boolean = fields.any { f -> f.value()?.let { it != current(f) } ?: true }

    /** What changed, each value to store (null: back to the default). */
    fun changes(): Map<String, JsonElement?> = fields.mapNotNull { f ->
        val v = f.value() ?: return@mapNotNull null
        if (v == current(f)) null else f.key to (if (v == f.default) null else v)
    }.toMap()

    fun reset() = fields.forEach { it.show(stored(it.key)) }
}

class McppConfigurable(private val project: Project) : SearchableConfigurable {
    private var form: SettingsForm? = null
    private val holder = JPanel(BorderLayout())

    override fun getId(): String = "io.github.mcppcommunity.mcppclion.settings"
    override fun getDisplayName(): String = "mcpp"

    override fun createComponent(): JComponent {
        val session = McppSession.get(project)
        holder.removeAll()
        holder.add(JBLabel(session.t("Reading the settings…")), BorderLayout.NORTH)
        session.start().thenCompose { session.request("settings/registry") }.whenComplete { registry, error ->
            ApplicationManager.getApplication().invokeLater {
                holder.removeAll()
                if (error != null || registry == null || !registry.isJsonObject) {
                    holder.add(JBLabel(session.t("The settings come from the mcpp-ide backend, which is not running: {0}", (error?.cause ?: error)?.message ?: "")), BorderLayout.NORTH)
                } else {
                    val f = SettingsForm(registry.asJsonObject, { key -> session.t(key) }) { key -> McppSettings.get().get(key) }
                    form = f
                    holder.add(f.component, BorderLayout.CENTER)
                }
                holder.revalidate()
                holder.repaint()
            }
        }
        return holder
    }

    override fun isModified(): Boolean = form?.isModified() ?: false

    override fun apply() {
        val f = form ?: return
        val settings = McppSettings.get()
        for ((key, value) in f.changes()) settings.set(key, value)
    }

    override fun reset() {
        form?.reset()
    }

    override fun disposeUIResources() {
        form = null
        holder.removeAll()
    }
}
