// A graph of the project -- its packages or its modules -- as the page webui/graph.html draws it in
// JCEF (mcpp design 2026-10-30 part 2 §9). The page receives the backend's `graph/packages` or
// `graph/modules` and the strings it shows, and asks the IDE to open a module's source; it reaches
// nothing else. Without JCEF the tab says so.
package io.github.mcppcommunity.mcppclion.ui

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.PathManager
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.ui.JBColor
import com.intellij.ui.components.JBLabel
import com.intellij.ui.jcef.JBCefApp
import com.intellij.ui.jcef.JBCefBrowser
import com.intellij.ui.jcef.JBCefBrowserBase
import com.intellij.ui.jcef.JBCefJSQuery
import io.github.mcppcommunity.mcppclion.McppSession
import org.cef.browser.CefBrowser
import org.cef.browser.CefFrame
import org.cef.handler.CefLoadHandlerAdapter
import java.io.File
import javax.swing.JComponent
import javax.swing.SwingConstants

class GraphPage(private val project: Project, private val kind: String) : McppView, Disposable {
    override val id = "graph:$kind"
    private val session = McppSession.get(project)
    private val browser: JBCefBrowser? = if (JBCefApp.isSupported()) JBCefBrowser() else null
    private val query: JBCefJSQuery? = browser?.let { JBCefJSQuery.create(it as JBCefBrowserBase) }
    @Volatile private var loaded = false
    private var pending: String? = null
    val component: JComponent = browser?.component ?: JBLabel(session.t("This IDE runs without JCEF, which draws the graph."), SwingConstants.CENTER)

    init {
        if (browser != null && query != null) {
            Disposer.register(this, browser)
            Disposer.register(this, query)
            query.addHandler { text -> open(text); null }
            browser.jbCefClient.addLoadHandler(object : CefLoadHandlerAdapter() {
                override fun onLoadEnd(b: CefBrowser, frame: CefFrame, status: Int) {
                    if (!frame.isMain) return
                    b.executeJavaScript("window.mcppHostSend = function (text) { ${query.inject("text")} };", b.url, 0)
                    ApplicationManager.getApplication().invokeLater {
                        loaded = true
                        pending?.let { render(it) }
                        pending = null
                    }
                }
            }, browser.cefBrowser)
            browser.loadURL(page().toURI().toString())
        }
    }

    /** The page's files, written once per build of the plugin where the browser can load them. */
    private fun page(): File {
        val jar = runCatching { File(GraphPage::class.java.protectionDomain.codeSource.location.toURI()) }.getOrNull()
        val version = jar?.takeIf { it.isFile }?.let { "${it.nameWithoutExtension}-${it.lastModified()}" } ?: "dev"
        val dir = File(PathManager.getSystemPath(), "mcpp-clion/webui-$version")
        val index = javaClass.getResource("/webui/files.txt")?.readText() ?: error("the graph page is not in the plugin (webui/files.txt)")
        for (f in index.lines().filter { it.isNotBlank() }) {
            val target = File(dir, f)
            if (target.isFile && version != "dev") continue
            target.parentFile.mkdirs()
            javaClass.getResourceAsStream("/webui/$f")?.use { input -> target.outputStream().use { input.copyTo(it) } }
        }
        return File(dir, "graph.html")
    }

    override fun refresh() {
        val root = session.root ?: return
        session.request("graph/$kind", JsonObject().apply { addProperty("root", root) }).whenComplete { graph, error ->
            val page = JsonObject().apply {
                addProperty("kind", kind)
                add("graph", graph?.takeIf { it.isJsonObject } ?: JsonObject().apply { add("nodes", com.google.gson.JsonArray()); add("edges", com.google.gson.JsonArray()) })
                addProperty("title", if (kind == "modules") session.t("Module Graph") else session.t("Dependency Graph"))
                addProperty("empty", error?.let { session.t("mcpp: {0}", (it.cause ?: it).message ?: "") } ?: session.t("Nothing to draw yet: build the project first."))
                addProperty("truncated", session.t("Only the first {0} nodes are drawn."))
                addProperty("dark", !JBColor.isBright())
            }
            ApplicationManager.getApplication().invokeLater {
                if (loaded) render(page.toString()) else pending = page.toString()
            }
        }
    }

    private fun render(page: String) {
        val b = browser ?: return
        b.cefBrowser.executeJavaScript("window.mcppRender && window.mcppRender($page);", b.cefBrowser.url, 0)
    }

    /** `{"open": "<source>"}`: the module's source in an editor. */
    private fun open(text: String) {
        val source = runCatching { JsonParser.parseString(text).asJsonObject["open"]?.asString }.getOrNull() ?: return
        val root = session.root ?: return
        val path = File(source).let { if (it.isAbsolute) it else File(root, source) }.path
        ApplicationManager.getApplication().invokeLater {
            LocalFileSystem.getInstance().refreshAndFindFileByPath(path)?.let { OpenFileDescriptor(project, it).navigate(true) }
        }
    }

    override fun dispose() {}
}
