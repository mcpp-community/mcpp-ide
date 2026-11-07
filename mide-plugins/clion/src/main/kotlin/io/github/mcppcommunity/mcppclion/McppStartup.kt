// When an mcpp project opens: the session starts; the language service is reported when it is not
// what mcpp projects need; and a compile_commands.json an earlier mcpp left at the root, which would
// let CLion's compilation-database support take the project over from mcppls, is pointed out.
package io.github.mcppcommunity.mcppclion

import com.intellij.ide.util.PropertiesComponent
import com.intellij.notification.NotificationAction
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.startup.ProjectActivity
import com.intellij.openapi.wm.ToolWindowManager
import io.github.mcppcommunity.mcppclion.actions.Dispatch
import java.io.File

/**
 * The mcpp tool window on the left stripe, beside Project, as VS Code's activity bar carries mcpp
 * (design 2026-11-07 §4.9). The new UI puts a plugin's tool window under "More Tool Windows" until
 * the user places it, so the first mcpp project opened places it once; what the user does with the
 * button afterwards is kept.
 */
private fun showToolWindowButton(project: Project) {
    val placed = "io.github.mcppcommunity.mcppclion.toolWindowPlaced"
    val properties = PropertiesComponent.getInstance()
    if (properties.getBoolean(placed)) return
    ApplicationManager.getApplication().invokeLater {
        if (project.isDisposed) return@invokeLater
        val window = ToolWindowManager.getInstance(project).getToolWindow("mcpp") ?: return@invokeLater
        window.isShowStripeButton = true
        properties.setValue(placed, true)
    }
}

class McppStartup : ProjectActivity {
    override suspend fun execute(project: Project) {
        val base = project.basePath ?: return
        if (!File(base, "mcpp.toml").isFile) return
        val session = McppSession.get(project)
        showToolWindowButton(project)
        // mcpp.toml and build.mcpp reach the backend's rules layer as they are opened and edited.
        io.github.mcppcommunity.mcppclion.editor.McppDocuments.get(project).install()
        session.start().thenAccept { init ->
            init.get("languageService")?.takeIf { it.isJsonObject }?.asJsonObject?.let { ls ->
                if (ls["state"].asString == "ok") return@let
                val n = NotificationGroupManager.getInstance().getNotificationGroup("mcpp")
                    .createNotification("mcpp", ls["message"].asString, NotificationType.WARNING)
                ls.getAsJsonArray("actions").forEach { a ->
                    n.addAction(NotificationAction.createSimpleExpiring(a.asJsonObject["title"].asString) { Dispatch.languageServiceAction(project, a.asJsonObject["id"].asString) })
                }
                n.notify(project)
            }
        }
        val stale = File(base, "compile_commands.json")
        if (stale.isFile) {
            NotificationGroupManager.getInstance().getNotificationGroup("mcpp").createNotification("mcpp",
                session.t("compile_commands.json at the project root was written by an earlier mcpp. CLion may open the project as a compilation database instead of leaving its C++ to mcppls; mcpp now keeps the database under target/."),
                NotificationType.WARNING)
                .addAction(NotificationAction.createSimpleExpiring(session.t("Remove it")) { stale.delete() })
                .notify(project)
        }
    }
}
