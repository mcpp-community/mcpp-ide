// Run configurations from context, and the ▶ beside `main`: a C++ file of an mcpp project whose
// `main` is a test's (mcpp test --list names the file) runs that test; any other `main` runs the
// project's program. Both come from the same producer, so Run and Debug are offered alike.
package io.github.mcppcommunity.mcppclion.run

import com.intellij.execution.actions.ConfigurationContext
import com.intellij.execution.actions.LazyRunConfigurationProducer
import com.intellij.execution.configurations.ConfigurationFactory
import com.intellij.execution.configurations.ConfigurationTypeUtil
import com.intellij.execution.lineMarker.ExecutorAction
import com.intellij.execution.lineMarker.RunLineMarkerContributor
import com.intellij.icons.AllIcons
import com.intellij.openapi.util.Ref
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.PsiElement
import io.github.mcppcommunity.mcppclion.McppSession
import java.io.File

private val MAIN = Regex("""^\s*(?:export\s+)?(?:int|auto)\s+main\s*\(""")

/** The mcpp project a file belongs to: the nearest directory above it with an mcpp.toml. */
internal fun projectOf(file: VirtualFile?): String? {
    var dir = file?.parent
    while (dir != null) {
        if (dir.findChild("mcpp.toml") != null) return dir.path
        dir = dir.parent
    }
    return null
}

/** The test a file is the `main` of (mcpp test --list), as far as the session knows; null otherwise. */
internal fun testOf(session: McppSession, root: String, file: VirtualFile): String? = session.testForFile(root, file.path)

class McppConfigurationProducer : LazyRunConfigurationProducer<McppRunConfiguration>() {
    override fun getConfigurationFactory(): ConfigurationFactory =
        ConfigurationTypeUtil.findConfigurationType(McppRunConfigurationType::class.java).configurationFactories[0]

    override fun setupConfigurationFromContext(c: McppRunConfiguration, context: ConfigurationContext, source: Ref<PsiElement>): Boolean {
        val file = context.location?.virtualFile ?: return false
        val root = projectOf(file) ?: return false
        val text = context.psiLocation?.containingFile?.text ?: return false
        if (!text.lineSequence().any { MAIN.containsMatchIn(it) }) return false
        c.options.root = root
        val test = testOf(McppSession.get(c.project), root, file)
        if (test != null) {
            c.options.command = "test"
            c.options.filter = test
            c.name = "mcpp test $test"
        } else {
            c.options.command = "run"
            c.name = "mcpp run ${File(root).name}"
        }
        return true
    }

    override fun isConfigurationFromContext(c: McppRunConfiguration, context: ConfigurationContext): Boolean {
        val file = context.location?.virtualFile ?: return false
        val root = projectOf(file) ?: return false
        if (c.root != root) return false
        val test = testOf(McppSession.get(c.project), root, file)
        return if (test != null) c.options.command == "test" && c.options.filter == test else c.options.command == "run"
    }
}

/** The ▶ in the gutter beside a `main` of an mcpp project's C++ file. */
class McppRunLineMarker : RunLineMarkerContributor() {
    override fun getInfo(element: PsiElement): Info? {
        if (element.firstChild != null || element.text != "main") return null
        val file = element.containingFile?.virtualFile ?: return null
        if (projectOf(file) == null) return null
        val document = element.containingFile.viewProvider.document ?: return null
        val line = document.getLineNumber(element.textOffset)
        val text = document.getText(com.intellij.openapi.util.TextRange(document.getLineStartOffset(line), document.getLineEndOffset(line)))
        if (!MAIN.containsMatchIn(text)) return null
        return Info(AllIcons.RunConfigurations.TestState.Run, ExecutorAction.getActions(0)) { "mcpp" }
    }
}
