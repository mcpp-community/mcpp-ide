// Settings | Tools | mcpp: the shared settings panel, the same one VS Code shows.
package io.github.mcppcommunity.mcppclion.settings

import com.google.gson.JsonObject
import com.intellij.openapi.Disposable
import com.intellij.openapi.options.Configurable
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import io.github.mcppcommunity.mcppclion.webui.WebuiPanel
import javax.swing.JComponent

class McppConfigurable(private val project: Project) : Configurable {
    private var disposable: Disposable? = null

    override fun getDisplayName(): String = "mcpp"

    override fun createComponent(): JComponent {
        val d = Disposer.newDisposable("mcpp settings")
        disposable = d
        return WebuiPanel(project, "settings", JsonObject(), d).component
    }

    // Every change is stored as it is made (the panel writes through the host).
    override fun isModified(): Boolean = false
    override fun apply() {}

    override fun disposeUIResources() {
        disposable?.let { Disposer.dispose(it) }
        disposable = null
    }
}
