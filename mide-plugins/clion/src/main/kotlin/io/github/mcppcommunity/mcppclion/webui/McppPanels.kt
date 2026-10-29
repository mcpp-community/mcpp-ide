// The "mcpp" tool window: the workbench (Workbench.kt), whose tabs are mcpp-vscode's views -- the
// project tree, the libraries, the cache -- and the panels they open. A request for a panel (the
// command table's `panel` actions) brings it on screen there; the targets and packing are the IDE's
// own popups, as VS Code's are its quick picks.
package io.github.mcppcommunity.mcppclion.webui

import com.google.gson.JsonObject
import com.google.gson.JsonPrimitive
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Key
import com.intellij.openapi.wm.ToolWindow
import com.intellij.openapi.wm.ToolWindowFactory
import com.intellij.openapi.wm.ToolWindowManager
import com.intellij.ui.content.ContentFactory
import io.github.mcppcommunity.mcppclion.actions.Dispatch
import java.io.File

object McppPanels {
    const val TOOL_WINDOW = "mcpp"
    private val WORKBENCH = Key.create<Workbench>("mcpp.workbench")

    fun show(project: Project, panel: String, params: JsonObject = JsonObject()) {
        when (panel) {
            "targets" -> { Dispatch.selectTarget(project); return }
            "pack" -> { Dispatch.pack(project); return }
        }
        val window = ToolWindowManager.getInstance(project).getToolWindow(TOOL_WINDOW) ?: return
        window.activate {
            val workbench = window.contentManager.contents.firstNotNullOfOrNull { it.getUserData(WORKBENCH) } ?: return@activate
            when (panel) {
                "project" -> workbench.show("mcpp.project")
                "cache" -> workbench.show("mcpp.cache")
                "settings" -> workbench.show("settings")
                "graph" -> workbench.show("graph:${params["graph"]?.asString ?: "packages"}")
                "libraries", "library" -> params["id"]?.asString?.let { workbench.show("library", JsonPrimitive(it)) } ?: workbench.show("mcpp.library")
                else -> workbench.show(panel)
            }
        }
    }

    class Factory : ToolWindowFactory, DumbAware {
        override suspend fun isApplicableAsync(project: Project): Boolean = project.basePath?.let { File(it, "mcpp.toml").isFile } ?: false

        override fun createToolWindowContent(project: Project, toolWindow: ToolWindow) {
            val content = ContentFactory.getInstance().createContent(null, "", false)
            val workbench = Workbench(project, "window", toolWindow.disposable)
            content.component = workbench.component
            content.isCloseable = false
            content.putUserData(WORKBENCH, workbench)
            toolWindow.contentManager.addContent(content)
        }
    }
}
