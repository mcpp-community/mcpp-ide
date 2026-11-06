import com.intellij.driver.client.Remote
import com.intellij.driver.client.utility
import com.intellij.driver.model.OnDispatcher
import com.intellij.driver.sdk.Project
import com.intellij.driver.sdk.XDebuggerUtil
import com.intellij.driver.client.service
import com.intellij.driver.sdk.invokeAction
import com.intellij.driver.sdk.openEditor
import com.intellij.driver.sdk.singleProject
import com.intellij.driver.sdk.ui.ui
import com.intellij.driver.sdk.waitForProjectOpen
import com.intellij.ide.starter.ci.CIServer
import com.intellij.ide.starter.ci.NoCIServer
import com.intellij.ide.starter.di.di
import com.intellij.ide.starter.driver.engine.runIdeWithDriver
import com.intellij.ide.starter.ide.IdeDistributionFactory
import com.intellij.ide.starter.ide.IdeInstaller
import com.intellij.ide.starter.models.IdeInfo
import com.intellij.ide.starter.models.TestCase
import com.intellij.ide.starter.project.LocalProjectInfo
import com.intellij.ide.starter.runner.Starter
import com.intellij.platform.testFramework.teamCity.TeamCityReporter.SyntheticTestKind
import org.junit.jupiter.api.Assertions.*
import org.junit.jupiter.api.Test
import org.kodein.di.DI
import org.kodein.di.bindSingleton
import org.kodein.di.direct
import org.kodein.di.instance
import java.nio.file.Files
import java.nio.file.Path
import kotlin.time.Duration.Companion.minutes

@Remote("com.intellij.execution.RunManager")
interface NativeRunManagerFactory { fun getInstance(project: Project): NativeRunManager }
@Remote("com.intellij.execution.RunManager")
interface NativeRunManager {
    fun getAllSettings(): List<NativeRunSettings>
    fun setSelectedConfiguration(settings: NativeRunSettings)
}
@Remote("com.intellij.execution.RunnerAndConfigurationSettings")
interface NativeRunSettings { fun getName(): String }
@Remote("com.intellij.openapi.extensions.PluginId")
interface NativePluginIds { fun getId(id: String): NativePluginId }
@Remote("com.intellij.openapi.extensions.PluginId")
interface NativePluginId
@Remote("com.intellij.ide.plugins.PluginManagerCore")
interface NativePlugins { fun isPluginInstalled(id: NativePluginId): Boolean }

class NativeDebuggerControlTest {
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
    fun nativeCmakeDebuggerReadsVariablesAndStops() {
        val version = System.getProperty("mcpp.clion.ideVersion") ?: error("ideVersion is required")
        assertEquals("2026.2.4.1", version)
        val output = Path.of(System.getProperty("mcpp.clion.output") ?: error("output is required")).resolve("native-debug")
        Files.createDirectories(output)
        val checks = mutableListOf<String>()
        fun record(value: String) {
            checks += value
            Files.writeString(output.resolve("checks.json"), "{\"passed\":[" + checks.joinToString(",") { "\"$it\"" } + "]}\n")
        }
        val fixture = Files.createTempDirectory(output, "native-cmake-")
        Files.createDirectories(fixture.resolve("src"))
        val compiler = Path.of(System.getenv("NATIVE_CONTROL_CXX") ?: error("native compiler is required"))
        assertTrue(Files.isExecutable(compiler))
        val compilerVersion = ProcessBuilder(compiler.toString(), "-dumpfullversion").start()
        val versionText=compilerVersion.inputStream.bufferedReader().readText().trim()
        assertTrue(Regex("[0-9]+(\\.[0-9]+){1,3}").matches(versionText))
        assertEquals(0, compilerVersion.waitFor())
        Files.writeString(output.resolve("toolchain.json"),"{\"compilerVersion\":\"$versionText\"}\n")
        record("stock-compiler-control")
        Files.writeString(fixture.resolve("CMakeLists.txt"), """
            cmake_minimum_required(VERSION 3.15)
            set(CMAKE_CXX_COMPILER "${compiler.toString().replace('\\', '/')}")
            project(native_debug_control LANGUAGES CXX)
            set(CMAKE_CXX_STANDARD 17)
            add_executable(native_debug_control src/main.cpp)
        """.trimIndent() + "\n")
        val done = fixture.resolve("native-done.txt")
        val source = fixture.resolve("src/main.cpp")
        Files.writeString(source, """
            #include <cstdio>
            int main() {
                int answer = 40;
                answer += 2;
                auto* f = std::fopen("${done.toString().replace("\\", "\\\\")}", "w");
                if (!f) return 1;
                std::fputs("done", f);
                std::fclose(f);
                return answer == 42 ? 0 : 2;
            }
        """.trimIndent() + "\n")
        var ide = IdeInfo(productCode="CL", platformPrefix="CLion", executableFileName="clion", fullName="CLion", version=version, buildType="release")
        val localHome=System.getProperty("mcpp.clion.ideHome")?.let(Path::of)
        if (localHome!=null) {
            ide=ide.copy(getInstaller={
                object : IdeInstaller {
                    override suspend fun install(ideInfo: IdeInfo)=
                        Files.readString(localHome.resolve("build.txt")).trim().removePrefix("CL-") to
                            di.direct.instance<IdeDistributionFactory>().installIDE(localHome,"clion")
                }
            })
        }
        val context = Starter.newContext("native-debug-control-$version-${System.currentTimeMillis()}", TestCase(ide, LocalProjectInfo(fixture)))
        // No mcpp/mcppls installation; only the usual driver instrumentation.
        context.applyVMOptionsPatch {
            withXmx(2048)
            addSystemProperty("idea.trust.all.projects", true)
            addSystemProperty("ide.show.tips.on.startup.default.value", false)
            addSystemProperty("idea.system.path", output.resolve("ide-system").toString())
        }
        context.runIdeWithDriver(runTimeout=5.minutes).useDriverAndCloseIde {
            waitForProjectOpen()
            withContext {
                assertEquals(version, utility<ApplicationInfoFactory>().getInstance().getFullVersion())
                val ids=utility<NativePluginIds>()
                val plugins=utility<NativePlugins>()
                assertFalse(plugins.isPluginInstalled(ids.getId("io.github.mcpp-community.mcpp-clion")))
                assertFalse(plugins.isPluginInstalled(ids.getId("io.github.sunrisepeak.mcppls")))
                record("mcpp-and-mcppls-absent")
                ui.x("//div[@visible_text='Skip Wizard and Use Defaults' or @text='Skip Wizard and Use Defaults']").click()
                record("stock-toolchain-wizard-defaults")
                val runs=utility<NativeRunManagerFactory>().getInstance(singleProject())
                val ready=System.nanoTime()+java.util.concurrent.TimeUnit.SECONDS.toNanos(180)
                var configuration: NativeRunSettings?=null
                while (System.nanoTime()<ready) {
                    configuration=runs.getAllSettings().firstOrNull { it.getName()=="native_debug_control" }
                    if (configuration!=null) break
                    Thread.sleep(100)
                }
                assertNotNull(configuration,"stock CMake target is configured")
                withContext(OnDispatcher.EDT) { runs.setSelectedConfiguration(configuration!!) }
                record("stock-cmake-configuration")
                val file=requireNotNull(utility<LocalFileSystemFactory>().getInstance().refreshAndFindFileByPath(source.toString()))
                openEditor(file)
                withContext(OnDispatcher.EDT) { service<XDebuggerUtil>().toggleLineBreakpoint(singleProject(),file,3,false) }
                val debugger=utility<DebuggerManagerFactory>().getInstance(singleProject())
                val runContent=utility<RunContentManagerFactory>().getInstance(singleProject())
                fun existingRuns()=runContent.getAllDescriptors().map { it.getExecutionId() }.toSet()
                fun finishedRun(previous: Set<Long>) {
                    val deadline=System.nanoTime()+java.util.concurrent.TimeUnit.SECONDS.toNanos(30)
                    while (System.nanoTime()<deadline) {
                        if (runContent.getAllDescriptors().any { descriptor ->
                            descriptor.getExecutionId() !in previous && descriptor.getProcessHandler()?.let {
                                it.getClass().getName().contains("CidrDebugProcess") && it.isProcessTerminated()
                            }==true
                        }) return
                        Thread.sleep(50)
                    }
                    error("native debug process did not finish")
                }
                fun suspendedAt(line: Int): DebugSessionRemote {
                    val deadline=System.nanoTime()+java.util.concurrent.TimeUnit.SECONDS.toNanos(60)
                    while (System.nanoTime()<deadline) {
                        val session=debugger.getCurrentSession()
                        if (session?.isSuspended()==true && session.getCurrentPosition()?.getLine()==line) return session
                        Thread.sleep(50)
                    }
                    error("native control did not reach the fixed source line")
                }
                fun variable(value: Int) {
                    ui.x("//div[@visible_text='Threads & Variables']").click()
                    val window=utility<ToolWindowManagerFactory>().getInstance(singleProject()).getToolWindow("Debug")!!
                    val deadline=System.nanoTime()+java.util.concurrent.TimeUnit.SECONDS.toNanos(10)
                    var found=false
                    do {
                        found=withContext(OnDispatcher.EDT) {
                            val components=ArrayDeque<ContainerRemote>()
                            components.add(window.getContentManager().getSelectedContent()!!.getComponent())
                            val trees=mutableListOf<DebugTreeRemote>()
                            while (components.isNotEmpty()) {
                                val component=components.removeFirst()
                                if (component.getClass().getName().contains("DebuggerTree")) trees+=cast(component,DebugTreeRemote::class)
                                components.addAll(component.getComponents())
                            }
                            val nodes=ArrayDeque<DebugNodeRemote>()
                            trees.forEach { nodes.add(it.getModel().getRoot()) }
                            var visible=false
                            while (nodes.isNotEmpty()) {
                                val node=nodes.removeFirst()
                                if (Regex("\\banswer\\b.*\\b$value\\b").containsMatchIn(node.getText().getTexts().joinToString(""))) visible=true
                                nodes.addAll(node.getLoadedChildren())
                            }
                            visible
                        }
                        if (!found) Thread.sleep(50)
                    } while (!found && System.nanoTime()<deadline)
                    assertTrue(found,"native variables show the controlled value")
                    record("native-variable-$value")
                }
                val beforeResume=existingRuns()
                invokeAction("Debug")
                val paused=suspendedAt(3);record("native-breakpoint")
                variable(40)
                withContext(OnDispatcher.EDT) { paused.stepOver(false) }
                val stepped=suspendedAt(4);record("native-step")
                variable(42)
                withContext(OnDispatcher.EDT) { stepped.resume() }
                finishedRun(beforeResume)
                assertEquals("done",Files.readString(done));record("native-resume-output")
                Files.delete(done)
                val beforeStop=existingRuns()
                invokeAction("Debug")
                val stopped=suspendedAt(3)
                withContext(OnDispatcher.EDT) { stopped.stop() }
                finishedRun(beforeStop)
                assertFalse(Files.exists(done),"native stop prevents program work")
                record("native-stop-prevents-work")
                Files.writeString(output.resolve("result.json"),"{\"ideVersion\":\"$version\",\"mcppInstalled\":false,\"mcpplsInstalled\":false,\"nativeCmake\":true,\"functionalBodyPassed\":true}\n")
            }
        }
    }
}
