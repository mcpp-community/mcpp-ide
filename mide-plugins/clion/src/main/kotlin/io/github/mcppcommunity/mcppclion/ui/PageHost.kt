// The browser a page of webui/ is drawn in, as the core of the plugin knows it: a component, a page
// to load, a script to run in it, and what the page sends back. The core names no browser. JCEF's
// host (ui/jcef) is registered only by mcpp-jcef.xml, which the platform reads when the JCEF plugin
// is loaded, so a CLion without that plugin never links JCEF's classes; with no host, or a host
// that cannot draw now (JCEF switched off), the page's tab says why (#13).
package io.github.mcppcommunity.mcppclion.ui

import com.intellij.openapi.Disposable
import com.intellij.openapi.extensions.ExtensionPointName
import javax.swing.JComponent

interface PageHost : Disposable {
    val component: JComponent
    fun load(url: String)
    /** Runs a script in the page that is loaded. */
    fun execute(script: String)
}

interface PageHostFactory {
    /**
     * A host, or null when this IDE cannot draw a page now. `received` gets what the page sends
     * (`mcppHostSend`); `loaded` runs on the EDT each time the page has loaded.
     */
    fun create(received: (String) -> Unit, loaded: () -> Unit): PageHost?

    companion object {
        val EP: ExtensionPointName<PageHostFactory> = ExtensionPointName.create("io.github.mcpp-community.mcpp-clion.pageHost")

        fun create(received: (String) -> Unit, loaded: () -> Unit): PageHost? =
            EP.extensionList.firstNotNullOfOrNull { runCatching { it.create(received, loaded) }.getOrNull() }
    }
}
