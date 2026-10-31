// Run configurations from context, and the ▶ in the gutter (mcpp design 2026-10-30 part 2 §6.3,
// §7.4). What a file is -- a program's `main`, a test's, or neither -- is the backend's answer
// (source/entries); where `main` is, the language service's: the ▶ stands on the PSI element `main`
// on the line the backend read. A test program of a framework has a ▶ on each case's macro
// (test/cases), which runs that case. Every ▶ comes from the same producer, so Run and Debug are
// offered alike.
package io.github.mcppcommunity.mcppclion.run

import com.google.gson.JsonObject
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

/** The mcpp project a file belongs to: the nearest directory above it with an mcpp.toml. */
internal fun projectOf(file: VirtualFile?): String? {
    var dir = file?.parent
    while (dir != null) {
        if (dir.findChild("mcpp.toml") != null) return dir.path
        dir = dir.parent
    }
    return null
}

/** The 0-based line of an element, from its document. */
private fun lineOf(element: PsiElement): Int? {
    val document = element.containingFile?.viewProvider?.document ?: return null
    return document.getLineNumber(element.textOffset)
}

/** What the backend says the file is, as far as the session knows (asked in the background otherwise). */
internal fun entriesOf(session: McppSession, file: VirtualFile): JsonObject? =
    session.entriesFor(file.path)?.takeIf { it["kind"]?.asString in setOf("program", "test") }

/** The case written at `line` of a test's source, by its full name, with its framework. */
internal fun caseAt(session: McppSession, entries: JsonObject, line: Int): Pair<String, String>? {
    val root = entries["root"]?.asString ?: return null
    val test = entries["test"]?.asString ?: return null
    val cases = session.casesFor(root, test) ?: return null
    val framework = cases["framework"]?.takeIf { !it.isJsonNull }?.asString ?: return null
    val hit = cases.getAsJsonArray("cases")?.firstOrNull { it.asJsonObject["line"]?.asInt == line } ?: return null
    return hit.asJsonObject["name"].asString to framework
}

class McppConfigurationProducer : LazyRunConfigurationProducer<McppRunConfiguration>() {
    override fun getConfigurationFactory(): ConfigurationFactory =
        ConfigurationTypeUtil.findConfigurationType(McppRunConfigurationType::class.java).configurationFactories[0]

    override fun setupConfigurationFromContext(c: McppRunConfiguration, context: ConfigurationContext, source: Ref<PsiElement>): Boolean {
        val file = context.location?.virtualFile ?: return false
        val session = McppSession.get(c.project)
        val entries = entriesOf(session, file) ?: return false
        val root = entries["root"]?.asString ?: projectOf(file) ?: return false
        c.options.root = root
        if (entries["kind"].asString == "test") {
            val test = entries["test"].asString
            c.options.command = "test"
            c.options.filter = test
            val line = context.psiLocation?.let { lineOf(it) }
            val case = line?.let { caseAt(session, entries, it) }
            if (case != null) {
                c.options.framework = case.second
                c.options.cases = case.first
                c.name = "mcpp test $test · ${case.first}"
            } else {
                c.name = "mcpp test $test"
            }
        } else {
            c.options.command = "run"
            c.name = "mcpp run ${File(root).name}"
        }
        return true
    }

    override fun isConfigurationFromContext(c: McppRunConfiguration, context: ConfigurationContext): Boolean {
        val file = context.location?.virtualFile ?: return false
        val session = McppSession.get(c.project)
        val entries = entriesOf(session, file) ?: return false
        if (c.root != (entries["root"]?.asString ?: projectOf(file))) return false
        if (entries["kind"].asString != "test") return c.options.command == "run"
        val case = context.psiLocation?.let { lineOf(it) }?.let { caseAt(session, entries, it) }
        return c.options.command == "test" && c.options.filter == entries["test"].asString && c.options.cases == (case?.first ?: "")
    }
}

/** The ▶ in the gutter: beside a program's or a test's `main`, and beside each case of a framework. */
class McppRunLineMarker : RunLineMarkerContributor() {
    override fun getInfo(element: PsiElement): Info? {
        if (element.firstChild != null) return null
        val file = element.containingFile?.virtualFile ?: return null
        if (projectOf(file) == null) return null
        val session = McppSession.get(element.project)
        val entries = entriesOf(session, file) ?: return null
        val line = lineOf(element) ?: return null
        val text = element.text
        if (text == "main" && entries.getAsJsonObject("main")?.get("line")?.asInt == line)
            return Info(AllIcons.RunConfigurations.TestState.Run, ExecutorAction.getActions(0)) { "mcpp" }
        // A case: the macro's name on the line the backend read the case from.
        if (entries["kind"].asString == "test" && text.isNotEmpty() && text.all { it.isUpperCase() || it == '_' }) {
            val case = caseAt(session, entries, line) ?: return null
            // The last outcome of the case, as the test tree recorded it (its locationHint).
            val url = "file://${file.path}:${line + 1}"
            val icon = getTestStateIcon(url, element.project, false) ?: AllIcons.RunConfigurations.TestState.Run
            return Info(icon, ExecutorAction.getActions(0)) { "mcpp: ${case.first}" }
        }
        return null
    }
}
