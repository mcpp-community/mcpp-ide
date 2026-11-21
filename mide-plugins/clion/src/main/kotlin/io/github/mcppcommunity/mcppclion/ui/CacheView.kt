// The cache, in the two sections of VS Code's page (design 2026-11-21, "The cache view"): the
// project's build (`target/`) with its cleanups, and the shared cache mcpp keeps (`cache/inventory`
// over `mcpp cache list`) -- the total, each kind and its size, the largest labels, the ages -- with
// its own. A cleanup sits on the section it acts on, by the scope the command table states, and is
// planned, confirmed at its danger level and applied as a job (Dispatch.cleanup).
package io.github.mcppcommunity.mcppclion.ui

import com.google.gson.JsonObject
import com.intellij.icons.AllIcons
import com.intellij.openapi.Disposable
import com.intellij.openapi.actionSystem.ActionManager
import com.intellij.openapi.actionSystem.DefaultActionGroup
import com.intellij.openapi.application.ApplicationManager
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

/** The cache's sections, from the command table and the inventory. Pure: the tests build it. */
object CacheModel {
    /**
     * The cleanups the command table states for a scope (`project`, `shared`), in its order, one per
     * plan: two commands of one plan (mcpp.clean, mcpp.cleanProjectArtifacts) are one action, the
     * cache group's, in its place.
     */
    fun actions(commands: List<JsonObject>, scope: String): List<RowAction> {
        val byPlan = linkedMapOf<String, JsonObject>()
        for (c in commands) {
            val action = c["action"]?.takeIf { it.isJsonObject }?.asJsonObject ?: continue
            if (action["kind"]?.asString != "cache" || action["scope"]?.asString != scope) continue
            val plan = action["plan"]?.asString ?: continue
            val kept = byPlan[plan]
            if (kept == null || (kept["group"]?.asString != "cache" && c["group"]?.asString == "cache")) byPlan[plan] = c
        }
        return byPlan.entries.sortedBy { (_, c) -> commands.indexOf(c) }.map { (plan, c) ->
            RowAction(c["id"].asString, plan, c["title"]?.asString ?: plan, (c["action"].asJsonObject["level"]?.asInt ?: 0) > 0)
        }
    }

    fun sections(inventory: JsonObject?, error: String?, commands: List<JsonObject>, t: (String, Array<out Any?>) -> String): List<RowNode> {
        fun tr(key: String, vararg a: Any?) = t(key, a)
        val project = RowNode(Row(tr("Project cache"), "target/", AllIcons.Nodes.Module, actions = actions(commands, "project")))
        val shared = mutableListOf<RowNode>()
        var total = ""
        if (error != null) shared += RowNode(Row(tr("mcpp: {0}", error), icon = AllIcons.General.Error, error = true))
        else if (inventory != null) {
            total = tr("{0} in {1} entries", inventory["totalText"]?.asString ?: "", inventory["totalEntries"]?.asString ?: "0")
            shared += rows(inventory, t)
        }
        val cache = RowNode(Row(tr("Global build cache"), total, AllIcons.Nodes.Folder, actions = actions(commands, "shared")), shared)
        return listOf(project, cache)
    }

    /** What the inventory says of the shared cache: where it is, each kind, the largest, the ages. */
    fun rows(inventory: JsonObject, t: (String, Array<out Any?>) -> String): List<RowNode> {
        fun tr(key: String, vararg a: Any?) = t(key, a)
        val out = mutableListOf<RowNode>()
        inventory["root"]?.asString?.takeIf { it.isNotEmpty() }?.let { out += RowNode(Row(it, icon = AllIcons.Nodes.Folder)) }
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
    val component: JComponent get() = panel
    private var unsubscribe: () -> Unit = {}

    init {
        Disposer.register(parent, this)
        tree.isRootVisible = false
        tree.showsRootHandles = true
        tree.cellRenderer = RowRenderer()
        installRowActions(tree) { a -> Dispatch.cleanup(project, a.plan) }
        // The toolbar holds what acts on the whole view; each cleanup is its section's.
        val group = DefaultActionGroup().apply {
            add(SessionAction({ session.t("Refresh") }, AllIcons.Actions.Refresh) { refresh() })
        }
        val toolbar = ActionManager.getInstance().createActionToolbar("McppCacheView", group, true)
        toolbar.targetComponent = panel
        panel.toolbar = toolbar.component
        panel.setContent(ScrollPaneFactory.createScrollPane(tree))
        unsubscribe = session.on("job/finished") { p -> if (p.asJsonObject["kind"]?.asString in setOf("cache", "clean", "command")) refresh() }
    }

    override fun refresh() {
        if (!session.running) return
        session.request("cache/inventory", JsonObject().apply { session.root?.let { addProperty("root", it) } }).whenComplete { r, error ->
            ApplicationManager.getApplication().invokeLater {
                if (project.isDisposed) return@invokeLater
                val root = DefaultMutableTreeNode()
                val failed = error?.let { (it.cause ?: it).message ?: "" }
                CacheModel.sections(r?.takeIf { it.isJsonObject }?.asJsonObject, failed, session.commands) { key, args -> session.t(key, *args) }
                    .forEach { root.add(it) }
                tree.model = DefaultTreeModel(root)
                TreeUtil.expand(tree, 2)
            }
        }
    }

    override fun dispose() = unsubscribe()
}
