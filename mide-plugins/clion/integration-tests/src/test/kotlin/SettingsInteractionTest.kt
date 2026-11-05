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
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Test
import org.kodein.di.DI
import org.kodein.di.bindSingleton
import org.kodein.di.direct
import org.kodein.di.instance
import java.nio.file.Files
import java.nio.file.Path

@Remote("io.github.mcppcommunity.mcppclion.settings.McppSettings", plugin = "io.github.mcpp-community.mcpp-clion")
interface InteractiveSettingsStore {
    fun get(key: String): RemoteJsonValue?
}

@Remote("javax.swing.JLabel")
interface SettingsLabelRemote {
    fun getLabelFor(): Component?
}

@Remote("javax.swing.text.JTextComponent")
interface SettingsTextRemote {
    fun getText(): String
    fun setText(value: String)
    fun requestFocusInWindow(): Boolean
}

@Remote("com.intellij.openapi.application.PathManager")
interface SettingsPathsRemote { fun getConfigPath(): String }

class SettingsInteractionTest {
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
    fun settingsValidateBeforeSavingInTheRealDialog() {
        val version = System.getProperty("mcpp.clion.ideVersion") ?: error("ideVersion is required")
        val output = Path.of(System.getProperty("mcpp.clion.output") ?: "build/stable-smoke").toAbsolutePath().resolve("settings")
        Files.createDirectories(output)
        val observations = mutableListOf<String>()
        fun record(name: String, value: String) {
            observations += "\"$name\":\"${value.replace("\\", "\\\\").replace("\"", "\\\"")}\""
            Files.writeString(output.resolve("observations.json"), "{${observations.joinToString(",")}}\n")
        }
        val fixture = Files.createTempDirectory(output, "settings-project-")
        Files.createDirectories(fixture.resolve("src"))
        Files.writeString(fixture.resolve("mcpp.toml"), "[package]\nname = \"settings-control\"\nversion = \"0.1.0\"\n")
        Files.writeString(fixture.resolve("src/main.cpp"), "int main() { return 0; }\n")
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
        val context = Starter.newContext("mcpp-settings-$version-${System.currentTimeMillis()}", TestCase(ide, LocalProjectInfo(fixture)))
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
                assertEquals(version, utility<ApplicationInfoFactory>().getInstance().getFullVersion())
                record("ideVersion", version)
                record("ideBuild", utility<ApplicationInfoFactory>().getInstance().getBuild().asString())
                val session = service<McppSessionRemote>(singleProject())
                val future = session.start()
                val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(60)
                while (!future.isDone() && System.nanoTime() < deadline) Thread.sleep(100)
                assertTrue(future.isDone())
                assertEquals(2, future.get().get("protocol").getAsInt())
                record("startup", "protocol2")
                val store = service<InteractiveSettingsStore>()
                val key = "mcpp.runtime.timeoutSeconds"
                invokeAction("mcpp.openSettings")
                var settingsOpen = true
                val dialog = ui.x {
                    and(or(byType(javax.swing.JDialog::class.java), byType(javax.swing.JFrame::class.java)),
                        byAccessibleName("Settings"))
                }
                try {
                    val label = ui.x("//div[@visible_text='Set the command timeout:' or @text='Set the command timeout:']")
                    val labelComponent = label.component
                    record("settingsForm", "visible")
                    val linked = cast(labelComponent, SettingsLabelRemote::class).getLabelFor()
                    record("labelLinked", (linked != null).toString())
                    assertNotNull(linked, "the visible setting label is associated with its input")
                    var field = cast(linked!!, SettingsTextRemote::class)
                    val apply = dialog.x("//div[@visible_text='Apply' or @text='Apply']")
                    fun typeValue(target: SettingsTextRemote, value: String) {
                        assertTrue(withContext(OnDispatcher.EDT) { target.requestFocusInWindow() })
                        label.keyboard {
                            // This check exercises actual text-input events, which
                            // notify the native Settings editor of modifications.
                            @Suppress("OPT_IN_USAGE")
                            hotKeyWithDefaultModifierKey(java.awt.event.KeyEvent.VK_A)
                            backspace()
                            if (value.isNotEmpty()) typeText(value)
                        }
                        assertEquals(value, target.getText(), "the real input receives the typed value")
                    }
                    fun setAndApply(value: String) {
                        typeValue(field, value)
                        val ready = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(10)
                        while (!apply.isEnabled() && System.nanoTime() < ready) Thread.sleep(100)
                        assertTrue(apply.isEnabled(), "Apply becomes available for the edited field")
                        apply.click()
                    }
                    setAndApply("12")
                    assertEquals("12", store.get(key)?.getAsString(), "a valid number reaches the settings store")
                    record("validNumber", store.get(key)!!.getAsString())
                    val outputLabel = ui.x("//div[@visible_text='Cap captured command output:' or @text='Cap captured command output:']")
                    val outputInput = cast(outputLabel.component, SettingsLabelRemote::class).getLabelFor()
                    assertNotNull(outputInput)
                    val outputField = cast(outputInput!!, SettingsTextRemote::class)
                    typeValue(outputField, "32")
                    setAndApply("-1")
                    assertEquals(null, store.get("mcpp.runtime.maxOutputMiB"), "another valid edit is not partially saved when the form is invalid")
                    assertEquals("12", store.get(key)?.getAsString())
                    record("atomicValidation", "no-partial-save")
                    typeValue(outputField, "16")
                    for (invalid in listOf("-1", "3601", "NaN", "Infinity", "not-a-number", "")) {
                        setAndApply(invalid)
                        val actual = store.get(key)?.getAsString() ?: "default"
                        record("afterInvalid-$invalid", actual)
                        assertEquals("12", actual, "invalid input $invalid must not replace the previous valid setting")
                        assertTrue(ui.x("//div[contains(@visible_text,'Invalid value') or contains(@text,'Invalid value')]").present(),
                            "invalid input is explained visibly before saving")
                    }
                    typeValue(field, "12")
                    dialog.x("//div[@visible_text='OK' or @text='OK']").click()
                    settingsOpen = false
                    invokeAction("SaveAll")
                    val settingsFile = Path.of(utility<SettingsPathsRemote>().getConfigPath()).resolve("options/mcpp-ide.xml")
                    fun persistedValue(): String? {
                        if (!Files.isRegularFile(settingsFile)) return null
                        return runCatching {
                            val document = javax.xml.parsers.DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(settingsFile.toFile())
                            val entries = document.getElementsByTagName("entry")
                            (0 until entries.length).map { entries.item(it) as org.w3c.dom.Element }
                                .firstOrNull { it.getAttribute("key") == key }?.getAttribute("value")
                        }.getOrNull()
                    }
                    val savedAt = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(15)
                    while (persistedValue() != "12" && System.nanoTime() < savedAt) Thread.sleep(100)
                    assertEquals("12", persistedValue(), "the actual persistent settings file contains the saved value")
                    record("persistedXml", "12")
                    invokeAction("mcpp.openSettings")
                    settingsOpen = true
                    val reopened = ui.x("//div[@visible_text='Set the command timeout:' or @text='Set the command timeout:']")
                    field = cast(cast(reopened.component, SettingsLabelRemote::class).getLabelFor()!!, SettingsTextRemote::class)
                    assertEquals("12", field.getText(), "reopening the native form restores the saved value")
                    record("reopenedForm", "12")
                    setAndApply("30")
                    assertEquals(null, store.get(key), "restoring the default removes the stored override")
                    record("resetDefault", "override-removed")
                } finally {
                    if (settingsOpen) dialog.x("//div[@visible_text='Cancel' or @text='Cancel']").click()
                }
            }
        }
        val plugin = Path.of(System.getProperty("mcpp.clion.pluginZip"))
        val digest = java.security.MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(plugin))
            .joinToString("") { "%02x".format(it) }
        Files.writeString(output.resolve("result.json"), """{"ideVersion":"$version","pluginSha256":"$digest","settingsValidated":true,"atomicValidation":true,"persistedXml":true,"reopenedForm":true,"defaultOverrideRemoved":true}""" + "\n")
    }
}
