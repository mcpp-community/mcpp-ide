// The wire and the backend, without an IDE: frames reassemble whatever the chunking; a session over
// the mock mcpp (protocol 2, an LSP server) finds the project, publishes mcpp.toml's diagnostics and
// completes it, runs a build to `ok`, and lists the commands this editor offers
// (a forward to one of mcppls's actions only under the JetBrains id mcppls states). The session test runs when the build passes
// the backend and the mock mcpp (MCPP_IDE_BACKEND, MCPP_IDE_MOCK_MCPP).
package io.github.mcppcommunity.mcppclion

import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonObject
import io.github.mcppcommunity.mcppclion.backend.Backend
import io.github.mcppcommunity.mcppclion.backend.FrameReader
import io.github.mcppcommunity.mcppclion.backend.Locate
import io.github.mcppcommunity.mcppclion.backend.encode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit

class BackendTest {
    @Test
    fun framesReassemble() {
        val all = "Junk: 1\r\n\r\n".toByteArray() + encode(JsonObject().apply { addProperty("a", 1) }) +
            encode(JsonObject().apply { addProperty("b", "é") })
        val reader = FrameReader()
        val bodies = mutableListOf<String>()
        for (b in all) bodies += reader.feed(byteArrayOf(b))
        assertEquals(listOf("""{"a":1}""", """{"b":"é"}"""), bodies)
    }

    @Test
    fun theBundleRunsUnderTheOkrunCarriedForThisSystem() {
        assertEquals("windows-x86_64", Locate.okrunDir("Windows 11", "amd64"))
        assertEquals("macos-aarch64", Locate.okrunDir("Mac OS X", "aarch64"))
        val root = Files.createTempDirectory("mcppclion-okb-").toFile()
        File(root, "backend/okrun/linux-x86_64").mkdirs()
        File(root, "backend/mcpp-ide.okb").writeText("")
        File(root, "backend/okrun/linux-x86_64/okrun").writeText("")
        val linux = Locate.backend("", root, path = "", os = "Linux", arch = "amd64", home = "/nonexistent")!!
        assertEquals(listOf(File(root, "backend/okrun/linux-x86_64/okrun").path, File(root, "backend/mcpp-ide.okb").path), linux.command)
        assertEquals("bundle", linux.source)
        // No okrun for this system and none on PATH: nothing in the plugin starts.
        val arm = Locate.backend("", root, path = "", os = "Linux", arch = "aarch64", home = "/nonexistent")
        assertEquals(null, arm)
        root.deleteRecursively()
    }

    @Test
    fun aPayloadsRunnerIsMadeExecutableAgain() {
        assumeTrue("the executable bit is a POSIX file mode", !Locate.windows)
        val payload = Files.createTempDirectory("mcppclion-payload-").toFile()
        File(payload, "bin").mkdirs()
        val runner = File(payload, "bin/okrun").apply { writeText(""); setExecutable(false) }
        val image = File(payload, "bin/mcppls.okb").apply { writeText(""); setExecutable(false) }
        File(payload, "payload.json").writeText("""{"server": {"runner": "bin/okrun", "path": "bin/mcppls.okb"}}""")
        assertEquals(payload, Locate.runnablePayload(payload))
        assertTrue("the runner", runner.canExecute())
        assertTrue("the image is not a program", !image.canExecute())
        // A path that leaves the payload is not followed.
        val outside = File(payload.path + "-outside").apply { writeText(""); setExecutable(false) }
        File(payload, "payload.json").writeText("""{"server": {"runner": "../${outside.name}", "path": "bin/mcppls.okb"}}""")
        Locate.runnablePayload(payload)
        assertTrue("outside the payload", !outside.canExecute())
        outside.delete()
        payload.deleteRecursively()
    }

    @Test
    fun sessionOverTheMockMcpp() {
        val backend = System.getProperty("mcppide.backend")
        val mock = System.getProperty("mcppide.mockMcpp")
        assumeTrue("MCPP_IDE_BACKEND and MCPP_IDE_MOCK_MCPP name the programs", backend != null && mock != null)
        val root = Files.createTempDirectory("mcppclion-").toFile()
        File(root, "src").mkdirs()
        File(root, "mcpp.toml").writeText("[package]\nname = \"app\"\nversion = \"0.1.0\"\n")
        Backend(listOf(backend!!, "serve")).use { b ->
            val init = b.request("initialize", JsonObject().apply {
                add("workspaceFolders", JsonArray().apply { add(JsonObject().apply { addProperty("uri", uriOf(root.path)) }) })
                add("initializationOptions", JsonObject().apply {
                    addProperty("editor", "jetbrains")
                    add("settings", JsonObject().apply { addProperty("mcpp.path", mock) })
                })
            }).get(30, TimeUnit.SECONDS).asJsonObject
            assertEquals(1, init.getAsJsonArray("projects").size())
            assertEquals(2, init["protocol"].asInt)
            // mcpp.toml through LSP: opened, its diagnostics published (0-based, UTF-16), completion answers.
            val uri = uriOf(File(root, "mcpp.toml").path)
            val published = CompletableFuture<JsonElement>()
            b.on("textDocument/publishDiagnostics") { _, p -> published.complete(p) }
            b.notify("textDocument/didOpen", JsonObject().apply {
                add("textDocument", JsonObject().apply {
                    addProperty("uri", uri); addProperty("languageId", "mcpp-toml"); addProperty("version", 1)
                    addProperty("text", "[package]\nname = \"名字\"\ncolour = 1\n")
                })
            })
            val diagnostic = published.get(30, TimeUnit.SECONDS).asJsonObject.getAsJsonArray("diagnostics")[0].asJsonObject
            assertEquals("mcpp.manifest.unknownKey", diagnostic["code"].asString)
            assertEquals(2, diagnostic.getAsJsonObject("range").getAsJsonObject("start")["line"].asInt)
            val list = b.request("textDocument/completion", JsonObject().apply {
                add("textDocument", JsonObject().apply { addProperty("uri", uri) })
                add("position", JsonObject().apply { addProperty("line", 0); addProperty("character", 1) })
            }).get(30, TimeUnit.SECONDS).asJsonObject
            assertTrue(list.getAsJsonArray("items").any { it.asJsonObject["label"].asString == "[build]" })
            val finished = CompletableFuture<JsonElement>()
            b.on("job/finished") { _, p -> finished.complete(p) }
            b.request("job/start", JsonObject().apply { addProperty("kind", "build"); addProperty("root", root.path) }).get(30, TimeUnit.SECONDS)
            assertEquals("ok", finished.get(60, TimeUnit.SECONDS).asJsonObject["status"].asString)
            val commands = b.request("commands/list", JsonObject().apply { addProperty("editor", "jetbrains") }).get(30, TimeUnit.SECONDS)
                .asJsonObject.getAsJsonArray("commands").map { it.asJsonObject }
            assertTrue(commands.any { it["id"].asString == "mcpp.build" })
            assertTrue(commands.filter { it["action"].asJsonObject["kind"].asString == "forward" }
                .all { it["action"].asJsonObject["command"]?.asString?.startsWith("community.mcpp.mcppls.") == true })
        }
        root.deleteRecursively()
    }

    @Test
    fun snippetsAndMarkdownAsCLionShowsThem() {
        assertEquals("name = \"1.0.0\"" to 0, io.github.mcppcommunity.mcppclion.editor.expandSnippet("\${1:name} = \"\${2:1.0.0}\""))
        assertEquals("role = source;" to 7, io.github.mcppcommunity.mcppclion.editor.expandSnippet("role = \${3|source,check|};"))
        assertEquals("x()" to 2, io.github.mcppcommunity.mcppclion.editor.expandSnippet("x($1)"))
        assertEquals("<p><b>[package]</b></p><p>plane: <code>identity</code> <a href=\"u\">r</a></p>",
            io.github.mcppcommunity.mcppclion.editor.markdownToHtml("**[package]**\n\nplane: `identity` [r](u)"))
        assertEquals("file:///C:/a%20b/mcpp.toml", uriOf("C:\\a b\\mcpp.toml"))
        assertEquals("C:/a b/mcpp.toml", io.github.mcppcommunity.mcppclion.editor.pathOf("file:///C:/a%20b/mcpp.toml"))
    }

    @Test
    fun theGeneratedActionsAreTheTables() {
        val xml = javaClass.getResource("/META-INF/mcpp-actions.xml")?.readText()
        assumeTrue("the generated resources are on the test classpath", xml != null)
        assertTrue(xml!!.contains("""id="mcpp.build""""))
        // mcppls's actions are declared; each is shown where mcppls states a JetBrains id for it.
        assertTrue(xml.contains("mcpp.languageServer.restart"))
        // Build, Run, Debug and Test in the editor's context menu (part 2 §6.2).
        assertTrue(xml.contains("""<add-to-group group-id="EditorPopupMenu" anchor="last"/>"""))
        assertTrue(xml.contains("""<reference ref="mcpp.debug"/>"""))
        val bundle = javaClass.getResource("/messages/McppClionBundle.properties")!!.readText()
        assertTrue(bundle.contains("action.mcpp.build.text=Build"))
        // VS Code's chords as keymaps of their own.
        val keymap = javaClass.getResource("/keymaps/mcpp.xml")!!.readText()
        assertTrue(keymap.contains("""<action id="mcpp.build">"""))
        assertTrue(keymap.contains("""first-keystroke="ctrl alt B""""))
        assertTrue(javaClass.getResource("/keymaps/mcpp-macos.xml")!!.readText().contains("""first-keystroke="meta alt B""""))
    }
}
