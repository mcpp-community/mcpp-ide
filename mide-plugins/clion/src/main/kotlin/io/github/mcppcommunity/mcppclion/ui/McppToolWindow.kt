// The "mcpp" tool window in CLion's own components (mcpp design 2026-10-30 part 2 §9): the project,
// the libraries and the cache as native trees and lists over the backend's view models, and a graph
// (a project's packages or modules) as a page of its own in JCEF. VS Code's information structure,
// CLion's components; the strings are the backend's (`i18n/bundle`), the ones VS Code shows.
package io.github.mcppcommunity.mcppclion.ui

import com.google.gson.JsonObject
import com.intellij.openapi.options.ShowSettingsUtil
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.util.Key
import com.intellij.openapi.wm.ToolWindow
import com.intellij.openapi.wm.ToolWindowFactory
import com.intellij.openapi.wm.ToolWindowManager
import com.intellij.ui.content.Content
import com.intellij.ui.content.ContentFactory
import com.intellij.ui.content.ContentManagerListener
import com.intellij.ui.content.ContentManagerEvent
import io.github.mcppcommunity.mcppclion.McppSession
import io.github.mcppcommunity.mcppclion.actions.Dispatch
import io.github.mcppcommunity.mcppclion.settings.McppConfigurable
import java.io.File

/** A view of the tool window: what it is called, and how it is asked to show something. */
interface McppView {
    val id: String
    fun refresh()
    /** Shows what `params` names (a package of the libraries); the view itself when nothing. */
    fun focus(params: JsonObject) { refresh() }
}

object McppPanels {
    const val TOOL_WINDOW = "mcpp"
    private val VIEW = Key.create<McppView>("mcpp.view")
    private val FOCUS_PARAMS = Key.create<JsonObject>("mcpp.focus.params")

    private fun focus(content: Content) {
        val params = content.getUserData(FOCUS_PARAMS) ?: JsonObject()
        content.putUserData(FOCUS_PARAMS, null)
        content.getUserData(VIEW)?.focus(params)
    }

    /** The command table's `panel` actions: a view brought on screen, a graph opened, settings shown. */
    fun show(project: Project, panel: String, params: JsonObject = JsonObject()) {
        when (panel) {
            "targets" -> { Dispatch.selectTarget(project); return }
            "pack" -> { Dispatch.pack(project); return }
            "settings" -> { ShowSettingsUtil.getInstance().showSettingsDialog(project, McppConfigurable::class.java); return }
        }
        val window = ToolWindowManager.getInstance(project).getToolWindow(TOOL_WINDOW) ?: return
        window.activate {
            when (panel) {
                "graph" -> openGraph(project, window, params["graph"]?.asString ?: "packages")
                "project" -> select(window, "project", params)
                "cache" -> select(window, "cache", params)
                "libraries", "library" -> select(window, "libraries", params)
            }
        }
    }

    private fun select(window: ToolWindow, id: String, params: JsonObject) {
        val content = window.contentManager.contents.firstOrNull { it.getUserData(VIEW)?.id == id } ?: return
        select(window, content, params)
    }

    private fun select(window: ToolWindow, content: Content, params: JsonObject) {
        content.putUserData(FOCUS_PARAMS, params)
        if (window.contentManager.selectedContent === content) focus(content)
        else window.contentManager.setSelectedContent(content, true)
    }

    /** A graph is a tab of its own, closeable; one per kind, refreshed when it is asked again. */
    private fun openGraph(project: Project, window: ToolWindow, kind: String) {
        val id = "graph:$kind"
        window.contentManager.contents.firstOrNull { it.getUserData(VIEW)?.id == id }?.let {
            select(window, it, JsonObject())
            return
        }
        val session = McppSession.get(project)
        val page = GraphPage(project, kind)
        val title = if (kind == "modules") session.t("Module Graph") else session.t("Dependency Graph")
        val content = ContentFactory.getInstance().createContent(page.component, title, false)
        content.isCloseable = true
        content.putUserData(VIEW, page)
        Disposer.register(content, page)
        window.contentManager.addContent(content)
        select(window, content, JsonObject())
    }

    class Factory : ToolWindowFactory, DumbAware {
        override suspend fun isApplicableAsync(project: Project): Boolean = project.basePath?.let { File(it, "mcpp.toml").isFile } ?: false

        override fun createToolWindowContent(project: Project, toolWindow: ToolWindow) {
            val session = McppSession.get(project)
            fun add(view: McppView, component: javax.swing.JComponent, title: String): Content {
                val content = ContentFactory.getInstance().createContent(component, title, false)
                content.isCloseable = false
                content.putUserData(VIEW, view)
                toolWindow.contentManager.addContent(content)
                return content
            }
            val projectView = ProjectView(project, toolWindow.disposable)
            val libraryView = LibraryView(project, toolWindow.disposable)
            val cacheView = CacheView(project, toolWindow.disposable)
            add(projectView, projectView.component, session.t("Project"))
            add(libraryView, libraryView.component, session.t("Libraries"))
            add(cacheView, cacheView.component, session.t("Cache"))
            toolWindow.contentManager.addContentManagerListener(object : ContentManagerListener {
                override fun selectionChanged(event: ContentManagerEvent) {
                    if (event.operation == ContentManagerEvent.ContentOperation.add) focus(event.content)
                }
            })
            // The strings and the data arrive with the backend.
            session.whenStarted {
                com.intellij.openapi.application.ApplicationManager.getApplication().invokeLater {
                    if (project.isDisposed) return@invokeLater
                    val titles = mapOf("project" to "Project", "libraries" to "Libraries", "cache" to "Cache")
                    for (c in toolWindow.contentManager.contents) c.getUserData(VIEW)?.id?.let { id -> titles[id]?.let { c.displayName = session.t(it) } }
                    toolWindow.contentManager.selectedContent?.let(::focus)
                }
            }
            projectView.refresh()
        }
    }
}
