import com.intellij.driver.client.Remote
import com.intellij.driver.client.service
import com.intellij.driver.client.utility
import com.intellij.driver.sdk.invokeAction
import com.intellij.driver.sdk.Project
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

private const val PLUGIN_ID = "io.github.mcpp-community.mcpp-clion"

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
    fun getContentManager(): ContentManagerRemote
}
@Remote("com.intellij.ui.content.ContentManager")
interface ContentManagerRemote { fun getContentCount(): Int }

class PackagedPluginTest {
    init {
        // A successful process exit alone cannot prove the plugin loaded:
        // exceptions reported by the IDE must fail the integration test.
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
    fun packagedPluginStartsItsBundledBackend() {
        val version = System.getProperty("mcpp.clion.ideVersion") ?: error("ideVersion is required")
        val output = Path.of(System.getProperty("mcpp.clion.output") ?: "build/stable-smoke").toAbsolutePath()
        Files.createDirectories(output)
        val fixture = output.resolve("project")
        Files.createDirectories(fixture.resolve("src"))
        Files.writeString(fixture.resolve("mcpp.toml"), "[package]\nname = \"clion-smoke\"\nversion = \"0.1.0\"\n")
        Files.writeString(fixture.resolve("src/main.cpp"), "int main() { return 0; }\n")
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
        context.runIdeWithDriver().useDriverAndCloseIde {
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
                val backendVersion = initialized.get("serverInfo").getAsJsonObject().get("version").getAsString()
                assertFalse(session.getCommands().isEmpty(), "i18n/bundle and commands/list complete before startup succeeds")
                assertFalse(session.getDegraded())
                invokeAction("ActivatemcppToolWindow")
                val window = utility<ToolWindowManagerFactory>().getInstance(singleProject()).getToolWindow("mcpp")
                assertNotNull(window, "the mcpp tool window is registered")
                val visibleDeadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(30)
                while (!window!!.isVisible() && System.nanoTime() < visibleDeadline) Thread.sleep(100)
                assertTrue(window!!.isVisible(), "the tool window opens")
                assertEquals(3, window.getContentManager().getContentCount(), "Project, Libraries and Cache views load")
                // Store only public versions and assertions. Raw IDE logs can
                // contain usernames, private paths or workspace details.
                Files.writeString(output.resolve("result.json"), """{"ideVersion":"$version","ideBuild":"$ideBuild","backendVersion":"$backendVersion","protocol":2,"startup":true,"commands":true,"toolWindowVisible":true,"toolWindowTabs":3}""" + "\n")
            }
        }
    }
}
