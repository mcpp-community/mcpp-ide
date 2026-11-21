// CLion's own views over the backend's view models (mcpp design 2026-10-30 part 2 §9), headless: the
// project tree and the cache's rows from the answers the backend gives, a package's page, and the
// settings form from a registry -- its fields, its search, Apply's changes and Reset. They need the
// application (icons, Swing) and no project: a light project lives on temp://, where a bundled
// plugin's startup activity (CLion's STM32) fails.
package io.github.mcppcommunity.mcppclion

import com.google.gson.JsonElement
import com.google.gson.JsonParser
import com.google.gson.JsonPrimitive
import com.intellij.testFramework.ApplicationRule
import com.intellij.testFramework.EdtRule
import com.intellij.testFramework.RunsInEdt
import com.intellij.openapi.options.ConfigurationException
import com.intellij.ui.components.JBCheckBox
import com.intellij.ui.components.JBTextField
import io.github.mcppcommunity.mcppclion.settings.SettingsForm
import io.github.mcppcommunity.mcppclion.ui.CacheModel
import io.github.mcppcommunity.mcppclion.ui.LibraryPage
import io.github.mcppcommunity.mcppclion.ui.ProjectModel
import io.github.mcppcommunity.mcppclion.ui.RowNode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.assertThrows
import org.junit.ClassRule
import org.junit.Rule
import org.junit.Test

@RunsInEdt
class UiTest {
    companion object {
        @ClassRule @JvmField val application = ApplicationRule()
    }

    @get:Rule val edt = EdtRule()

    private val t: (String, Array<out Any?>) -> String = { key, args ->
        var text = key
        args.forEachIndexed { i, a -> text = text.replace("{$i}", a.toString()) }
        text
    }

    private fun json(text: String) = JsonParser.parseString(text).asJsonObject

    private fun texts(node: RowNode): List<String> = (0 until node.childCount).map { (node.getChildAt(it) as RowNode).row.text }

    @Test
    fun theProjectTreeFromTheBackendsAnswers() {
        val project = json("""{"root": "/w/app", "name": "app"}""")
        val summary = json("""{"targets": [{"name": "app", "kind": "executable", "configuration": "dev"}],
            "tests": [{"name": "test_smoke", "kind": "test"}], "libraries": [],
            "lastBuild": {"status": "ok", "command": "mcpp build"}, "artifacts": [{"path": "/w/app/target/bin/app"}],
            "platformText": "openkal-platform 0.19.0 (built-in)"}""")
        val mcpp = json("""{"state": "ok", "version": "2026.10.31.2", "program": "/x/mcpp"}""")
        val node = ProjectModel.project(project, summary, mcpp, t)
        assertEquals("app", node.row.text)
        assertEquals("/w/app/mcpp.toml", node.row.file)
        assertEquals(listOf("mcpp 2026.10.31.2", "Targets", "Tests", "Last build: ok", "Artifacts", "Platform: openkal-platform 0.19.0 (built-in)"), texts(node))
        val targets = node.getChildAt(1) as RowNode
        assertEquals("mcpp.run", (targets.getChildAt(0) as RowNode).row.command)
        val artifacts = node.getChildAt(4) as RowNode
        assertEquals("/w/app/target/bin/app", (artifacts.getChildAt(0) as RowNode).row.file)
        // A project whose mcpp is pinned and missing says so, as an error.
        val missing = ProjectModel.project(project, null, json("""{"state": "pinned-missing", "message": "mcpp 2026.9.1.1 is pinned"}"""), t)
        assertTrue((missing.getChildAt(0) as RowNode).row.error)
    }

    @Test
    fun theCacheInTheSectionsOfVsCodesPage() {
        val inventory = json("""{"root": "/home/u/.mcpp", "totalBytes": 3, "totalEntries": 2, "totalText": "1.5 GiB",
            "byKind": [{"kind": "bmi", "entries": 2, "bytes": 3, "bytesText": "1.5 GiB"}],
            "topLabels": [{"label": "std", "entries": 1, "bytes": 2, "bytesText": "1.0 GiB"}],
            "ageBuckets": [{"fromDays": 0, "toDays": 7, "entries": 2, "bytes": 3, "bytesText": "1.5 GiB"}, {"fromDays": 7, "entries": 0, "bytes": 0, "bytesText": "0 B"}],
            "incomplete": []}""")
        // The cleanups as commands/list gives them: each with its plan's scope and level.
        fun command(id: String, group: String, plan: String, scope: String, level: Int, title: String) =
            json("""{"id": "$id", "group": "$group", "title": "$title", "action": {"kind": "cache", "plan": "$plan", "scope": "$scope", "level": $level}}""")
        val commands = listOf(
            command("mcpp.clean", "project", "project", "project", 1, "Clean target"),
            json("""{"id": "mcpp.build", "group": "project", "title": "Build", "action": {"kind": "job", "job": "build"}}"""),
            command("mcpp.cleanStaleArtifacts", "cache", "stale", "project", 2, "Clean Stale Artifacts"),
            command("mcpp.cleanProjectArtifacts", "cache", "project", "project", 1, "Clean Project Artifacts"),
            command("mcpp.gcGlobalCache", "cache", "cacheGc", "shared", 2, "Collect the Shared Cache to a Budget"),
            command("mcpp.verifyGlobalCache", "cache", "cacheVerify", "shared", 0, "Verify the Shared Cache"))
        val sections = CacheModel.sections(inventory, null, commands, t)
        assertEquals(listOf("Project cache", "Global build cache"), sections.map { it.row.text })
        // The project's section: its two cleanups, the one plan of mcpp.clean and mcpp.cleanProjectArtifacts once.
        val project = sections[0].row
        assertEquals("target/", project.detail)
        assertEquals(listOf("Clean Stale Artifacts…", "Clean Project Artifacts…"), project.actions.map { it.label })
        assertEquals(listOf("stale", "project"), project.actions.map { it.plan })
        // The shared cache's: its total, its cleanups, and what the inventory says of it.
        val shared = sections[1]
        assertEquals("1.5 GiB in 2 entries", shared.row.detail)
        assertEquals(listOf("Collect the Shared Cache to a Budget…", "Verify the Shared Cache"), shared.row.actions.map { it.label })
        assertEquals(listOf("/home/u/.mcpp", "By kind", "Largest", "By last use"), texts(shared))
        val ages = shared.getChildAt(3) as RowNode
        assertEquals("0–7 days", (ages.getChildAt(0) as RowNode).row.text)
        assertEquals(1, ages.childCount)   // an empty bucket is not a row
        // An inventory mcpp could not give is the shared cache's error; the project's section stays.
        val failed = CacheModel.sections(null, "no mcpp", commands, t)
        assertEquals(2, failed.size)
        assertTrue((failed[1].getChildAt(0) as RowNode).row.error)
        assertEquals("", failed[1].row.detail)
    }

    @Test
    fun aToolbarsTextIsReadWhenItIsShown() {
        // The view is built before the session's strings arrive; the next time the toolbar is shown
        // the action reads them (#13: an English tooltip beside Chinese rows).
        val strings = mutableMapOf<String, String>()
        val action = io.github.mcppcommunity.mcppclion.ui.SessionAction({ strings["Refresh"] ?: "Refresh" }, com.intellij.icons.AllIcons.Actions.Refresh) {}
        val event = com.intellij.testFramework.TestActionEvent.createTestEvent(action)
        action.update(event)
        assertEquals("Refresh", event.presentation.text)
        strings["Refresh"] = "刷新"
        action.update(event)
        assertEquals("刷新", event.presentation.text)
    }

    @Test
    fun noClassOutsideUiJcefNamesJcef() {
        // The JCEF plugin can be absent or disabled; a class of the core that names JCEF's fails to load
        // there (#13). Only ui/jcef, which mcpp-jcef.xml registers, may.
        val sources = java.io.File("src/main/kotlin")
        assertTrue(sources.isDirectory)
        val offending = sources.walk().filter { it.isFile && it.extension == "kt" && !it.path.replace('\\', '/').contains("/ui/jcef/") }
            .filter { f -> f.readLines().any { it.startsWith("import com.intellij.ui.jcef") || it.startsWith("import org.cef") || it.contains("JBCef") } }
            .map { it.name }.toList()
        assertEquals(emptyList<String>(), offending)
        val optional = java.io.File("src/main/resources/META-INF/mcpp-jcef.xml").readText()
        assertTrue(optional.contains("ui.jcef.JcefPageHost\$Factory"))
    }

    @Test
    fun aPackagesPage() {
        val html = LibraryPage.html(json("""{"id": "mcpplibs:os", "description": "the OS <API>", "index": "mcpplibs",
            "dependencyName": "mcpplibs.os", "latest": "0.2.4", "versions": ["0.2.4", "0.2.3"], "licenses": ["Apache-2.0"],
            "repo": "https://github.com/mcpplibs/os"}""")) { it }
        assertTrue(html.contains("mcpplibs:os"))
        assertTrue(html.contains("the OS &lt;API&gt;"))
        assertTrue(html.contains("<code>mcpplibs.os</code>"))
        assertTrue(html.contains("href=\"https://github.com/mcpplibs/os\""))
    }

    @Test
    fun theSettingsFormFromARegistry() {
        val registry = json("""{"groups": [{"id": "project", "order": 10, "title": "Project"}, {"id": "build", "order": 20, "title": "Build"}],
            "settings": [
              {"key": "mcpp.path", "type": "string", "default": "", "group": "project", "tier": "public", "title": "mcpp path", "description": "the mcpp to run"},
              {"key": "mcpp.build.onSave", "type": "boolean", "default": false, "group": "build", "tier": "public", "title": "Build on save", "description": "a save builds"},
              {"key": "mcpp.build.jobs", "type": "number", "default": 0, "group": "build", "tier": "advanced", "title": "Jobs", "description": "parallel jobs"},
              {"key": "mcpp.build.profile", "type": "string", "enum": ["dev", "release"], "default": "dev", "group": "build", "tier": "public", "title": "Profile", "description": ""},
              {"key": "mcpp.old", "type": "string", "default": "", "group": "build", "tier": "public", "deprecated": true, "title": "old", "description": ""}]}""")
        val stored = mutableMapOf<String, JsonElement>("mcpp.path" to JsonPrimitive("/opt/mcpp"))
        val form = SettingsForm(registry, { it }) { stored[it] }
        assertEquals(listOf("mcpp.path", "mcpp.build.onSave", "mcpp.build.profile", "mcpp.build.jobs"), form.fields.map { it.key })
        assertFalse(form.isModified())
        assertEquals("/opt/mcpp", (form.fields[0].component as JBTextField).text)
        // A change is what Apply stores; a value back at its default is removed.
        (form.fields[1].component as JBCheckBox).isSelected = true
        (form.fields[0].component as JBTextField).text = ""
        (form.fields[3].component as JBTextField).text = "8"
        assertTrue(form.isModified())
        assertEquals(mapOf("mcpp.path" to null, "mcpp.build.onSave" to JsonPrimitive(true), "mcpp.build.jobs" to JsonPrimitive(8L)), form.changes())
        form.reset()
        assertFalse(form.isModified())
        // The search narrows the form.
        form.filter("save")
        assertEquals(listOf(false, true, false, false), form.fields.map { it.component.isVisible })
        form.filter("")
        assertTrue(form.fields.all { it.component.isVisible })
    }

    @Test
    fun invalidNumbersRefuseTheWholeApplyAndKeepLargeFiniteValues() {
        val registry = json("""{"groups":[{"id":"runtime","title":"Runtime"}],"settings":[
            {"key":"enabled","type":"boolean","default":false,"group":"runtime"},
            {"key":"timeout","type":"number","minimum":0,"maximum":3600,"default":30,"group":"runtime","title":"Timeout"},
            {"key":"budget","type":"number","minimum":0,"default":0,"group":"runtime","title":"Budget"}]}""")
        val form = SettingsForm(registry, { it }) { null }
        val timeout = form.fields.first { it.key == "timeout" }.component as JBTextField
        val enabled = form.fields.first { it.key == "enabled" }.component as JBCheckBox
        enabled.isSelected = true
        for (invalid in listOf("", "not-a-number", "NaN", "Infinity", "-1", "3601", "1e309")) {
            timeout.text = invalid
            val error = assertThrows(ConfigurationException::class.java) { form.changes() }
            assertTrue(error.message!!.contains("Timeout"))
            assertTrue(error.message!!.contains("3600"))
        }
        timeout.text = "0.5"
        assertEquals(JsonPrimitive(0.5), form.changes()["timeout"])
        assertEquals(JsonPrimitive(true), form.changes()["enabled"])
        timeout.text = "3600"
        assertEquals(JsonPrimitive(3600L), form.changes()["timeout"])
        val budget = form.fields.first { it.key == "budget" }.component as JBTextField
        budget.text = "1e20"
        assertEquals(1e20, form.changes()["budget"]!!.asDouble, 0.0)
        assertTrue(form.fields.filter { it.label != null }.all { it.label!!.labelFor === it.component })
    }
}
