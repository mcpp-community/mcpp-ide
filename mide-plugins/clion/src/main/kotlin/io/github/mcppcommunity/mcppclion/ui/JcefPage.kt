// A page of webui/ in JCEF (mcpp design 2026-10-30 part 2 §9): the plugin's copy of the page's files,
// a browser that loads one of them, `mcppRender` to hand the page the backend's answer and its strings,
// and `mcppHostSend` for what the page asks of the IDE. A page reaches nothing else. Without JCEF the
// tab says why.
package io.github.mcppcommunity.mcppclion.ui

import com.google.gson.JsonObject
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.PathManager
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.ui.components.JBTextArea
import com.intellij.ui.jcef.JBCefApp
import com.intellij.ui.jcef.JBCefBrowser
import com.intellij.ui.jcef.JBCefBrowserBase
import com.intellij.ui.jcef.JBCefJSQuery
import com.intellij.util.ui.JBUI
import io.github.mcppcommunity.mcppclion.McppSession
import org.cef.browser.CefBrowser
import org.cef.browser.CefFrame
import org.cef.handler.CefLoadHandlerAdapter
import java.io.File
import javax.swing.JComponent

abstract class JcefPage(protected val project: Project, private val file: String, withoutJcef: String) : McppView, Disposable {
    protected val session = McppSession.get(project)
    private val browser: JBCefBrowser? = if (JBCefApp.isSupported()) JBCefBrowser() else null
    private val query: JBCefJSQuery? = browser?.let { JBCefJSQuery.create(it as JBCefBrowserBase) }
    @Volatile private var loaded = false
    private var pending: String? = null
    // Without JCEF the page says why. The tool window sits on the left beside Project, narrower than
    // the sentence, so the text wraps; its accessible name is the whole sentence.
    val component: JComponent = browser?.component ?: unavailable(withoutJcef)

    init {
        if (browser != null && query != null) {
            Disposer.register(this, browser)
            Disposer.register(this, query)
            query.addHandler { text -> received(text); null }
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

    /** What the page sent (`mcppHostSend`). */
    protected abstract fun received(text: String)

    /** The page's files, written once per build of the plugin where the browser can load them. */
    private fun page(): File {
        val jar = runCatching { File(JcefPage::class.java.protectionDomain.codeSource.location.toURI()) }.getOrNull()
        val version = jar?.takeIf { it.isFile }?.let { "${it.nameWithoutExtension}-${it.lastModified()}" } ?: "dev"
        val dir = File(PathManager.getSystemPath(), "mcpp-clion/webui-$version")
        val index = javaClass.getResource("/webui/files.txt")?.readText() ?: error("the pages are not in the plugin (webui/files.txt)")
        for (f in index.lines().filter { it.isNotBlank() }) {
            val target = File(dir, f)
            if (target.isFile && version != "dev") continue
            target.parentFile.mkdirs()
            javaClass.getResourceAsStream("/webui/$f")?.use { input -> target.outputStream().use { input.copyTo(it) } }
        }
        return File(dir, file)
    }

    /** Hands the page what it draws, once it has loaded. */
    protected fun show(page: JsonObject) {
        val text = page.toString()
        ApplicationManager.getApplication().invokeLater {
            if (loaded) render(text) else pending = text
        }
    }

    private fun render(page: String) {
        val b = browser ?: return
        b.cefBrowser.executeJavaScript("window.mcppRender && window.mcppRender($page);", b.cefBrowser.url, 0)
    }

    /** A file of the project in an editor, at a 0-based line. */
    protected fun openSource(source: String, line: Int = 0) {
        val root = session.root ?: return
        val path = File(source).let { if (it.isAbsolute) it else File(root, source) }.path
        ApplicationManager.getApplication().invokeLater {
            LocalFileSystem.getInstance().refreshAndFindFileByPath(path)?.let { OpenFileDescriptor(project, it, line, 0).navigate(true) }
        }
    }

    override fun dispose() {}
}

private fun unavailable(text: String): JComponent = JBTextArea(text).apply {
    isEditable = false
    lineWrap = true
    wrapStyleWord = true
    isOpaque = false
    border = JBUI.Borders.empty(12)
    accessibleContext.accessibleName = text
}
