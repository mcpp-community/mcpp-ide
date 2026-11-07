import com.intellij.driver.client.Remote
import com.intellij.driver.client.service
import com.intellij.driver.client.utility
import com.intellij.driver.client.impl.RefWrapper
import com.intellij.driver.sdk.invokeAction
import com.intellij.driver.sdk.Project
import com.intellij.driver.sdk.FileEditorManager
import com.intellij.driver.sdk.openEditor
import com.intellij.driver.sdk.XDebuggerUtil
import com.intellij.driver.sdk.VirtualFile
import com.intellij.driver.sdk.ui.ui
import com.intellij.driver.model.OnDispatcher
import com.intellij.driver.sdk.singleProject
import com.intellij.driver.sdk.waitForProjectOpen
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
import java.nio.file.StandardOpenOption
import kotlin.time.Duration.Companion.minutes

private const val PLUGIN_ID = "io.github.mcpp-community.mcpp-clion"

private fun stockClion262DebuggerStop(
    version: String?, message: String, details: String, checks: String?, result: String?
): Boolean {
    if (version != "2026.2.4.1" || checks == null || result == null) return false
    val completed = listOf("debug-variable-40", "debug-variable-42", "debug-step-over",
        "debug-stop-prevents-program-work", "cache-inventory-visible")
    if (completed.any { "\"$it\"" !in checks } ||
        "\"ideVersion\":\"2026.2.4.1\"" !in result ||
        "\"programExecuted\":true" !in result) return false
    val reported = "$message\n$details"
    return "com.intellij.openapi.diagnostic.UnhandledException" in reported &&
        "com.intellij.execution.ExecutionFinishedException" in reported &&
        "GDBDriver" in reported && "FrameVariables" in reported &&
        "io.github.mcppcommunity." !in reported
}

private fun ideFailureFingerprint(message: String, details: String, classification: String): String {
    // IDE reports can contain machine paths. Retain only Java/Kotlin type and
    // method symbols; this is enough to distinguish an SDK error from ours.
    val text = "$message\n$details"
    val types = Regex("""\b(?:[A-Za-z_$][A-Za-z0-9_$]*\.)*[A-Z][A-Za-z0-9_$]*(?:Exception|Error)\b""")
        .findAll(text).map { it.value }.distinct().take(24).toList()
    val allFrames = Regex("""(?m)^\s*at\s+([A-Za-z0-9_.$]+)\(""")
        .findAll(details).map { it.groupValues[1] }.distinct().toList()
    val frames = (allFrames.take(8) + allFrames.filter {
        "GDBDriver" in it || "mcpp" in it.lowercase()
    }.take(12)).distinct()
    fun array(values: List<String>) = values.joinToString(",", "[", "]") { "\"$it\"" }
    return "{\"classification\":\"$classification\",\"types\":${array(types)},\"frames\":${array(frames)}}"
}

@Remote("io.github.mcppcommunity.mcppclion.McppSession", plugin = PLUGIN_ID)
interface McppSessionRemote {
    fun start(): StartupFuture
    fun getCommands(): List<RemoteJson>
    fun getDegraded(): Boolean
}


@Remote("java.util.concurrent.CompletableFuture")
interface StartupFuture {
    fun isDone(): Boolean
    fun get(): RemoteJson
}

@Remote("com.google.gson.JsonObject")
interface RemoteJson { fun get(name: String): RemoteJsonValue }

@Remote("com.google.gson.JsonElement")
interface RemoteJsonValue {
    fun getAsInt(): Int
    fun getAsString(): String
    fun getAsJsonObject(): RemoteJson
}

@Remote("com.intellij.openapi.application.ApplicationInfo")
interface ApplicationInfoFactory { fun getInstance(): ApplicationInfoRemote }

@Remote("com.intellij.openapi.application.ApplicationInfo")
interface ApplicationInfoRemote {
    fun getFullVersion(): String
    fun getBuild(): BuildNumberRemote
}

@Remote("com.intellij.openapi.util.BuildNumber")
interface BuildNumberRemote { fun asString(): String }

@Remote("com.intellij.openapi.wm.ToolWindowManager")
interface ToolWindowManagerFactory { fun getInstance(project: Project): ToolWindowManagerRemote }
@Remote("com.intellij.openapi.wm.ToolWindowManager")
interface ToolWindowManagerRemote { fun getToolWindow(id: String): ToolWindowRemote? }
@Remote("com.intellij.openapi.wm.ToolWindow")
interface ToolWindowRemote {
    fun isVisible(): Boolean
    fun isActive(): Boolean
    fun isAutoHide(): Boolean
    fun isSplitMode(): Boolean
    fun isShowStripeButton(): Boolean
    fun getType(): ToolWindowTypeRemote
    fun getContentManager(): ContentManagerRemote
}
@Remote("com.intellij.openapi.wm.ToolWindowType")
interface ToolWindowTypeRemote { fun name(): String }
@Remote("com.intellij.ui.content.ContentManager")
interface ContentManagerRemote {
    fun getContentCount(): Int
    fun getSelectedContent(): ContentRemote?
}
@Remote("com.intellij.ui.content.Content")
interface ContentRemote {
    fun getDisplayName(): String
    fun getComponent(): ContainerRemote
}
@Remote("java.awt.Container")
interface ContainerRemote {
    fun getClass(): ClassNameRemote
    fun getComponents(): Array<ContainerRemote>
}
@Remote("java.lang.Class")
interface ClassNameRemote { fun getName(): String }
@Remote("javax.swing.JTree")
interface TreeRemote {
    fun getRowCount(): Int
    fun getModel(): TreeModelRemote
}
@Remote("javax.swing.tree.TreeModel")
interface TreeModelRemote { fun getRoot(): TreeNodeRemote }
@Remote("javax.swing.tree.DefaultMutableTreeNode")
interface TreeNodeRemote {
    fun getChildAt(index: Int): TreeNodeRemote
    fun getUserObject(): ViewRowRemote
}
@Remote("io.github.mcppcommunity.mcppclion.ui.Row", plugin = PLUGIN_ID)
interface ViewRowRemote { fun getError(): Boolean }
@Remote("com.intellij.openapi.project.DumbService")
interface DumbServiceFactory { fun getInstance(project: Project): DumbServiceRemote }
@Remote("com.intellij.openapi.project.DumbService")
interface DumbServiceRemote { fun isDumb(): Boolean }

@Remote("com.intellij.openapi.actionSystem.ActionManager")
interface ActionManagerFactory { fun getInstance(): ActionManagerRemote }
@Remote("com.intellij.openapi.actionSystem.ActionManager")
interface ActionManagerRemote { fun getAction(id: String): ActionRemote? }
@Remote("com.intellij.openapi.actionSystem.AnAction")
interface ActionRemote

@Remote("com.intellij.execution.ui.RunContentManager")
interface RunContentManagerFactory { fun getInstance(project: Project): RunContentManagerRemote }
@Remote("com.intellij.execution.ui.RunContentManager")
interface RunContentManagerRemote { fun getAllDescriptors(): List<RunDescriptorRemote> }
@Remote("com.intellij.execution.ui.RunContentDescriptor")
interface RunDescriptorRemote {
    fun getDisplayName(): String
    fun getExecutionId(): Long
    fun getProcessHandler(): ProcessHandlerRemote?
    fun getExecutionConsole(): ExecutionConsoleRemote
}
@Remote("com.intellij.execution.process.ProcessHandler")
interface ProcessHandlerRemote {
    fun isProcessTerminated(): Boolean
    fun getExitCode(): Int?
    fun getClass(): ClassNameRemote
}

@Remote("com.jetbrains.cidr.cpp.toolchains.CPPToolchains", plugin = PLUGIN_ID)
interface CppToolchainsFactory { fun getInstance(): CppToolchainsRemote }
@Remote("com.jetbrains.cidr.cpp.toolchains.CPPToolchains", plugin = PLUGIN_ID)
interface CppToolchainsRemote { fun getDefaultToolchain(): CppToolchainRemote? }
@Remote("com.jetbrains.cidr.cpp.toolchains.CPPToolchains\$Toolchain", plugin = PLUGIN_ID)
interface CppToolchainRemote

@Remote("com.intellij.xdebugger.XDebuggerManager")
interface DebuggerManagerFactory { fun getInstance(project: Project): DebuggerManagerRemote }
@Remote("com.intellij.xdebugger.XDebuggerManager")
interface DebuggerManagerRemote { fun getCurrentSession(): DebugSessionRemote? }
@Remote("com.intellij.xdebugger.XDebugSession")
interface DebugSessionRemote {
    fun isSuspended(): Boolean
    fun getCurrentPosition(): DebugPositionRemote?
    fun resume()
    fun stepOver(ignoreBreakpoints: Boolean)
    fun stop()
}
@Remote("com.intellij.xdebugger.XSourcePosition")
interface DebugPositionRemote { fun getLine(): Int }
@Remote("javax.swing.JTree")
interface DebugTreeRemote { fun getModel(): DebugTreeModelRemote }
@Remote("javax.swing.tree.TreeModel")
interface DebugTreeModelRemote { fun getRoot(): DebugNodeRemote }
@Remote("com.intellij.xdebugger.impl.ui.tree.nodes.XDebuggerTreeNode")
interface DebugNodeRemote {
    fun getLoadedChildren(): List<DebugNodeRemote>
    fun getText(): ColoredTextRemote
}
@Remote("com.intellij.ui.SimpleColoredText")
interface ColoredTextRemote { fun getTexts(): List<String> }

@Remote("com.intellij.execution.ui.ExecutionConsole")
interface ExecutionConsoleRemote
@Remote("com.intellij.execution.impl.ConsoleViewImpl")
interface TextConsoleRemote { fun getText(): String }

@Remote("com.intellij.execution.testframework.sm.runner.ui.SMTRunnerConsoleView", plugin = PLUGIN_ID)
interface TestConsoleRemote { fun getResultsViewer(): TestResultsRemote }
@Remote("com.intellij.execution.testframework.sm.runner.ui.SMTestRunnerResultsForm", plugin = PLUGIN_ID)
interface TestResultsRemote { fun getTestsRootNode(): TestNodeRemote }
@Remote("com.intellij.execution.testframework.sm.runner.SMTestProxy", plugin = PLUGIN_ID)
interface TestNodeRemote {
    fun getChildren(): List<TestNodeRemote>
    fun isPassed(): Boolean
    fun isDefect(): Boolean
}

@Remote("com.intellij.openapi.fileEditor.FileEditorManager")
interface FileEditorManagerFactory { fun getInstance(project: Project): FileEditorManager }
@Remote("com.intellij.openapi.vfs.LocalFileSystem")
interface LocalFileSystemFactory { fun getInstance(): LocalFileSystemRemote }
@Remote("com.intellij.openapi.vfs.LocalFileSystem")
interface LocalFileSystemRemote { fun refreshAndFindFileByPath(path: String): VirtualFile? }
@Remote("com.intellij.codeInsight.lookup.LookupManager")
interface LookupManagerFactory { fun getInstance(project: Project): LookupManagerRemote }
@Remote("com.intellij.codeInsight.lookup.LookupManager")
interface LookupManagerRemote { fun getActiveLookup(): LookupRemote? }
@Remote("com.intellij.codeInsight.lookup.impl.LookupImpl")
interface LookupRemote {
    fun getItems(): List<LookupItemRemote>
    fun setCurrentItem(item: LookupItemRemote)
}
@Remote("com.intellij.codeInsight.lookup.LookupElement")
interface LookupItemRemote { fun getLookupString(): String }

class PackagedPluginTest {
    init {
        // A successful process exit alone cannot prove the plugin loaded.
        // Only the independently reproduced stock 262 post-stop debugger
        // cancellation is classified after every functional assertion passes.
        di = DI {
            extend(di)
            bindSingleton<CIServer>(overrides = true) {
                object : CIServer by NoCIServer {
                    override fun reportTestFailure(testName: String, message: String, details: String, linkToLogs: String?, kind: SyntheticTestKind, generifyTestName: Boolean) {
                        val output = Path.of(System.getProperty("mcpp.clion.output") ?: "build/stable-smoke").toAbsolutePath()
                        Files.createDirectories(output)
                        val known = stockClion262DebuggerStop(
                            System.getProperty("mcpp.clion.ideVersion"), message, details,
                            output.resolve("checks.json").takeIf { Files.exists(it) }?.let { Files.readString(it) },
                            output.resolve("result.json").takeIf { Files.exists(it) }?.let { Files.readString(it) })
                        Files.writeString(output.resolve("ide-errors.ndjson"),
                            ideFailureFingerprint(message, details,
                                if (known) "stock-clion-262-debugger-stop" else "unclassified") + "\n",
                            StandardOpenOption.CREATE, StandardOpenOption.APPEND)
                        if (known) return
                        throw AssertionError("IDE failure; see sanitized ide-errors.ndjson")
                    }
                }
            }
        }
    }

    @Test
    fun stockDebuggerStopClassificationIsNarrow() {
        val details = """com.intellij.openapi.diagnostic.UnhandledException
            at com.intellij.openapi.application.impl.ExceptionsKt.processUnhandledException(Exceptions.kt:56)
            Caused by: com.intellij.execution.ExecutionFinishedException
            at com.jetbrains.cidr.execution.debugger.backend.gdb.GDBDriver.doGetFrameVariables(GDBDriver.java:1)
        """.trimIndent()
        val checks = """{"passed":["debug-variable-40","debug-variable-42","debug-step-over","debug-stop-prevents-program-work","cache-inventory-visible"]}"""
        val result = """{"ideVersion":"2026.2.4.1","programExecuted":true}"""
        assertTrue(stockClion262DebuggerStop("2026.2.4.1", "", details, checks, result))
        assertFalse(stockClion262DebuggerStop("2026.1.5.2", "", details, checks, result))
        assertFalse(stockClion262DebuggerStop("2026.2.4.1", "", details.replace("ExecutionFinishedException", "IllegalStateException"), checks, result))
        assertFalse(stockClion262DebuggerStop("2026.2.4.1", "", details + " io.github.mcppcommunity.mcppclion.Backend", checks, result))
        assertFalse(stockClion262DebuggerStop("2026.2.4.1", "", details, checks.replace("debug-variable-42", "missing"), result))
        assertFalse(stockClion262DebuggerStop("2026.2.4.1", "", details, checks, null))
    }

    @Test
    fun packagedPluginStartsItsBundledBackend() {
        val version = System.getProperty("mcpp.clion.ideVersion") ?: error("ideVersion is required")
        val output = Path.of(System.getProperty("mcpp.clion.output") ?: "build/stable-smoke").toAbsolutePath()
        Files.createDirectories(output)
        Files.deleteIfExists(output.resolve("result.json"))
        Files.deleteIfExists(output.resolve("checks.json"))
        Files.deleteIfExists(output.resolve("ide-errors.ndjson"))
        val checked = linkedSetOf<String>()
        fun record(name: String) {
            checked += name
            val checks = checked.joinToString(",") { "\"$it\"" }
            Files.writeString(output.resolve("checks.json"), """{"passed":[$checks]}""" + "\n")
        }
        val fixture = Files.createTempDirectory(output, "mcpp-用例 space-")
        Files.createDirectories(fixture.resolve("src"))
        Files.writeString(fixture.resolve("mcpp.toml"), "[package]\nname = \"clion-smoke\"\nversion = \"0.1.0\"\n")
        Files.writeString(fixture.resolve("src/main.cpp"), """
            #include <cstdio>
            int main() {
                int answer = 40;
                answer += 2;
                auto* f = std::fopen("clion-run-ok.txt", "w");
                if (!f) return 1;
                std::fputs("run-ok\n", f);
                std::fclose(f);
                return answer == 42 ? 0 : 2;
            }
        """.trimIndent() + "\n")
        Files.createDirectories(fixture.resolve("tests"))
        Files.writeString(fixture.resolve("tests/check.cpp"), "int main() { return 0; }\n")
        val localHome = System.getProperty("mcpp.clion.ideHome")?.let(Path::of)
        var ide = IdeInfo(productCode = "CL", platformPrefix = "CLion", executableFileName = "clion", fullName = "CLion", version = version, buildType = "release")
        if (localHome != null) {
            // Reuse the verified full local distribution without copying an
            // additional SDK into the ordinary disk cache.
            ide = ide.copy(getInstaller = {
                object : IdeInstaller {
                    override suspend fun install(ideInfo: IdeInfo) =
                        Files.readString(localHome.resolve("build.txt")).trim().removePrefix("CL-") to
                            di.direct.instance<IdeDistributionFactory>().installIDE(localHome, "clion")
                }
            })
        }
        val context = Starter.newContext("mcpp-clion-stable-$version-${System.currentTimeMillis()}", TestCase(ide, LocalProjectInfo(fixture)))
        PluginConfigurator(context).installPluginFromPath(Path.of(System.getProperty("mcpp.clion.mcpplsZip") ?: error("mcpplsZip is required")))
        PluginConfigurator(context).installPluginFromPath(Path.of(System.getProperty("mcpp.clion.pluginZip") ?: error("pluginZip is required")))
        context.applyVMOptionsPatch {
            withXmx(2048)
            addSystemProperty("idea.trust.all.projects", true)
            addSystemProperty("ide.show.tips.on.startup.default.value", false)
        }
        context.runIdeWithDriver(runTimeout = 3.minutes).useDriverAndCloseIde {
            waitForProjectOpen()
            withContext {
                val application = utility<ApplicationInfoFactory>().getInstance()
                assertEquals(version, application.getFullVersion(), "the requested stable IDE actually runs")
                val ideBuild = application.getBuild().asString()
                val session = service<McppSessionRemote>(singleProject())
                val future = session.start()
                val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(60)
                while (!future.isDone() && System.nanoTime() < deadline) Thread.sleep(100)
                assertTrue(future.isDone(), "the complete plugin startup chain finishes")
                val initialized = future.get()
                assertEquals(2, initialized.get("protocol").getAsInt())
                record("protocol2-startup")
                val backendVersion = initialized.get("serverInfo").getAsJsonObject().get("version").getAsString()
                assertFalse(session.getCommands().isEmpty(), "i18n/bundle and commands/list complete before startup succeeds")
                val actions = utility<ActionManagerFactory>().getInstance()
                for (command in session.getCommands()) {
                    val id = command.get("id").getAsString()
                    assertNotNull(actions.getAction(id), "offered command $id is registered in this IDE")
                    val action = command.get("action").getAsJsonObject()
                    if (action.get("kind").getAsString() == "forward") {
                        val target = action.get("command").getAsString()
                        assertNotNull(actions.getAction(target), "forwarded command $id resolves to $target")
                    }
                }
                record("offered-actions-and-forward-targets")
                assertFalse(session.getDegraded())
                invokeAction("ActivatemcppToolWindow")
                val window = utility<ToolWindowManagerFactory>().getInstance(singleProject()).getToolWindow("mcpp")
                assertNotNull(window, "the mcpp tool window is registered")
                val visibleDeadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(30)
                while (!window!!.isVisible() && System.nanoTime() < visibleDeadline) Thread.sleep(100)
                assertTrue(window!!.isVisible(), "the tool window opens")
                assertEquals(3, window.getContentManager().getContentCount(), "Project, Libraries and Cache views load")
                record("visible-three-view-tool-window")
                val runs = utility<RunContentManagerFactory>().getInstance(singleProject())
                val actionResults = mutableMapOf<String, Long>()
                fun execute(command: String, label: String, success: Boolean? = true, whileRunning: (() -> Unit)? = null): RunDescriptorRemote {
                    val before = runs.getAllDescriptors().map { it.getExecutionId() }.toSet()
                    val previousHandlers = runs.getAllDescriptors().mapNotNull { it.getProcessHandler() }
                        .map { (it as RefWrapper).getRef().identityHashCode() }.toSet()
                    val startedAt = System.nanoTime()
                    invokeAction("mcpp.$command")
                    whileRunning?.invoke()
                    val deadline = startedAt + java.util.concurrent.TimeUnit.SECONDS.toNanos(if (command == "debug") 60 else 180)
                    var completed: RunDescriptorRemote? = null
                    while (System.nanoTime() < deadline) {
                        completed = runs.getAllDescriptors().firstOrNull {
                            it.getDisplayName().startsWith("mcpp $command ") &&
                                it.getExecutionId() !in before && it.getProcessHandler()?.let { handler ->
                                    (handler as RefWrapper).getRef().identityHashCode() !in previousHandlers && handler.isProcessTerminated()
                                } == true
                        }
                        if (completed != null) break
                        Thread.sleep(100)
                    }
                    assertNotNull(completed, "the real $command action finishes in the IDE")
                    val finished = requireNotNull(completed)
                    if (success != null) assertEquals(success, finished.getProcessHandler()!!.getExitCode() == 0, "$label has the expected exit status")
                    if (command == "test") {
                        val testRoot = cast(finished.getExecutionConsole(), TestConsoleRemote::class).getResultsViewer().getTestsRootNode()
                        assertFalse(testRoot.getChildren().isEmpty(), "the native test tree contains discovered tests")
                        assertEquals(success == false, testRoot.isDefect(), "the test tree records the actual outcome")
                        if (success == true) assertTrue(testRoot.getChildren().all { it.isPassed() }, "test results reach the native tree")
                        record("native-test-tree-$label")
                    }
                    actionResults[label] = java.util.concurrent.TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - startedAt)
                    record("action-$label")
                    return finished
                }
                for (command in listOf("build", "run", "test")) execute(command, command)
                assertEquals("run-ok\n", Files.readString(fixture.resolve("clion-run-ok.txt")), "the program actually ran")
                record("program-executed")
                val source = fixture.resolve("src/main.cpp")
                val sourceBefore = Files.readString(source)
                Files.writeString(source, "int main() { return missing_clion_smoke_symbol; }\n")
                val failedBuild = execute("build", "build-failure", success = false)
                val console = cast(failedBuild.getExecutionConsole(), TextConsoleRemote::class)
                val consoleDeadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(10)
                while (!console.getText().contains("missing_clion_smoke_symbol") && System.nanoTime() < consoleDeadline) Thread.sleep(100)
                assertTrue(console.getText().contains("missing_clion_smoke_symbol"), "the compiler error reaches the visible run console")
                record("compiler-error-visible")
                Files.writeString(source, sourceBefore)
                execute("build", "build-recovery")
                val testSource = fixture.resolve("tests/check.cpp")
                Files.writeString(testSource, "int main() { return 1; }\n")
                execute("test", "test-failure", success = false)
                Files.writeString(testSource, "int main() { return 0; }\n")
                execute("test", "test-recovery")
                assertNotNull(utility<CppToolchainsFactory>().getInstance().getDefaultToolchain(), "CLion has a debugger toolchain")
                val debug = execute("debug", "debug-native-launch")
                assertTrue(debug.getProcessHandler()!!.getClass().getName().contains("CidrDebugProcess"), "CLion's native debugger owns the process")
                val debugWindow = utility<ToolWindowManagerFactory>().getInstance(singleProject()).getToolWindow("Debug")
                assertTrue(debugWindow?.isVisible() == true, "the native debugger view is visible")
                record("native-debugger-process-and-view")
                val debugger = utility<DebuggerManagerFactory>().getInstance(singleProject())
                val debugFile = requireNotNull(utility<LocalFileSystemFactory>().getInstance().refreshAndFindFileByPath(source.toString()))
                openEditor(debugFile)
                withContext(OnDispatcher.EDT) { service<XDebuggerUtil>().toggleLineBreakpoint(singleProject(), debugFile, 3, false) }
                fun suspendedAt(line: Int): DebugSessionRemote {
                    val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(30)
                    while (System.nanoTime() < deadline) {
                        val current = debugger.getCurrentSession()
                        if (current?.isSuspended() == true && current.getCurrentPosition()?.getLine() == line) return current
                        Thread.sleep(100)
                    }
                    error("the native debugger did not suspend at fixture line $line")
                }
                fun visibleVariable(value: Int) {
                    ui.x("//div[@visible_text='Threads & Variables']").click()
                    val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(10)
                    var found = false
                    do {
                        found = withContext(OnDispatcher.EDT) {
                            val components = ArrayDeque<ContainerRemote>()
                            components.add(debugWindow!!.getContentManager().getSelectedContent()!!.getComponent())
                            val trees = mutableListOf<DebugTreeRemote>()
                            while (components.isNotEmpty()) {
                                val component = components.removeFirst()
                                if (component.getClass().getName().contains("DebuggerTree")) trees += cast(component, DebugTreeRemote::class)
                                components.addAll(component.getComponents())
                            }
                            val nodes = ArrayDeque<DebugNodeRemote>()
                            trees.forEach { nodes.add(it.getModel().getRoot()) }
                            var visible = false
                            while (nodes.isNotEmpty()) {
                                val node = nodes.removeFirst()
                                if (Regex("\\banswer\\b.*\\b$value\\b").containsMatchIn(node.getText().getTexts().joinToString(""))) visible = true
                                nodes.addAll(node.getLoadedChildren())
                            }
                            visible
                        }
                        if (!found) Thread.sleep(100)
                    } while (!found && System.nanoTime() < deadline)
                    assertTrue(found, "the native variables view shows answer = $value")
                    record("debug-variable-$value")
                }
                Files.delete(fixture.resolve("clion-run-ok.txt"))
                execute("debug", "debug-breakpoint-step-resume", whileRunning = {
                    val paused = suspendedAt(3)
                    record("debug-breakpoint")
                    visibleVariable(40)
                    withContext(OnDispatcher.EDT) { paused.stepOver(false) }
                    val stepped = suspendedAt(4)
                    record("debug-step-over")
                    visibleVariable(42)
                    withContext(OnDispatcher.EDT) { stepped.resume() }
                })
                assertEquals("run-ok\n", Files.readString(fixture.resolve("clion-run-ok.txt")), "resume completes the debugged program")
                record("debug-resume-program-output")
                Files.delete(fixture.resolve("clion-run-ok.txt"))
                execute("debug", "debug-stop", success = null, whileRunning = {
                    val paused = suspendedAt(3)
                    withContext(OnDispatcher.EDT) { paused.stop() }
                })
                assertFalse(Files.exists(fixture.resolve("clion-run-ok.txt")), "stop at the breakpoint prevents subsequent program work")
                record("debug-stop-prevents-program-work")
                withContext(OnDispatcher.EDT) { service<XDebuggerUtil>().toggleLineBreakpoint(singleProject(), debugFile, 3, false) }
                for ((command, title) in listOf("mcpp.library.search" to "Libraries", "mcpp.showCachePanel" to "Cache")) {
                    invokeAction(command)
                    val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(10)
                    while (window.getContentManager().getSelectedContent()?.getDisplayName() != title && System.nanoTime() < deadline) Thread.sleep(100)
                    assertEquals(title, window.getContentManager().getSelectedContent()?.getDisplayName(), "$command selects its native view")
                    record("view-$title")
                }
                for (title in listOf("Libraries", "Cache")) {
                    ui.x("//div[@visible_text='$title' and contains(@javaclass,'ContentTabLabel')]").click()
                    assertEquals(title, window.getContentManager().getSelectedContent()?.getDisplayName(), "a mouse click selects $title")
                    record("mouse-tab-$title")
                }
                val manifest = utility<LocalFileSystemFactory>().getInstance().refreshAndFindFileByPath(fixture.resolve("mcpp.toml").toString())
                openEditor(requireNotNull(manifest))
                val editor = requireNotNull(utility<FileEditorManagerFactory>().getInstance(singleProject()).getSelectedTextEditor())
                val manifestBefore = editor.getDocument().getText()
                withWriteAction { editor.getDocument().setText(manifestBefore + "\n[dep") }
                withContext(OnDispatcher.EDT) { editor.getCaretModel().moveToOffset(editor.getDocument().getText().length) }
                val indexingAtCompletion = utility<DumbServiceFactory>().getInstance(singleProject()).isDumb()
                val manifestEditor = ui.x("//div[@class='EditorComponentImpl' and @accessiblename='Editor for mcpp.toml']")
                manifestEditor.click()
                val focusDeadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(10)
                while (!manifestEditor.isFocusOwner() && System.nanoTime() < focusDeadline) Thread.sleep(100)
                assertTrue(manifestEditor.isFocusOwner(), "the manifest editor owns completion keyboard actions")
                invokeAction("CodeCompletion")
                val lookups = utility<LookupManagerFactory>().getInstance(singleProject())
                val lookupDeadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(15)
                var dependencies: LookupItemRemote? = null
                while (System.nanoTime() < lookupDeadline) {
                    dependencies = lookups.getActiveLookup()?.getItems()?.firstOrNull { it.getLookupString() == "[dependencies]" }
                    if (dependencies != null) break
                    Thread.sleep(100)
                }
                assertNotNull(dependencies, "mcpp.toml completion is visible in the actual editor")
                withContext(OnDispatcher.EDT) { lookups.getActiveLookup()!!.setCurrentItem(dependencies!!) }
                manifestEditor.keyboard { hotKey(java.awt.event.KeyEvent.VK_ENTER) }
                assertEquals(manifestBefore + "\n[dependencies]", editor.getDocument().getText(), "completion replaces exactly the requested range")
                record("manifest-completion-and-insertion")
                if (indexingAtCompletion) record("manifest-completion-during-indexing")
                withContext(OnDispatcher.EDT) {
                val pending = ArrayDeque<ContainerRemote>()
                pending.add(window.getContentManager().getSelectedContent()!!.getComponent())
                var cacheTree: TreeRemote? = null
                while (pending.isNotEmpty()) {
                    val component = pending.removeFirst()
                    if (component.getClass().getName() == "com.intellij.ui.treeStructure.Tree") {
                        cacheTree = cast(component, TreeRemote::class)
                        break
                    }
                    pending.addAll(component.getComponents())
                }
                assertNotNull(cacheTree, "the native cache tree exists")
                val cacheDeadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(15)
                while (cacheTree!!.getRowCount() == 0 && System.nanoTime() < cacheDeadline) Thread.sleep(100)
                assertTrue(cacheTree!!.getRowCount() > 0, "opening the cache view loads its inventory without manual refresh")
                assertFalse(cacheTree.getModel().getRoot().getChildAt(0).getUserObject().getError(), "the inventory query succeeds")
                record("cache-inventory-visible")
                }
                // Store only public versions and assertions. Raw IDE logs can
                // contain usernames, private paths or workspace details.
                val actionJson = actionResults.entries.joinToString(",") { "\"${it.key}\":${it.value}" }
                Files.writeString(output.resolve("result.json"), """{"ideVersion":"$version","ideBuild":"$ideBuild","backendVersion":"$backendVersion","protocol":2,"startup":true,"commands":true,"registeredCommands":${session.getCommands().size},"toolWindowVisible":true,"toolWindowTabs":3,"actionDurationMs":{$actionJson},"programExecuted":true,"indexingAtCompletion":$indexingAtCompletion}""" + "\n")
            }
        }
    }
}
