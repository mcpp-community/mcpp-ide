// JCEF's page host: a JBCefBrowser, a query the page's `mcppHostSend` reaches, and a load handler
// that injects it. This package is the only one that names JCEF's classes; mcpp-jcef.xml registers
// its factory, and the platform reads that file only when the JCEF plugin is loaded (PageHost.kt).
package io.github.mcppcommunity.mcppclion.ui.jcef

import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.util.Disposer
import com.intellij.ui.jcef.JBCefApp
import com.intellij.ui.jcef.JBCefBrowser
import com.intellij.ui.jcef.JBCefBrowserBase
import com.intellij.ui.jcef.JBCefJSQuery
import io.github.mcppcommunity.mcppclion.ui.PageHost
import io.github.mcppcommunity.mcppclion.ui.PageHostFactory
import org.cef.browser.CefBrowser
import org.cef.browser.CefFrame
import org.cef.handler.CefLoadHandlerAdapter
import javax.swing.JComponent

class JcefPageHost private constructor(received: (String) -> Unit, loaded: () -> Unit) : PageHost {
    private val browser = JBCefBrowser()
    private val query = JBCefJSQuery.create(browser as JBCefBrowserBase)
    override val component: JComponent get() = browser.component

    init {
        Disposer.register(this, browser)
        Disposer.register(this, query)
        query.addHandler { text -> received(text); null }
        browser.jbCefClient.addLoadHandler(object : CefLoadHandlerAdapter() {
            override fun onLoadEnd(b: CefBrowser, frame: CefFrame, status: Int) {
                if (!frame.isMain) return
                b.executeJavaScript("window.mcppHostSend = function (text) { ${query.inject("text")} };", b.url, 0)
                ApplicationManager.getApplication().invokeLater { loaded() }
            }
        }, browser.cefBrowser)
    }

    override fun load(url: String) = browser.loadURL(url)

    override fun execute(script: String) = browser.cefBrowser.executeJavaScript(script, browser.cefBrowser.url, 0)

    override fun dispose() {}

    /** No host while JCEF is switched off (`ide.browser.jcef.enabled=false`) or cannot start here. */
    class Factory : PageHostFactory {
        override fun create(received: (String) -> Unit, loaded: () -> Unit): PageHost? =
            if (JBCefApp.isSupported()) JcefPageHost(received, loaded) else null
    }
}
