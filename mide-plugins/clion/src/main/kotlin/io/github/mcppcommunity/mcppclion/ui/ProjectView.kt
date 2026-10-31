// The project view: each mcpp project of the window, its mcpp, its targets, tests and libraries, its
// last build and what it produced, and what C++ Modules says about it -- a tree over the backend's
// `projects/list`, `project/summary`, `project/mcpp` and `languageService/status`.
package io.github.mcppcommunity.mcppclion.ui

import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonObject
import com.intellij.icons.AllIcons
import com.intellij.openapi.Disposable
import com.intellij.openapi.actionSystem.ActionManager
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.DefaultActionGroup
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.project.DumbAwareAction
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.SimpleToolWindowPanel
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.ui.ColoredTreeCellRenderer
import com.intellij.ui.DoubleClickListener
import com.intellij.ui.ScrollPaneFactory
import com.intellij.ui.SimpleTextAttributes
import com.intellij.ui.treeStructure.Tree
import com.intellij.util.ui.tree.TreeUtil
import io.github.mcppcommunity.mcppclion.McppSession
import io.github.mcppcommunity.mcppclion.actions.Dispatch
import java.awt.event.MouseEvent
import java.util.concurrent.CompletableFuture
import javax.swing.Icon
import javax.swing.JComponent
import javax.swing.JTree
import javax.swing.tree.DefaultMutableTreeNode
import javax.swing.tree.DefaultTreeModel

/** A row of a tree: its text, a quieter detail, an icon, and a file it opens or a command it runs. */
data class Row(val text: String, val detail: String = "", val icon: Icon? = null, val file: String? = null,
               val command: String? = null, val root: String? = null, val error: Boolean = false)

/** A row with its children. */
class RowNode(row: Row, children: List<RowNode> = emptyList()) : DefaultMutableTreeNode(row) {
    init { children.forEach { add(it) } }
    val row: Row get() = userObject as Row
}

/** What the project view shows for one project, from the backend's answers. Pure: the tests build it. */
object ProjectModel {
    fun project(p: JsonObject, summary: JsonObject?, mcpp: JsonObject?, t: (String, Array<out Any?>) -> String): RowNode {
        fun tr(key: String, vararg a: Any?) = t(key, a)
        val root = p["root"].asString
        val children = mutableListOf<RowNode>()
        mcpp?.let { m ->
            val state = m["state"]?.asString ?: "not-probed"
            val text = if (state == "ok") "mcpp ${m["version"]?.asString ?: ""}" else "mcpp: ${m["message"]?.asString ?: state}"
            children += RowNode(Row(text, m["program"]?.asString ?: "", AllIcons.Nodes.Plugin, error = state != "ok"))
        }
        summary?.let { s ->
            s["error"]?.takeIf { it.isJsonPrimitive }?.let { children += RowNode(Row(tr("The build database could not be read: {0}", it.asString), icon = AllIcons.General.Warning, error = true)) }
            fun group(key: String, title: String, icon: Icon, command: String?) {
                val rows = s[key]?.takeIf { it.isJsonArray }?.asJsonArray ?: return
                if (rows.isEmpty) return
                children += RowNode(Row(title, rows.size().toString(), AllIcons.Nodes.Folder), rows.map { r ->
                    val o = r.asJsonObject
                    RowNode(Row(o["name"].asString, o["configuration"]?.asString ?: "", icon, command = command, root = root))
                })
            }
            group("targets", tr("Targets"), AllIcons.RunConfigurations.Application, "mcpp.run")
            group("tests", tr("Tests"), AllIcons.RunConfigurations.Junit, "mcpp.test")
            group("libraries", tr("Libraries"), AllIcons.Nodes.PpLib, null)
            s["lastBuild"]?.takeIf { it.isJsonObject }?.asJsonObject?.let { b ->
                val status = b["status"]?.asString ?: ""
                children += RowNode(Row(tr("Last build: {0}", status), b["command"]?.asString ?: "",
                    if (status == "ok") AllIcons.RunConfigurations.TestPassed else AllIcons.RunConfigurations.TestFailed, error = status != "ok"))
            }
            s["artifacts"]?.takeIf { it.isJsonArray }?.asJsonArray?.takeIf { !it.isEmpty }?.let { artifacts ->
                children += RowNode(Row(tr("Artifacts"), artifacts.size().toString(), AllIcons.Nodes.Artifact), artifacts.mapNotNull { a ->
                    val path = when {
                        a.isJsonPrimitive -> a.asString
                        a.isJsonObject -> a.asJsonObject["path"]?.asString
                        else -> null
                    } ?: return@mapNotNull null
                    RowNode(Row(path.substringAfterLast('/'), path, AllIcons.FileTypes.Any_type, file = path))
                })
            }
            s["platformText"]?.takeIf { it.isJsonPrimitive && it.asString.isNotEmpty() }?.let {
                children += RowNode(Row(tr("Platform: {0}", it.asString), icon = AllIcons.Nodes.Module))
            }
        }
        return RowNode(Row(p["name"]?.asString ?: root.substringAfterLast('/'), root, AllIcons.Nodes.Module, file = "$root/mcpp.toml"), children)
    }

    fun languageService(ls: JsonObject?, t: (String, Array<out Any?>) -> String): RowNode? {
        ls ?: return null
        val ok = ls["state"]?.asString == "ok"
        val workspace = ls["workspace"]?.takeIf { it.isJsonObject }?.asJsonObject
        val state = workspace?.get("state")?.asString
        val text = t("C++ Modules", emptyArray()) + (state?.let { ": $it" } ?: "")
        return RowNode(Row(text, ls["message"]?.asString ?: "", AllIcons.Nodes.Plugin, error = !ok))
    }
}

class ProjectView(private val project: Project, parent: Disposable) : McppView, Disposable {
    override val id = "project"
    private val session = McppSession.get(project)
    private val tree = Tree(DefaultTreeModel(DefaultMutableTreeNode()))
    private val panel = SimpleToolWindowPanel(true, true)
    val component: JComponent get() = panel
    private var unsubscribe: List<() -> Unit> = emptyList()

    init {
        Disposer.register(parent, this)
        tree.isRootVisible = false
        tree.showsRootHandles = true
        tree.cellRenderer = RowRenderer()
        object : DoubleClickListener() {
            override fun onDoubleClick(event: MouseEvent): Boolean {
                val row = (tree.lastSelectedPathComponent as? RowNode)?.row ?: return false
                open(row)
                return true
            }
        }.installOn(tree)
        val group = DefaultActionGroup().apply {
            add(action("Refresh", AllIcons.Actions.Refresh) { refresh() })
            add(action("Build", AllIcons.Actions.Compile) { Dispatch.run(project, "mcpp.build", JsonObject()) })
            add(action("Test", AllIcons.RunConfigurations.Junit) { Dispatch.run(project, "mcpp.test", JsonObject()) })
            addSeparator()
            add(action("Dependency Graph", AllIcons.Graph.Layout) { McppPanels.show(project, "graph", JsonObject().apply { addProperty("graph", "packages") }) })
            add(action("Module Graph", AllIcons.Nodes.Module) { McppPanels.show(project, "graph", JsonObject().apply { addProperty("graph", "modules") }) })
        }
        val toolbar = ActionManager.getInstance().createActionToolbar("McppProjectView", group, true)
        toolbar.targetComponent = panel
        panel.toolbar = toolbar.component
        panel.setContent(ScrollPaneFactory.createScrollPane(tree))
        // What changes the tree: a build ended, a manifest or a result record changed, mcppls answered.
        unsubscribe = listOf("job/finished", "result/changed", "manifest/changed", "languageService/changed", "project/mcppChanged")
            .map { m -> session.on(m) { refresh() } }
    }

    private fun action(text: String, icon: Icon, run: () -> Unit) = object : DumbAwareAction(session.t(text), null, icon) {
        override fun actionPerformed(e: AnActionEvent) = run()
    }

    private fun open(row: Row) {
        row.file?.let { path ->
            LocalFileSystem.getInstance().refreshAndFindFileByPath(path)?.let { OpenFileDescriptor(project, it).navigate(true) }
            return
        }
        val command = row.command ?: return
        // A target runs as its program (`bin`), a test as the one the filter names.
        Dispatch.run(project, command, JsonObject().apply {
            row.root?.let { addProperty("root", it) }
            addProperty(if (command == "mcpp.run") "bin" else "filter", row.text)
        })
    }

    override fun refresh() {
        if (!session.running) return
        session.request("projects/list").thenCompose { list ->
            val projects = list.takeIf { it.isJsonArray }?.asJsonArray?.map { it.asJsonObject } ?: emptyList()
            val asked = projects.map { p ->
                val root = JsonObject().apply { addProperty("root", p["root"].asString) }
                val summary = session.request("project/summary", root).handle { v, _ -> v }
                val mcpp = session.request("project/mcpp", root).handle { v, _ -> v }
                summary.thenCombine(mcpp) { s, m -> Triple(p, s?.takeIf { it.isJsonObject }?.asJsonObject, m?.takeIf { it.isJsonObject }?.asJsonObject) }
            }
            CompletableFuture.allOf(*asked.toTypedArray()).thenCombine(session.languageServiceStatus().handle { v, _ -> v }) { _, ls ->
                asked.map { it.join() } to ls?.takeIf { it.isJsonObject }?.asJsonObject
            }
        }.thenAccept { (rows, ls) ->
            val t: (String, Array<out Any?>) -> String = { key, args -> session.t(key, *args) }
            val root = DefaultMutableTreeNode()
            for ((p, s, m) in rows) root.add(ProjectModel.project(p, s, m, t))
            ProjectModel.languageService(ls, t)?.let { root.add(it) }
            ApplicationManager.getApplication().invokeLater {
                if (project.isDisposed) return@invokeLater
                val state = TreeUtil.collectExpandedPaths(tree)
                tree.model = DefaultTreeModel(root)
                if (state.isEmpty()) TreeUtil.expand(tree, 2) else TreeUtil.restoreExpandedPaths(tree, state)
            }
        }
    }

    override fun dispose() {
        unsubscribe.forEach { it() }
    }
}

/** A row's text, then its detail in grey; an error in the error colour. */
class RowRenderer : ColoredTreeCellRenderer() {
    override fun customizeCellRenderer(tree: JTree, value: Any?, selected: Boolean, expanded: Boolean, leaf: Boolean, row: Int, hasFocus: Boolean) {
        val r = (value as? RowNode)?.row ?: return
        icon = r.icon
        append(r.text, if (r.error) SimpleTextAttributes.ERROR_ATTRIBUTES else SimpleTextAttributes.REGULAR_ATTRIBUTES)
        if (r.detail.isNotEmpty()) append("  " + r.detail, SimpleTextAttributes.GRAYED_ATTRIBUTES)
    }
}

/** The values of a JSON array as objects. */
internal fun JsonElement?.objects(): List<JsonObject> =
    (this as? JsonArray)?.mapNotNull { it.takeIf { e -> e.isJsonObject }?.asJsonObject } ?: emptyList()
