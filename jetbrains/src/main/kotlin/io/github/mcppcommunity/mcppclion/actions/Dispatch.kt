// The command table's actions in the IDE: every action of the mcpp menu (generated from
// spec/commands.json) is one McppCommandAction, which looks its row up in the backend's table and
// does what the row's `action` says, in the IDE's own terms (run configurations, dialogs, panels).
package io.github.mcppcommunity.mcppclion.actions

import com.google.gson.JsonObject
import com.intellij.execution.ProgramRunnerUtil
import com.intellij.execution.RunManager
import com.intellij.execution.executors.DefaultDebugExecutor
import com.intellij.execution.executors.DefaultRunExecutor
import com.intellij.ide.BrowserUtil
import com.intellij.ide.impl.ProjectUtil
import com.intellij.notification.NotificationAction
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.actionSystem.ActionUpdateThread
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.fileChooser.FileChooser
import com.intellij.openapi.fileChooser.FileChooserDescriptorFactory
import com.intellij.openapi.options.ShowSettingsUtil
import com.intellij.openapi.project.DumbAwareAction
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.Messages
import com.intellij.openapi.ui.popup.JBPopupFactory
import com.intellij.openapi.ui.popup.PopupStep
import com.intellij.openapi.ui.popup.util.BaseListPopupStep
import io.github.mcppcommunity.mcppclion.McppSession
import io.github.mcppcommunity.mcppclion.run.McppRunConfiguration
import io.github.mcppcommunity.mcppclion.run.McppRunConfigurationType
import io.github.mcppcommunity.mcppclion.settings.McppSettings
import io.github.mcppcommunity.mcppclion.webui.McppPanels
import java.io.File
import java.nio.file.Paths

class McppCommandAction : DumbAwareAction() {
    override fun getActionUpdateThread(): ActionUpdateThread = ActionUpdateThread.BGT

    override fun update(e: AnActionEvent) {
        val project = e.project
        e.presentation.isEnabledAndVisible = project != null
        if (project == null) return
        val id = e.actionManager.getId(this) ?: return
        val needsProject = id in setOf("mcpp.build", "mcpp.run", "mcpp.debug", "mcpp.test", "mcpp.clean", "mcpp.pack", "mcpp.cleanProjectArtifacts")
        if (needsProject) e.presentation.isEnabled = McppSession.get(project).root != null
    }

    override fun actionPerformed(e: AnActionEvent) {
        val project = e.project ?: return
        val id = e.actionManager.getId(this) ?: return
        Dispatch.run(project, id, JsonObject())
    }
}

object Dispatch {
    fun run(project: Project, command: String, args: JsonObject) {
        val session = McppSession.get(project)
        session.start().whenComplete { _, error ->
            ApplicationManager.getApplication().invokeLater {
                if (error != null) { session.notify(session.t("mcpp: {0}", (error.cause ?: error).message ?: ""), NotificationType.ERROR); return@invokeLater }
                val entry = session.commands.firstOrNull { it["id"].asString == command }
                if (entry == null) { session.notify(session.t("unknown command {0}", command), NotificationType.ERROR); return@invokeLater }
                perform(project, entry["action"].asJsonObject, args)
            }
        }
    }

    private fun configuration(project: Project, command: String, args: JsonObject) {
        val session = McppSession.get(project)
        val root = args["root"]?.asString ?: session.root ?: return
        val manager = RunManager.getInstance(project)
        val name = "mcpp $command ${File(root).name}" + (args["filter"]?.asString?.let { " $it" } ?: "")
        val settings = manager.findConfigurationByName(name) ?: manager.createConfiguration(name, McppRunConfigurationType.Factory(McppRunConfigurationType())).also {
            manager.addConfiguration(it)
        }
        val c = settings.configuration as McppRunConfiguration
        c.options.command = if (command == "debug") "run" else command
        c.options.root = root
        args["filter"]?.asString?.let { c.options.filter = it }
        args["bin"]?.asString?.let { c.options.bin = it }
        manager.selectedConfiguration = settings
        val executor = if (command == "debug") DefaultDebugExecutor.getDebugExecutorInstance() else DefaultRunExecutor.getRunExecutorInstance()
        ProgramRunnerUtil.executeConfiguration(settings, executor)
    }

    fun perform(project: Project, action: JsonObject, args: JsonObject) {
        val session = McppSession.get(project)
        fun t(key: String, vararg a: Any?) = session.t(key, *a)
        val inner = args["args"]?.takeIf { it.isJsonObject }?.asJsonObject ?: JsonObject()
        inner.entrySet().forEach { (k, v) -> if (!args.has(k)) args.add(k, v) }
        when (action["kind"].asString) {
            "job" -> configuration(project, if (action["job"]?.asString == "test") "test" else "build", args)
            "run" -> configuration(project, "run", args)
            "debug" -> configuration(project, "debug", args)
            "cache" -> cleanup(project, action["plan"].asString)
            "panel" -> McppPanels.show(project, action["panel"].asString, JsonObject().apply {
                action["graph"]?.let { add("graph", it) }
                args["id"]?.let { add("id", it) }
            })
            "cancel" -> session.request("job/list").thenAccept { list ->
                list.asJsonArray.forEach { j -> session.request("job/cancel", JsonObject().apply { add("jobId", j.asJsonObject["id"]) }) }
            }
            "selectTarget" -> selectTarget(project)
            "installToolchain" -> {
                val spec = Messages.showInputDialog(project, "gcc@16.1.0, llvm@23.1.3", t("Install an mcpp toolchain"), null) ?: return
                job(project, "toolchains/install", JsonObject().apply { addProperty("spec", spec) })
            }
            "selectDefaultToolchain" -> selectDefaultToolchain(project)
            "newProject" -> newProject(project)
            "selfCheck" -> session.request("selfCheck").thenAccept { r ->
                val text = r.asJsonObject.getAsJsonArray("checks").joinToString("\n") { c -> "${c.asJsonObject["status"].asString.padEnd(8)} ${c.asJsonObject["message"].asString}" }
                ApplicationManager.getApplication().invokeLater { Messages.showInfoMessage(project, text, t("Environment Self-check")) }
            }
            "languageService" -> languageService(project)
            "rpc" -> job(project, action["method"].asString, JsonObject().apply { session.root?.let { addProperty("root", it) } })
            else -> session.notify(t("This command is not available in this IDE."), NotificationType.INFORMATION)
        }
    }

    /** A job whose narration is not a console's: its end is a notification. */
    private fun job(project: Project, method: String, params: JsonObject) {
        val session = McppSession.get(project)
        session.request(method, params).thenAccept { r ->
            val id = r.asJsonObject["jobId"]?.asString ?: return@thenAccept
            lateinit var off: () -> Unit
            off = session.on("job/finished") { p ->
                if (p.asJsonObject["jobId"]?.asString != id) return@on
                off()
                val status = p.asJsonObject["status"]?.asString ?: "failed"
                session.notify(session.t("mcpp {0}: {1}", method.substringBefore('/'), status), if (status == "ok") NotificationType.INFORMATION else NotificationType.ERROR)
            }
        }
    }

    /** plan -> the IDE's confirmation at the plan's level -> apply. */
    fun cleanup(project: Project, actionName: String) {
        val session = McppSession.get(project)
        fun t(key: String, vararg a: Any?) = session.t(key, *a)
        session.request("cache/plan", JsonObject().apply { addProperty("action", actionName); session.root?.let { addProperty("root", it) } }).thenAccept { r ->
            val plan = r.asJsonObject
            ApplicationManager.getApplication().invokeLater {
                if (plan["refusedUntrusted"]?.asBoolean == true) { session.notify(t("this action needs a trusted workspace"), NotificationType.WARNING); return@invokeLater }
                val confirmation = JsonObject()
                if (plan["level"].asInt > 0) {
                    val preview = plan["preview"]?.takeIf { it.isJsonObject }?.asJsonObject
                    val previewText = when {
                        preview == null -> ""
                        preview["source"]?.asString == "mcpp" -> preview["text"]?.asString ?: ""
                        else -> t("About {0} would be freed ({1} entries).", preview["bytesText"]?.asString ?: "", preview["count"]?.asString ?: "0")
                    }
                    val detail = listOf(plan["detail"].asString, previewText).filter { it.isNotBlank() }.joinToString("\n\n")
                    if (Messages.showOkCancelDialog(project, detail, plan["title"].asString, t("Continue"), Messages.getCancelButton(), Messages.getWarningIcon()) != Messages.OK) return@invokeLater
                    confirmation.addProperty("confirmed", true)
                    confirmation.addProperty("previewShown", preview != null)
                    if (plan["acknowledge"]?.asBoolean == true) {
                        val ack = t("I understand this affects every mcpp project on this machine")
                        if (Messages.showOkCancelDialog(project, ack, plan["title"].asString, ack, Messages.getCancelButton(), Messages.getWarningIcon()) != Messages.OK) return@invokeLater
                        confirmation.addProperty("acknowledged", true)
                    }
                }
                job(project, "cache/apply", JsonObject().apply {
                    addProperty("action", actionName); add("confirmation", confirmation); session.root?.let { addProperty("root", it) }
                })
            }
        }
    }

    private fun selectTarget(project: Project) {
        val session = McppSession.get(project)
        session.request("targets/list", JsonObject().apply { session.root?.let { addProperty("root", it) } }).thenAccept { r ->
            val rows = r.asJsonObject.getAsJsonArray("rows").map { it.asJsonObject }
            ApplicationManager.getApplication().invokeLater {
                val step = object : BaseListPopupStep<JsonObject>(session.t("Select the build target"), rows) {
                    override fun getTextFor(row: JsonObject): String {
                        val platform = row["platformText"]?.asString?.takeIf { it.isNotEmpty() }?.let { " · $it" } ?: ""
                        return (if (row["selected"].asBoolean) "✓ " else "") + row["target"].asString + platform
                    }
                    override fun onChosen(row: JsonObject, finalChoice: Boolean): PopupStep<*>? {
                        McppSettings.get().set("mcpp.build.target", com.google.gson.JsonPrimitive(if (row["host"].asBoolean) "" else row["target"].asString))
                        return FINAL_CHOICE
                    }
                }
                JBPopupFactory.getInstance().createListPopup(step).showCenteredInCurrentWindow(project)
            }
        }
    }

    private fun selectDefaultToolchain(project: Project) {
        val session = McppSession.get(project)
        session.request("toolchains/list").thenAccept { r ->
            val o = r.asJsonObject
            val rows = o.getAsJsonArray("rows").map { it.asJsonObject }.filter { it["target"].asString == o["host"].asString && it["spec"].asString.isNotEmpty() }
            ApplicationManager.getApplication().invokeLater {
                val step = object : BaseListPopupStep<JsonObject>(session.t("Choose the mcpp global default toolchain"), rows) {
                    override fun getTextFor(row: JsonObject): String = (if (row["default"].asBoolean) "✓ " else "") + row["spec"].asString + " (" + row["status"].asString + ")"
                    override fun onChosen(row: JsonObject, finalChoice: Boolean): PopupStep<*>? {
                        session.request("toolchains/setDefault", JsonObject().apply { add("spec", row["spec"]) })
                        return FINAL_CHOICE
                    }
                }
                JBPopupFactory.getInstance().createListPopup(step).showCenteredInCurrentWindow(project)
            }
        }
    }

    private fun newProject(project: Project) {
        val session = McppSession.get(project)
        val folder = FileChooser.chooseFile(FileChooserDescriptorFactory.createSingleFolderDescriptor(), project, null) ?: return
        val name = Messages.showInputDialog(project, session.t("The project's name, which is also its directory"), session.t("New mcpp project (1/2)"), null) ?: return
        session.request("project/new", JsonObject().apply { addProperty("parent", folder.path); addProperty("name", name) }).thenAccept { r ->
            val id = r.asJsonObject["jobId"].asString
            val created = r.asJsonObject["root"].asString
            lateinit var off: () -> Unit
            off = session.on("job/finished") { p ->
                if (p.asJsonObject["jobId"]?.asString != id) return@on
                off()
                if (p.asJsonObject["status"]?.asString == "ok")
                    ApplicationManager.getApplication().invokeLater { ProjectUtil.openOrImport(Paths.get(created), project, false) }
                else session.notify(session.t("mcpp new {0}: {1}", name, p.asJsonObject["status"]?.asString ?: "failed"), NotificationType.ERROR)
            }
        }
    }

    fun languageServiceAction(project: Project, id: String) {
        // The mcppls CLion plugin: its page on the Marketplace when it is listed there, else its releases.
        if (id == "install" || id == "update") {
            ShowSettingsUtil.getInstance().showSettingsDialog(project, "Plugins")
            BrowserUtil.browse("https://github.com/Sunrisepeak/mcpp-language-server/releases")
        }
    }

    private fun languageService(project: Project) {
        val session = McppSession.get(project)
        session.request("languageService/status").thenAccept { r ->
            val s = r.asJsonObject
            val ok = s["state"].asString == "ok"
            val n = NotificationGroupManager.getInstance().getNotificationGroup("mcpp")
                .createNotification("mcpp", s["message"].asString, if (ok) NotificationType.INFORMATION else NotificationType.WARNING)
            s.getAsJsonArray("actions").forEach { a ->
                val o = a.asJsonObject
                n.addAction(NotificationAction.createSimpleExpiring(o["title"].asString) { languageServiceAction(project, o["id"].asString) })
            }
            n.notify(project)
        }
    }
}
