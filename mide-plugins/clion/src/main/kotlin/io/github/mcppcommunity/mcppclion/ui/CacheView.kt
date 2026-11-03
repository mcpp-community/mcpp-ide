// The cache: what mcpp keeps (`cache/inventory` over `mcpp cache list`) -- the total, each kind and
// its size, the largest labels, the ages -- and the cleanups of the command table, each planned,
// confirmed at its danger level and applied as a job (Dispatch.cleanup).
package io.github.mcppcommunity.mcppclion.ui

import com.google.gson.JsonObject
import com.intellij.icons.AllIcons
import com.intellij.openapi.Disposable
import com.intellij.openapi.actionSystem.ActionManager
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.DefaultActionGroup
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.DumbAwareAction
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.SimpleToolWindowPanel
import com.intellij.openapi.util.Disposer
import com.intellij.ui.ScrollPaneFactory
import com.intellij.ui.treeStructure.Tree
import com.intellij.util.ui.tree.TreeUtil
import io.github.mcppcommunity.mcppclion.McppSession
import io.github.mcppcommunity.mcppclion.actions.Dispatch
import javax.swing.JComponent
import javax.swing.tree.DefaultMutableTreeNode
import javax.swing.tree.DefaultTreeModel

/** The cache's rows, from the inventory. Pure: the tests build it. */
object CacheModel {
    fun rows(inventory: JsonObject, t: (String, Array<out Any?>) -> String): List<RowNode> {
        fun tr(key: String, vararg a: Any?) = t(key, a)
        val out = mutableListOf<RowNode>()
        out += RowNode(Row(tr("{0} in {1} entries", inventory["totalText"]?.asString ?: "", inventory["totalEntries"]?.asString ?: "0"),
            inventory["root"]?.asString ?: "", AllIcons.Nodes.Folder))
        val kinds = inventory["byKind"].objects()
        if (kinds.isNotEmpty())
            out += RowNode(Row(tr("By kind"), icon = AllIcons.Nodes.Folder), kinds.map { k ->
                RowNode(Row(k["kind"].asString, tr("{0} · {1} entries", k["bytesText"]?.asString ?: "", k["entries"]?.asString ?: "0"), AllIcons.Nodes.DataTables))
            })
        val top = inventory["topLabels"].objects()
        if (top.isNotEmpty())
            out += RowNode(Row(tr("Largest"), icon = AllIcons.Nodes.Folder), top.map { l ->
                RowNode(Row(l["label"].asString.ifEmpty { "—" }, tr("{0} · {1} entries", l["bytesText"]?.asString ?: "", l["entries"]?.asString ?: "0"), AllIcons.Nodes.Package))
            })
        val ages = inventory["ageBuckets"].objects().filter { (it["entries"]?.asLong ?: 0) > 0 }
        if (ages.isNotEmpty())
            out += RowNode(Row(tr("By last use"), icon = AllIcons.Nodes.Folder), ages.map { b ->
                val from = b["fromDays"]?.asInt ?: 0
                val span = b["toDays"]?.asInt?.let { tr("{0}–{1} days", from, it) } ?: tr("{0} days or more", from)
                RowNode(Row(span, tr("{0} · {1} entries", b["bytesText"]?.asString ?: "", b["entries"]?.asString ?: "0"), AllIcons.Vcs.History))
            })
        val incomplete = inventory["incomplete"].objects()
        if (incomplete.isNotEmpty())
            out += RowNode(Row(tr("{0} incomplete entries", incomplete.size), icon = AllIcons.General.Warning, error = true))
        return out
    }
}

class CacheView(private val project: Project, parent: Disposable) : McppView, Disposable {
    override val id = "cache"
    private val session = McppSession.get(project)
    private val tree = Tree(DefaultTreeModel(DefaultMutableTreeNode()))
    private val panel = SimpleToolWindowPanel(true, true)
    private val cleanups = DefaultActionGroup()
    val component: JComponent get() = panel
    private var unsubscribe: () -> Unit = {}

    init {
        Disposer.register(parent, this)
        tree.isRootVisible = false
        tree.showsRootHandles = true
        tree.cellRenderer = RowRenderer()
        val group = DefaultActionGroup().apply {
            add(object : DumbAwareAction(session.t("Refresh"), null, AllIcons.Actions.Refresh) {
                override fun actionPerformed(e: AnActionEvent) = refresh()
            })
            addSeparator()
            add(cleanups)
        }
        val toolbar = ActionManager.getInstance().createActionToolbar("McppCacheView", group, true)
        toolbar.targetComponent = panel
        panel.toolbar = toolbar.component
        panel.setContent(ScrollPaneFactory.createScrollPane(tree))
        unsubscribe = session.on("job/finished") { p -> if (p.asJsonObject["kind"]?.asString in setOf("cache", "clean", "command")) refresh() }
    }

    /** The cleanups the command table offers here, under their titles. */
    private fun fillCleanups() {
        cleanups.removeAll()
        for (c in session.commands) {
            val action = c["action"]?.asJsonObject ?: continue
            if (action["kind"]?.asString != "cache") continue
            val plan = action["plan"]?.asString ?: continue
            cleanups.add(object : DumbAwareAction(c["title"]?.asString ?: plan, null, AllIcons.Actions.GC) {
                override fun actionPerformed(e: AnActionEvent) = Dispatch.cleanup(project, plan)
            })
        }
    }

    override fun refresh() {
        if (!session.running) return
        session.request("cache/inventory", JsonObject().apply { session.root?.let { addProperty("root", it) } }).whenComplete { r, error ->
            ApplicationManager.getApplication().invokeLater {
                if (project.isDisposed) return@invokeLater
                fillCleanups()
                val root = DefaultMutableTreeNode()
                if (error != null) root.add(RowNode(Row(session.t("mcpp: {0}", (error.cause ?: error).message ?: ""), icon = AllIcons.General.Error, error = true)))
                else CacheModel.rows(r.asJsonObject) { key, args -> session.t(key, *args) }.forEach { root.add(it) }
                tree.model = DefaultTreeModel(root)
                TreeUtil.expand(tree, 1)
            }
        }
    }

    override fun dispose() = unsubscribe()
}
