// The mcpp item of the status bar (mcpp design 2026-10-30 part 2 §6.1): what VS Code's status item
// shows -- the project's mcpp version, and the job that runs -- with the same quick menu on a click.
package io.github.mcppcommunity.mcppclion

import com.google.gson.JsonObject
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.popup.JBPopupFactory
import com.intellij.openapi.ui.popup.PopupStep
import com.intellij.openapi.ui.popup.util.BaseListPopupStep
import com.intellij.openapi.wm.StatusBar
import com.intellij.openapi.wm.StatusBarWidget
import com.intellij.openapi.wm.StatusBarWidgetFactory
import com.intellij.util.Consumer
import io.github.mcppcommunity.mcppclion.actions.Dispatch
import java.awt.Component
import java.awt.event.MouseEvent
import java.io.File

class McppStatusBarFactory : StatusBarWidgetFactory {
    override fun getId(): String = ID
    override fun getDisplayName(): String = "mcpp"
    override fun isAvailable(project: Project): Boolean = project.basePath?.let { File(it, "mcpp.toml").isFile } == true
    override fun createWidget(project: Project): StatusBarWidget = McppStatusBarWidget(project)

    companion object {
        const val ID = "io.github.mcppcommunity.mcppclion.status"
    }
}

class McppStatusBarWidget(private val project: Project) : StatusBarWidget, StatusBarWidget.TextPresentation {
    private var bar: StatusBar? = null
    @Volatile private var version: String = ""
    @Volatile private var program: String = ""
    @Volatile private var running: String = ""
    private val subscriptions = mutableListOf<() -> Unit>()

    override fun ID(): String = McppStatusBarFactory.ID
    override fun getPresentation(): StatusBarWidget.WidgetPresentation = this

    override fun install(statusBar: StatusBar) {
        bar = statusBar
        val session = McppSession.get(project)
        subscriptions += session.on("job/started") { p -> running = p.asJsonObject["kind"]?.asString ?: "job"; update() }
        subscriptions += session.on("job/finished") { running = ""; update() }
        subscriptions += session.on("project/mcppChanged") { ask() }
        ask()
    }

    /** The project's mcpp, as the project asks for it (project/mcpp). */
    private fun ask() {
        val session = McppSession.get(project)
        val root = session.root ?: return
        session.request("project/mcpp", JsonObject().apply { addProperty("root", root) }).thenAccept { r ->
            val o = r.asJsonObject
            version = if (o["state"]?.asString == "ok") o["version"]?.asString ?: "" else o["state"]?.asString ?: ""
            program = o["program"]?.asString ?: ""
            update()
        }
    }

    private fun update() = ApplicationManager.getApplication().invokeLater { bar?.updateWidget(ID()) }

    override fun getText(): String = buildString {
        append("mcpp")
        if (version.isNotEmpty()) append(" ").append(version)
        if (running.isNotEmpty()) append(" · ").append(running).append("…")
    }

    override fun getTooltipText(): String = McppSession.get(project).t("mcpp {0} at {1}", version, program)

    override fun getAlignment(): Float = Component.LEFT_ALIGNMENT

    override fun getClickConsumer(): Consumer<MouseEvent> = Consumer { event ->
        val session = McppSession.get(project)
        session.request("menu/quick", JsonObject().apply { session.root?.let { addProperty("root", it) } }).thenAccept { r ->
            val rows = r.asJsonObject.getAsJsonArray("items")?.map { it.asJsonObject }?.filter { it["enabled"]?.asBoolean != false } ?: emptyList()
            ApplicationManager.getApplication().invokeLater {
                JBPopupFactory.getInstance().createListPopup(object : BaseListPopupStep<JsonObject>("mcpp", rows) {
                    override fun getTextFor(row: JsonObject): String = row["title"]?.asString ?: row["command"].asString
                    override fun onChosen(row: JsonObject, finalChoice: Boolean): PopupStep<*>? {
                        Dispatch.run(project, row["command"].asString, JsonObject())
                        return FINAL_CHOICE
                    }
                }).showUnderneathOf(event.component)
            }
        }
    }

    override fun dispose() {
        subscriptions.forEach { it() }
        subscriptions.clear()
        bar = null
    }
}
