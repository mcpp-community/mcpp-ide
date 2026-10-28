// The shared panels (../webui) in a JCEF browser. The page speaks the backend's protocol through
// this host -- `window.mcppHostSend` (a JBCefJSQuery) out, `window.mcppHostReceive` in -- and asks
// it for what only the IDE does: open a file, confirm, store a setting, run a command.
package io.github.mcppcommunity.mcppclion.webui

import com.google.gson.Gson
import com.google.gson.JsonElement
import com.google.gson.JsonNull
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.intellij.ide.BrowserUtil
import com.intellij.ide.actions.RevealFileAction
import com.intellij.notification.NotificationType
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.PathManager
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.ide.CopyPasteManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.Messages
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.ui.JBColor
import com.intellij.ui.jcef.JBCefApp
import com.intellij.ui.jcef.JBCefBrowser
import com.intellij.ui.jcef.JBCefBrowserBase
import com.intellij.ui.jcef.JBCefJSQuery
import com.intellij.util.ui.UIUtil
import io.github.mcppcommunity.mcppclion.McppSession
import io.github.mcppcommunity.mcppclion.actions.Dispatch
import io.github.mcppcommunity.mcppclion.settings.McppSettings
import org.cef.browser.CefBrowser
import org.cef.browser.CefFrame
import org.cef.handler.CefLoadHandlerAdapter
import java.awt.Color
import java.awt.datatransfer.StringSelection
import java.io.File
import javax.swing.JComponent
import javax.swing.JLabel
import javax.swing.SwingConstants

class WebuiPanel(private val project: Project, private val panel: String, private val params: JsonObject, parent: Disposable) : Disposable {
    private val session = McppSession.get(project)
    private val browser: JBCefBrowser? = if (JBCefApp.isSupported()) JBCefBrowser() else null
    private val query: JBCefJSQuery? = browser?.let { JBCefJSQuery.create(it as JBCefBrowserBase) }
    private val unsubscribe: () -> Unit

    val component: JComponent = browser?.component ?: JLabel(session.t("This IDE runs without JCEF; the mcpp panels need it."), SwingConstants.CENTER)

    init {
        Disposer.register(parent, this)
        unsubscribe = session.on("*") { message -> post(JsonObject().apply { addProperty("type", "notify"); add("method", message.asJsonObject["method"]); add("params", message.asJsonObject["params"]) }) }
        if (browser != null && query != null) {
            query.addHandler { text -> receive(text); null }
            browser.jbCefClient.addLoadHandler(object : CefLoadHandlerAdapter() {
                override fun onLoadEnd(b: CefBrowser, frame: CefFrame, status: Int) {
                    if (!frame.isMain) return
                    b.executeJavaScript("window.mcppHostSend = function (text) { ${query.inject("text")} }; window.mcppHostAttached && window.mcppHostAttached();", b.url, 0)
                }
            }, browser.cefBrowser)
            browser.loadURL(page().toURI().toString())
        }
    }

    // The page and its scripts, written once per plugin version where the browser can load them.
    private fun page(): File {
        // One directory per build of the plugin: its jar's name and time identify it.
        val jar = runCatching { File(WebuiPanel::class.java.protectionDomain.codeSource.location.toURI()) }.getOrNull()
        val version = jar?.takeIf { it.isFile }?.let { "${it.nameWithoutExtension}-${it.lastModified()}" } ?: "dev"
        val dir = File(PathManager.getSystemPath(), "mcpp-clion/webui-$version")
        val index = javaClass.getResource("/webui/index.html")?.readText() ?: error("webui/index.html is not in the plugin")
        val files = Regex("\\{\\{base}}([\\w./-]+)").findAll(index).map { it.groupValues[1] }.toSet()
        for (f in files) {
            val target = File(dir, f)
            if (target.isFile && version != "dev") continue
            target.parentFile.mkdirs()
            javaClass.getResourceAsStream("/webui/$f")?.use { input -> target.outputStream().use { input.copyTo(it) } }
        }
        val html = File(dir, "panel.html")
        html.writeText(index.replace("{{base}}", "").replace("{{csp}}", "default-src 'self' 'unsafe-inline' file:")
            .replace("{{nonce}}", "").replace("{{lang}}", com.intellij.DynamicBundle.getLocale().language))
        return html
    }

    private fun css(c: Color) = "rgb(${c.red}, ${c.green}, ${c.blue})"

    private fun theme(): JsonObject = JsonObject().apply {
        addProperty("--mcpp-fg", css(UIUtil.getLabelForeground()))
        addProperty("--mcpp-bg", css(UIUtil.getPanelBackground()))
        addProperty("--mcpp-muted", css(UIUtil.getContextHelpForeground()))
        addProperty("--mcpp-border", css(JBColor.border()))
        addProperty("--mcpp-card", css(UIUtil.getListBackground()))
        addProperty("--mcpp-input", css(UIUtil.getTextFieldBackground()))
        addProperty("--mcpp-accent", css(JBColor.namedColor("Button.default.startBackground", JBColor(0x3574F0, 0x3574F0))))
        addProperty("--mcpp-secondary", css(JBColor.namedColor("Button.startBackground", JBColor(0xEBECF0, 0x3C3F41))))
        addProperty("--mcpp-secondary-fg", css(UIUtil.getLabelForeground()))
        addProperty("--mcpp-font", "\"${UIUtil.getLabelFont().family}\", system-ui, sans-serif")
        addProperty("--mcpp-font-size", "${UIUtil.getLabelFont().size}px")
    }

    private fun post(message: JsonObject) {
        val b = browser ?: return
        val js = "window.mcppHostReceive && window.mcppHostReceive(${message});"
        ApplicationManager.getApplication().invokeLater { b.cefBrowser.executeJavaScript(js, b.cefBrowser.url, 0) }
    }

    private fun receive(text: String) {
        val m = runCatching { JsonParser.parseString(text).asJsonObject }.getOrNull() ?: return
        when (m["type"]?.asString) {
            "ready" -> post(JsonObject().apply {
                addProperty("type", "init"); addProperty("panel", panel); add("params", params)
                add("bundle", Gson().toJsonTree(session.strings)); add("theme", theme()); addProperty("editor", "jetbrains")
            })
            "rpc" -> session.request(m["method"].asString, m["params"] ?: JsonObject()).whenComplete { result, error ->
                if (error != null) post(JsonObject().apply {
                    addProperty("type", "rpcError"); add("id", m["id"])
                    add("error", JsonObject().apply { addProperty("message", (error.cause ?: error).message ?: "failed") })
                })
                else post(JsonObject().apply { addProperty("type", "rpcResult"); add("id", m["id"]); add("result", result) })
            }
            "host" -> ApplicationManager.getApplication().invokeLater {
                val result = runCatching { host(m["action"].asString, m["params"]?.asJsonObject ?: JsonObject()) }
                post(JsonObject().apply {
                    add("id", m["id"])
                    result.onSuccess { addProperty("type", "hostResult"); add("result", it) }
                    result.onFailure { e -> addProperty("type", "hostError"); add("error", JsonObject().apply { addProperty("message", e.message ?: "failed") }) }
                })
            }
        }
    }

    private fun host(action: String, p: JsonObject): JsonElement {
        val settings = McppSettings.get()
        return when (action) {
            "settings" -> settings.values()
            "setSetting" -> { settings.set(p["key"].asString, p["value"]); JsonParser.parseString("true") }
            "resetSettings" -> { p.getAsJsonArray("keys").forEach { settings.set(it.asString, null) }; JsonParser.parseString("true") }
            "migrateSettings" -> {
                p.getAsJsonArray("renames").forEach { r ->
                    val o = r.asJsonObject
                    settings.get(o["from"].asString)?.let { v -> settings.set(o["to"].asString, v) }
                }
                JsonParser.parseString("true")
            }
            "openFile" -> {
                val file = LocalFileSystem.getInstance().refreshAndFindFileByPath(p["path"].asString)
                if (file != null) OpenFileDescriptor(project, file, maxOf(0, (p["line"]?.asInt ?: 1) - 1), 0).navigate(true)
                JsonParser.parseString((file != null).toString())
            }
            "openExternal" -> { BrowserUtil.browse(p["url"].asString); JsonNull.INSTANCE }
            "reveal" -> { RevealFileAction.openFile(File(p["path"].asString)); JsonNull.INSTANCE }
            "copy" -> { CopyPasteManager.getInstance().setContents(StringSelection(p["text"].asString)); JsonNull.INSTANCE }
            "notify" -> {
                val type = when (p["level"]?.asString) { "error" -> NotificationType.ERROR; "warning" -> NotificationType.WARNING; else -> NotificationType.INFORMATION }
                session.notify(p["message"].asString, type); JsonNull.INSTANCE
            }
            "openPanel" -> {
                val rest = p.deepCopy().apply { remove("panel") }
                McppPanels.show(project, p["panel"].asString, rest); JsonNull.INSTANCE
            }
            "runCommand" -> { Dispatch.run(project, p["command"].asString, p); JsonNull.INSTANCE }
            "confirm" -> {
                val detail = listOfNotNull(p["detail"]?.asString, p["preview"]?.asString?.takeIf { it.isNotBlank() }).joinToString("\n\n")
                val yes = Messages.showOkCancelDialog(project, detail, p["title"].asString, session.t("Continue"), Messages.getCancelButton(), Messages.getWarningIcon()) == Messages.OK
                var acknowledged = false
                if (yes && p["acknowledge"]?.asBoolean == true) {
                    val ack = session.t("I understand this affects every mcpp project on this machine")
                    acknowledged = Messages.showOkCancelDialog(project, ack, p["title"].asString, ack, Messages.getCancelButton(), Messages.getWarningIcon()) == Messages.OK
                }
                JsonObject().apply { addProperty("confirmed", yes && (acknowledged || p["acknowledge"]?.asBoolean != true)); addProperty("acknowledged", acknowledged) }
            }
            "languageService" -> { Dispatch.languageServiceAction(project, p["action"]?.asString ?: "install"); JsonNull.INSTANCE }
            else -> error("unknown host action $action")
        }
    }

    override fun dispose() {
        unsubscribe()
        query?.let { Disposer.dispose(it) }
        browser?.let { Disposer.dispose(it) }
    }
}
