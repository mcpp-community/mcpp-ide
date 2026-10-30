// The libraries: the packages of mcpp's indexes (`library/search`, a keyword and an index), and the
// page of the one selected -- its description, versions, licences and repository -- with the action
// that adds it to the project (`library/add`, `mcpp add` as a job).
package io.github.mcppcommunity.mcppclion.ui

import com.google.gson.JsonObject
import com.intellij.icons.AllIcons
import com.intellij.ide.BrowserUtil
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.ComboBox
import com.intellij.openapi.ui.popup.JBPopupFactory
import com.intellij.openapi.ui.popup.PopupStep
import com.intellij.openapi.ui.popup.util.BaseListPopupStep
import com.intellij.openapi.util.Disposer
import com.intellij.ui.ColoredListCellRenderer
import com.intellij.ui.JBSplitter
import com.intellij.ui.ScrollPaneFactory
import com.intellij.ui.SearchTextField
import com.intellij.ui.SimpleTextAttributes
import com.intellij.ui.components.JBList
import com.intellij.util.Alarm
import com.intellij.util.ui.HTMLEditorKitBuilder
import com.intellij.util.ui.JBUI
import io.github.mcppcommunity.mcppclion.McppSession
import java.awt.BorderLayout
import java.awt.FlowLayout
import javax.swing.DefaultComboBoxModel
import javax.swing.DefaultListModel
import javax.swing.JButton
import javax.swing.JComponent
import javax.swing.JEditorPane
import javax.swing.JList
import javax.swing.JPanel
import javax.swing.event.DocumentEvent
import javax.swing.event.HyperlinkEvent

/** A package's page as HTML. Pure: the tests read it. */
object LibraryPage {
    private fun esc(s: String) = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

    fun html(p: JsonObject, t: (String) -> String): String {
        val versions = p.getAsJsonArray("versions")?.map { it.asString } ?: emptyList()
        val licenses = p.getAsJsonArray("licenses")?.joinToString(", ") { it.asString } ?: ""
        val repo = p["repo"]?.takeIf { it.isJsonPrimitive }?.asString ?: ""
        return buildString {
            append("<html><body>")
            append("<h2>${esc(p["id"]?.asString ?: "")}</h2>")
            p["description"]?.takeIf { it.isJsonPrimitive }?.asString?.trim()?.takeIf { it.isNotEmpty() }?.let { append("<p>${esc(it)}</p>") }
            append("<table>")
            fun row(k: String, v: String) { if (v.isNotEmpty()) append("<tr><td><b>${esc(t(k))}</b></td><td>$v</td></tr>") }
            row("Index", esc(p["index"]?.asString ?: ""))
            row("In mcpp.toml", "<code>${esc(p["dependencyName"]?.asString ?: "")}</code>")
            row("Latest", esc(p["latest"]?.takeIf { it.isJsonPrimitive }?.asString ?: ""))
            row("Versions", esc(versions.joinToString(", ")))
            row("License", esc(licenses))
            if (repo.isNotEmpty()) row("Repository", "<a href=\"${esc(repo)}\">${esc(repo)}</a>")
            append("</table></body></html>")
        }
    }
}

class LibraryView(private val project: Project, parent: Disposable) : McppView, Disposable {
    override val id = "libraries"
    private val session = McppSession.get(project)
    private val search = SearchTextField(true)
    private val index = ComboBox<String>()
    private val packages = DefaultListModel<JsonObject>()
    private val list = JBList(packages)
    private val page = JEditorPane().apply {
        isEditable = false
        editorKit = HTMLEditorKitBuilder.simple()
        addHyperlinkListener { e -> if (e.eventType == HyperlinkEvent.EventType.ACTIVATED) e.url?.let { BrowserUtil.browse(it) } }
    }
    private val add = JButton(session.t("Add to the project"), AllIcons.General.Add)
    private val status = com.intellij.ui.components.JBLabel()
    private val alarm = Alarm(Alarm.ThreadToUse.SWING_THREAD, this)
    private var wanted: String? = null
    private val panel = JPanel(BorderLayout())
    val component: JComponent get() = panel

    init {
        Disposer.register(parent, this)
        index.model = DefaultComboBoxModel(arrayOf(""))
        index.renderer = com.intellij.ui.SimpleListCellRenderer.create("") { it.ifEmpty { session.t("All indexes") } }
        list.cellRenderer = object : ColoredListCellRenderer<JsonObject>() {
            override fun customizeCellRenderer(list: JList<out JsonObject>, value: JsonObject?, index: Int, selected: Boolean, hasFocus: Boolean) {
                value ?: return
                icon = AllIcons.Nodes.PpLib
                append(value["id"]?.asString ?: "")
                value["latest"]?.takeIf { it.isJsonPrimitive }?.let { append("  ${it.asString}", SimpleTextAttributes.GRAYED_ATTRIBUTES) }
                value["description"]?.takeIf { it.isJsonPrimitive }?.asString?.trim()?.takeIf { it.isNotEmpty() }?.let {
                    append("  — " + it.take(100), SimpleTextAttributes.GRAYED_SMALL_ATTRIBUTES)
                }
            }
        }
        list.addListSelectionListener { if (!it.valueIsAdjusting) show(list.selectedValue) }
        search.addDocumentListener(object : com.intellij.ui.DocumentAdapter() {
            override fun textChanged(e: DocumentEvent) {
                alarm.cancelAllRequests()
                alarm.addRequest({ refresh() }, 300)
            }
        })
        index.addActionListener { refresh() }
        add.isEnabled = false
        add.addActionListener { list.selectedValue?.let { addToProject(it) } }
        val top = JPanel(BorderLayout(JBUI.scale(4), 0)).apply {
            border = JBUI.Borders.empty(4)
            add(search, BorderLayout.CENTER)
            add(index, BorderLayout.EAST)
        }
        val detail = JPanel(BorderLayout()).apply {
            add(ScrollPaneFactory.createScrollPane(page), BorderLayout.CENTER)
            add(JPanel(FlowLayout(FlowLayout.LEFT)).apply { add(add) }, BorderLayout.SOUTH)
        }
        val splitter = JBSplitter(true, 0.55f).apply {
            firstComponent = ScrollPaneFactory.createScrollPane(list)
            secondComponent = detail
        }
        panel.add(top, BorderLayout.NORTH)
        panel.add(splitter, BorderLayout.CENTER)
        panel.add(status.apply { border = JBUI.Borders.empty(2, 6) }, BorderLayout.SOUTH)
    }

    override fun focus(params: JsonObject) {
        params["id"]?.asString?.let { id ->
            wanted = id
            search.text = id.substringAfter(':')
        }
        refresh()
    }

    override fun refresh() {
        if (!session.running) return
        val params = JsonObject().apply {
            addProperty("keyword", search.text.trim())
            (index.selectedItem as? String)?.takeIf { it.isNotEmpty() }?.let { addProperty("index", it) }
            addProperty("limit", 500)
            session.root?.let { addProperty("root", it) }
        }
        status.text = session.t("Searching…")
        session.request("library/search", params).whenComplete { r, error ->
            ApplicationManager.getApplication().invokeLater {
                if (project.isDisposed) return@invokeLater
                if (error != null) { status.text = session.t("mcpp: {0}", (error.cause ?: error).message ?: ""); return@invokeLater }
                val o = r.asJsonObject
                val selected = wanted ?: list.selectedValue?.get("id")?.asString
                packages.clear()
                o.getAsJsonArray("packages").forEach { packages.addElement(it.asJsonObject) }
                val known = (index.model as DefaultComboBoxModel<String>)
                if (known.size <= 1) o.getAsJsonArray("indexes")?.forEach { known.addElement(it.asString) }
                status.text = session.t("{0} packages", o["total"]?.asString ?: packages.size().toString())
                val at = (0 until packages.size()).firstOrNull { packages[it]["id"]?.asString == selected }
                if (at != null) { list.selectedIndex = at; list.ensureIndexIsVisible(at) } else show(null)
                wanted = null
            }
        }
    }

    private fun show(p: JsonObject?) {
        add.isEnabled = p != null
        page.text = p?.let { LibraryPage.html(it) { key -> session.t(key) } } ?: ""
        page.caretPosition = 0
    }

    /** `mcpp add <name>[@version]`: the version chosen among the package's, the latest first. */
    private fun addToProject(p: JsonObject) {
        val root = session.root ?: return
        val name = p["dependencyName"]?.asString ?: return
        val versions = listOf("") + (p.getAsJsonArray("versions")?.map { it.asString } ?: emptyList())
        val step = object : BaseListPopupStep<String>(session.t("Add {0}", name), versions) {
            override fun getTextFor(value: String): String = value.ifEmpty { session.t("The latest version") }
            override fun onChosen(value: String, finalChoice: Boolean): PopupStep<*>? {
                // A job: its end is said, and the project view reads the manifest again.
                io.github.mcppcommunity.mcppclion.actions.Dispatch.job(project, "library/add", JsonObject().apply {
                    addProperty("root", root)
                    addProperty("dependency", name)
                    if (value.isNotEmpty()) addProperty("version", value)
                })
                return FINAL_CHOICE
            }
        }
        JBPopupFactory.getInstance().createListPopup(step).showUnderneathOf(add)
    }

    override fun dispose() {}
}
