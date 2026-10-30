// CLion's own views over the backend's view models (mcpp design 2026-10-30 part 2 §9), headless: the
// project tree and the cache's rows from the answers the backend gives, a package's page, and the
// settings form from a registry -- its fields, its search, Apply's changes and Reset.
package io.github.mcppcommunity.mcppclion

import com.google.gson.JsonElement
import com.google.gson.JsonParser
import com.google.gson.JsonPrimitive
import com.intellij.testFramework.fixtures.BasePlatformTestCase
import com.intellij.ui.components.JBCheckBox
import com.intellij.ui.components.JBTextField
import io.github.mcppcommunity.mcppclion.settings.SettingsForm
import io.github.mcppcommunity.mcppclion.ui.CacheModel
import io.github.mcppcommunity.mcppclion.ui.LibraryPage
import io.github.mcppcommunity.mcppclion.ui.ProjectModel
import io.github.mcppcommunity.mcppclion.ui.RowNode

class UiTest : BasePlatformTestCase() {
    private val t: (String, Array<out Any?>) -> String = { key, args ->
        var text = key
        args.forEachIndexed { i, a -> text = text.replace("{$i}", a.toString()) }
        text
    }

    private fun json(text: String) = JsonParser.parseString(text).asJsonObject

    private fun texts(node: RowNode): List<String> = (0 until node.childCount).map { (node.getChildAt(it) as RowNode).row.text }

    fun testTheProjectTreeFromTheBackendsAnswers() {
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

    fun testTheCacheRowsFromTheInventory() {
        val inventory = json("""{"root": "/home/u/.mcpp", "totalBytes": 3, "totalEntries": 2, "totalText": "1.5 GiB",
            "byKind": [{"kind": "bmi", "entries": 2, "bytes": 3, "bytesText": "1.5 GiB"}],
            "topLabels": [{"label": "std", "entries": 1, "bytes": 2, "bytesText": "1.0 GiB"}],
            "ageBuckets": [{"fromDays": 0, "toDays": 7, "entries": 2, "bytes": 3, "bytesText": "1.5 GiB"}, {"fromDays": 7, "entries": 0, "bytes": 0, "bytesText": "0 B"}],
            "incomplete": []}""")
        val rows = CacheModel.rows(inventory, t)
        assertEquals(listOf("1.5 GiB in 2 entries", "By kind", "Largest", "By last use"), rows.map { it.row.text })
        assertEquals("0–7 days", (rows[3].getChildAt(0) as RowNode).row.text)
        assertEquals(1, rows[3].childCount)   // an empty bucket is not a row
    }

    fun testAPackagesPage() {
        val html = LibraryPage.html(json("""{"id": "mcpplibs:os", "description": "the OS <API>", "index": "mcpplibs",
            "dependencyName": "mcpplibs.os", "latest": "0.2.4", "versions": ["0.2.4", "0.2.3"], "licenses": ["Apache-2.0"],
            "repo": "https://github.com/mcpplibs/os"}""")) { it }
        assertTrue(html.contains("mcpplibs:os"))
        assertTrue(html.contains("the OS &lt;API&gt;"))
        assertTrue(html.contains("<code>mcpplibs.os</code>"))
        assertTrue(html.contains("href=\"https://github.com/mcpplibs/os\""))
    }

    fun testTheSettingsFormFromARegistry() {
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
}
