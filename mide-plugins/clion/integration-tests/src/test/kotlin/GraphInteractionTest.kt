import com.intellij.driver.client.Remote
import com.intellij.driver.client.service
import com.intellij.driver.client.utility
import com.intellij.driver.model.OnDispatcher
import com.intellij.driver.sdk.invokeAction
import com.intellij.driver.sdk.singleProject
import com.intellij.driver.sdk.waitForProjectOpen
import com.intellij.driver.sdk.ui.ui
import com.intellij.driver.sdk.ui.remote.Component
import com.intellij.ide.starter.ci.CIServer
import com.intellij.ide.starter.ci.NoCIServer
import com.intellij.ide.starter.di.di
import com.intellij.ide.starter.driver.engine.runIdeWithDriver
import com.intellij.ide.starter.ide.IdeDistributionFactory
import com.intellij.ide.starter.ide.IdeInstaller
import com.intellij.ide.starter.models.IdeInfo
import com.intellij.ide.starter.models.TestCase
import com.intellij.ide.starter.plugins.PluginConfigurator
import com.intellij.ide.starter.project.LocalProjectInfo
import com.intellij.ide.starter.runner.Starter
import com.intellij.platform.testFramework.teamCity.TeamCityReporter.SyntheticTestKind
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Test
import org.kodein.di.DI
import org.kodein.di.bindSingleton
import org.kodein.di.direct
import org.kodein.di.instance
import java.nio.file.Files
import java.nio.file.Path

@Remote("javax.swing.JComponent")
interface GraphComponentRemote { fun getClientProperty(key: Any): GraphBrowserRemote? }

@Remote("com.intellij.ui.jcef.JBCefBrowserBase", plugin = "io.github.mcpp-community.mcpp-clion")
interface GraphBrowserRemote {
    fun isCefBrowserCreated(): Boolean
    fun getCefBrowser(): GraphCefRemote
}

@Remote("org.cef.browser.CefBrowser", plugin = "io.github.mcpp-community.mcpp-clion")
interface GraphCefRemote {
    fun hasDocument(): Boolean
    fun getURL(): String
    fun getDevToolsClient(): GraphDevToolsRemote
}

@Remote("org.cef.browser.CefDevToolsClient", plugin = "io.github.mcpp-community.mcpp-clion")
interface GraphDevToolsRemote {
    fun executeDevToolsMethod(method: String, parameters: String): GraphStringFutureRemote
}

@Remote("java.util.concurrent.CompletableFuture")
interface GraphStringFutureRemote {
    fun isDone(): Boolean
    fun get(): String
}

@Remote("com.google.gson.JsonParser")
interface GraphJsonParserRemote { fun parseString(text: String): RemoteJsonValue }

@Remote("io.github.mcppcommunity.mcppclion.settings.McppSettings", plugin = "io.github.mcpp-community.mcpp-clion")
interface GraphSettingsRemote { fun set(key: String, value: RemoteJsonValue?) }

class GraphInteractionTest {
    init {
        di = DI {
            extend(di)
            bindSingleton<CIServer>(overrides = true) {
                object : CIServer by NoCIServer {
                    override fun reportTestFailure(testName: String, message: String, details: String, linkToLogs: String?, kind: SyntheticTestKind, generifyTestName: Boolean) {
                        throw AssertionError("IDE failure: $testName: $message")
                    }
                }
            }
        }
    }

    @Test
    fun graphRendersNavigatesAndRecoversInJcef() = runGraphScenario(false)

    @Test
    fun graphExplainsUnavailableJcef() = runGraphScenario(true)

    private fun runGraphScenario(disableJcef: Boolean) {
        val version = System.getProperty("mcpp.clion.ideVersion") ?: error("ideVersion is required")
        val output = Path.of(System.getProperty("mcpp.clion.output") ?: "build/stable-smoke").toAbsolutePath()
            .resolve(if (disableJcef) "graphs-no-jcef" else "graphs")
        Files.createDirectories(output)
        val observations = mutableListOf<String>()
        fun record(name: String) {
            observations += name
            Files.writeString(output.resolve("checks.json"), "{\"passed\":[" + observations.joinToString(",") { "\"$it\"" } + "]}\n")
        }
        val fixture = Files.createTempDirectory(output, "graph-project-")
        Files.createDirectories(fixture.resolve("src"))
        val manifest = "[package]\nname = \"clion-graph-control\"\nversion = \"0.1.0\"\n"
        Files.writeString(fixture.resolve("mcpp.toml"), manifest)
        Files.writeString(fixture.resolve("src/core.cppm"), "export module graphfixture.core;\nexport int answer() { return 42; }\n")
        Files.writeString(fixture.resolve("src/main.cpp"), "import graphfixture.core;\nint main() { return answer() == 42 ? 0 : 1; }\n")
        val localHome = System.getProperty("mcpp.clion.ideHome")?.let(Path::of)
        var ide = IdeInfo(productCode = "CL", platformPrefix = "CLion", executableFileName = "clion", fullName = "CLion", version = version, buildType = "release")
        if (localHome != null) {
            ide = ide.copy(getInstaller = {
                object : IdeInstaller {
                    override suspend fun install(ideInfo: IdeInfo) =
                        Files.readString(localHome.resolve("build.txt")).trim().removePrefix("CL-") to
                            di.direct.instance<IdeDistributionFactory>().installIDE(localHome, "clion")
                }
            })
        }
        val context = Starter.newContext("mcpp-graphs-$version-${System.currentTimeMillis()}", TestCase(ide, LocalProjectInfo(fixture)))
        PluginConfigurator(context).installPluginFromPath(Path.of(System.getProperty("mcpp.clion.mcpplsZip") ?: error("mcpplsZip is required")))
        PluginConfigurator(context).installPluginFromPath(Path.of(System.getProperty("mcpp.clion.pluginZip") ?: error("pluginZip is required")))
        context.applyVMOptionsPatch {
            withXmx(2048)
            addSystemProperty("idea.trust.all.projects", true)
            addSystemProperty("ide.show.tips.on.startup.default.value", false)
            // Keep the large generated VFS cache with this isolated test's output.
            addSystemProperty("idea.system.path", output.resolve("ide-system").toString())
            if (disableJcef) addSystemProperty("ide.browser.jcef.enabled", false)
        }
        context.runIdeWithDriver().useDriverAndCloseIde {
            waitForProjectOpen()
            withContext {
                assertEquals(version, utility<ApplicationInfoFactory>().getInstance().getFullVersion())
                System.getProperty("mcpp.clion.backendPath")?.let { backend ->
                    // Optional local backend-only control. Canonical CI leaves
                    // this unset and exercises the packaged universal backend.
                    val json = "\"" + backend.replace("\\", "\\\\").replace("\"", "\\\"") + "\""
                    withContext(OnDispatcher.EDT) {
                        service<GraphSettingsRemote>().set("mcpp.ide.backendPath", utility<GraphJsonParserRemote>().parseString(json))
                    }
                    record("configured-backend-control")
                }
                val startup = service<McppSessionRemote>(singleProject()).start()
                val startupDeadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(60)
                while (!startup.isDone() && System.nanoTime() < startupDeadline) Thread.sleep(100)
                assertTrue(startup.isDone())
                assertEquals(2, startup.get().get("protocol").getAsInt())
                record("protocol2-startup")
                if (disableJcef) {
                    // The mcpp tool window shares the left side with Project, which the IDE opens as
                    // the project's layout is restored: an action before that finishes is shown and
                    // then covered. The person asks once the project is on screen; so does the test.
                    val projectView = utility<ToolWindowManagerFactory>().getInstance(singleProject()).getToolWindow("Project")
                    val restored = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(30)
                    while (projectView != null && !projectView.isVisible() && System.nanoTime() < restored) Thread.sleep(100)
                    invokeAction("mcpp.project.showModuleDependencyGraph")
                    // The sentence wraps in the narrow tool window; its accessible name is whole.
                    val explanation = ui.x("//div[contains(@accessiblename,'This IDE runs without JCEF')]")
                    // Which window is on screen, each time that changes, for the report: the mcpp window
                    // shares the left side with Project, and only one of the two is shown at a time.
                    val windows = utility<ToolWindowManagerFactory>().getInstance(singleProject())
                    val began = System.nanoTime()
                    val timeline = StringBuilder()
                    var last = ""
                    val explained = began + java.util.concurrent.TimeUnit.SECONDS.toNanos(30)
                    while (!explanation.present() && System.nanoTime() < explained) {
                        val now = withContext(OnDispatcher.EDT) {
                            val mcpp = windows.getToolWindow("mcpp")
                            "mcpp=" + (mcpp?.isVisible() ?: "absent") + " project=" + (projectView?.isVisible() ?: "absent")
                        }
                        if (now != last) {
                            timeline.append("%.1fs %s\n".format((System.nanoTime() - began) / 1e9, now))
                            last = now
                        }
                        Thread.sleep(100)
                    }
                    if (!explanation.present()) {
                        // What the window shows instead, for the report.
                        val window = windows.getToolWindow("mcpp")
                        val state = withContext(OnDispatcher.EDT) {
                            if (window == null) "no mcpp tool window"
                            else "visible=" + window.isVisible() + " active=" + window.isActive() +
                                " type=" + runCatching { window.getType().name() }.getOrElse { "?" } +
                                " autoHide=" + window.isAutoHide() + " split=" + window.isSplitMode() +
                                " stripe=" + window.isShowStripeButton() + " selected=" +
                                window.getContentManager().getSelectedContent()?.getDisplayName()
                        }
                        Files.writeString(output.resolve("no-jcef-window.txt"), state + "\n" + timeline)
                    }
                    assertTrue(explanation.present(), "the graph explains why drawing is unavailable")
                    record("no-jcef-explanation-visible")
                } else {
                    val runs = utility<RunContentManagerFactory>().getInstance(singleProject())
                    val before = runs.getAllDescriptors().map { it.getExecutionId() }.toSet()
                    invokeAction("mcpp.build")
                    val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(180)
                    var finished: RunDescriptorRemote? = null
                    while (System.nanoTime() < deadline) {
                        finished = runs.getAllDescriptors().firstOrNull {
                            it.getDisplayName().startsWith("mcpp build ") && it.getExecutionId() !in before &&
                                it.getProcessHandler()?.isProcessTerminated() == true
                        }
                        if (finished != null) break
                        Thread.sleep(100)
                    }
                    assertNotNull(finished, "the graph fixture builds through the actual IDE action")
                    assertEquals(0, finished!!.getProcessHandler()!!.getExitCode())
                    record("module-project-built")
                    val window = utility<ToolWindowManagerFactory>().getInstance(singleProject()).getToolWindow("mcpp")!!
                    fun graph(kind: String): GraphCefRemote {
                        invokeAction(if (kind == "modules") "mcpp.project.showModuleDependencyGraph" else "mcpp.project.showDependencyGraph")
                        val expected = if (kind == "modules") "Module Graph" else "Dependency Graph"
                        val ready = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(30)
                        while (!window.isVisible() && System.nanoTime() < ready) Thread.sleep(100)
                        assertTrue(window.isVisible(), "the graph action opens the actual tool window")
                        fun selectedTitle() = withContext(OnDispatcher.EDT) { window.getContentManager().getSelectedContent()?.getDisplayName() }
                        while (selectedTitle() != expected && System.nanoTime() < ready) Thread.sleep(100)
                        assertEquals(expected, selectedTitle())
                        val panel = withContext(OnDispatcher.EDT) { window.getContentManager().getSelectedContent()!!.getComponent() }
                        val browser = withContext(OnDispatcher.EDT) {
                            cast(panel, GraphComponentRemote::class).getClientProperty("JBCefBrowser.instance")
                        }
                        assertNotNull(browser, "the actual graph tab owns a JCEF browser")
                        while (!browser!!.isCefBrowserCreated() && System.nanoTime() < ready) Thread.sleep(100)
                        assertTrue(browser!!.isCefBrowserCreated())
                        return browser.getCefBrowser()
                    }
                    fun evaluate(browser: GraphCefRemote, expression: String): Boolean {
                        val escaped = expression.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")
                        val future = browser.getDevToolsClient().executeDevToolsMethod("Runtime.evaluate", "{\"expression\":\"$escaped\",\"returnByValue\":true}")
                        val ready = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(10)
                        while (!future.isDone() && System.nanoTime() < ready) Thread.sleep(50)
                        assertTrue(future.isDone(), "the actual JCEF document answers evaluation")
                        val answer = future.get()
                        assertFalse(answer.contains("\"exceptionDetails\""), "the graph evaluation has no JavaScript exception")
                        return Regex("\\\"value\\\"\\s*:\\s*true").containsMatchIn(answer)
                    }
                    fun awaitGraph(browser: GraphCefRemote, expression: String, label: String) {
                        val ready = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(30)
                        var passed = false
                        while (System.nanoTime() < ready) {
                            if (browser.hasDocument() && evaluate(browser, expression)) { passed = true; break }
                            Thread.sleep(100)
                        }
                        assertTrue(passed, label)
                        record(label)
                    }
                    val packages = graph("packages")
                    awaitGraph(packages, "Boolean(document.querySelector('#graph svg') && document.body.textContent.includes('clion-graph-control'))", "package-svg-visible")
                    val modules = graph("modules")
                    awaitGraph(modules, "Boolean(document.querySelector('#graph svg') && [...document.querySelectorAll('g.node')].some(n => n.textContent.includes('graphfixture.core')))", "module-svg-visible")
                    val editors = utility<FileEditorManagerFactory>().getInstance(singleProject())
                    assertFalse(editors.getSelectedTextEditor()?.getDocument()?.getText()?.contains("export module graphfixture.core;") == true,
                        "the module source is not already selected before graph navigation")
                    assertTrue(evaluate(modules, "(() => { const n = [...document.querySelectorAll('g.node')].find(n => n.textContent.includes('graphfixture.core')); if (!n) return false; n.dispatchEvent(new MouseEvent('click', {bubbles: true})); return true; })()"))
                    val opened = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(10)
                    while (editors.getSelectedTextEditor()?.getDocument()?.getText()?.contains("export module graphfixture.core;") != true && System.nanoTime() < opened) Thread.sleep(100)
                    assertTrue(editors.getSelectedTextEditor()?.getDocument()?.getText()?.contains("export module graphfixture.core;") == true,
                        "clicking the rendered module opens its real source in the editor")
                    record("module-source-navigation")
                    try {
                        Files.writeString(fixture.resolve("mcpp.toml"), "[package\n")
                        val broken = graph("modules")
                        awaitGraph(broken, "Boolean(document.getElementById('note')?.textContent.includes('mcpp:') && !document.querySelector('#graph svg'))", "graph-error-visible")
                    } finally {
                        Files.writeString(fixture.resolve("mcpp.toml"), manifest)
                    }
                    val recovered = graph("modules")
                    awaitGraph(recovered, "Boolean(document.querySelector('#graph svg') && document.body.textContent.includes('graphfixture.core') && !document.getElementById('note')?.textContent.includes('mcpp:'))", "graph-recovery-visible")
                }
            }
        }
        val plugin = Path.of(System.getProperty("mcpp.clion.pluginZip"))
        val sha = java.security.MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(plugin)).joinToString("") { "%02x".format(it) }
        val backendControl = System.getProperty("mcpp.clion.backendPath") != null
        Files.writeString(output.resolve("result.json"), "{\"ideVersion\":\"$version\",\"pluginSha256\":\"$sha\",\"jcefDisabled\":$disableJcef,\"backendControl\":$backendControl,\"passed\":true}\n")
    }
}
