// The panels' places in the IDE: the "mcpp" tool window holds the project, libraries, cache and
// targets panels; any other panel (a graph, pack, settings, a package's page) opens there as a tab
// of its own, which a second request for the same panel selects again.
package io.github.mcppcommunity.mcppclion.webui

import com.google.gson.JsonObject
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.project.Project
import com.intellij.openapi.wm.ToolWindow
import com.intellij.openapi.wm.ToolWindowFactory
import com.intellij.openapi.wm.ToolWindowManager
import com.intellij.ui.content.ContentFactory
import io.github.mcppcommunity.mcppclion.McppSession
import java.io.File

object McppPanels {
    const val TOOL_WINDOW = "mcpp"
    private val defaults = listOf("project", "libraries", "cache", "targets")

    fun title(session: McppSession, panel: String, params: JsonObject): String = when (panel) {
        "project" -> session.t("Project")
        "libraries", "library" -> params["id"]?.asString ?: session.t("Libraries")
        "cache" -> session.t("Cache")
        "targets" -> session.t("Targets and Platforms")
        "settings" -> session.t("Settings")
        "pack" -> session.t("Pack")
        "graph" -> if (params["graph"]?.asString == "modules") session.t("Module graph") else session.t("Dependency graph")
        else -> panel
    }

    private fun key(panel: String, params: JsonObject) = "$panel:${params["graph"]?.asString ?: ""}:${params["id"]?.asString ?: ""}"

    fun add(project: Project, window: ToolWindow, panel: String, params: JsonObject, closeable: Boolean) {
        val session = McppSession.get(project)
        val content = ContentFactory.getInstance().createContent(null, title(session, panel, params), false)
        val view = WebuiPanel(project, if (panel == "library") "libraries" else panel, params, content)
        content.component = view.component
        content.isCloseable = closeable
        content.putUserData(KEY, key(panel, params))
        window.contentManager.addContent(content)
    }

    fun show(project: Project, panel: String, params: JsonObject = JsonObject()) {
        val window = ToolWindowManager.getInstance(project).getToolWindow(TOOL_WINDOW) ?: return
        if (!params.has("root")) McppSession.get(project).root?.let { params.addProperty("root", it) }
        val wanted = key(panel, params)
        val existing = window.contentManager.contents.firstOrNull { it.getUserData(KEY) == wanted }
        if (existing == null) add(project, window, panel, params, closeable = panel !in defaults)
        val content = window.contentManager.contents.first { it.getUserData(KEY) == wanted }
        window.contentManager.setSelectedContent(content)
        window.activate(null)
    }

    private val KEY = com.intellij.openapi.util.Key.create<String>("mcpp.panel")

    class Factory : ToolWindowFactory, DumbAware {
        override suspend fun isApplicableAsync(project: Project): Boolean = project.basePath?.let { File(it, "mcpp.toml").isFile } ?: false

        override fun createToolWindowContent(project: Project, toolWindow: ToolWindow) {
            val params = JsonObject().apply { McppSession.get(project).root?.let { addProperty("root", it) } }
            for (panel in defaults) add(project, toolWindow, panel, params.deepCopy(), closeable = false)
        }
    }
}
