// A page of webui/ (mcpp design 2026-10-30 part 2 §9): the plugin's copy of the page's files, a page
// host that loads one of them, `mcppRender` to hand the page the backend's answer and its strings, and
// `mcppHostSend` for what the page asks of the IDE. A page reaches nothing else. The host is the
// browser the IDE gives (PageHost.kt); without one the tab says why, in the session's language when
// it is shown.
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
import com.intellij.util.ui.JBUI
import io.github.mcppcommunity.mcppclion.McppSession
import java.io.File
import javax.swing.JComponent

abstract class WebPage(protected val project: Project, private val file: String, private val withoutBrowser: String) : McppView, Disposable {
    protected val session = McppSession.get(project)
    @Volatile private var loaded = false
    private var pending: String? = null
    private val host: PageHost? = PageHostFactory.create({ text -> received(text) }) {
        loaded = true
        pending?.let { render(it) }
        pending = null
    }
    // Without a host the page says why. The tool window sits on the left beside Project, narrower than
    // the sentence, so the text wraps; its accessible name is the whole sentence.
    private val explanation: JBTextArea? = if (host == null) unavailable(session.t(withoutBrowser)) else null
    val component: JComponent = host?.component ?: explanation!!

    init {
        host?.let {
            Disposer.register(this, it)
            it.load(page().toURI().toString())
        }
    }

    /** What the page sent (`mcppHostSend`). */
    protected abstract fun received(text: String)

    /** Asks the backend for what the page draws and hands it to [show]. */
    protected abstract fun draw()

    final override fun refresh() {
        explanation?.let {
            it.text = session.t(withoutBrowser)
            it.accessibleContext.accessibleName = it.text
        }
        if (host != null) draw()
    }

    /** The page's files, written once per build of the plugin where the browser can load them. */
    private fun page(): File {
        val jar = runCatching { File(WebPage::class.java.protectionDomain.codeSource.location.toURI()) }.getOrNull()
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
        host?.execute("window.mcppRender && window.mcppRender($page);")
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

private fun unavailable(text: String): JBTextArea = JBTextArea(text).apply {
    isEditable = false
    lineWrap = true
    wrapStyleWord = true
    isOpaque = false
    border = JBUI.Borders.empty(12)
    accessibleContext.accessibleName = text
}
