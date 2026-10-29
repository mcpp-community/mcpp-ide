// mcpp-clion: mcpp's project features in CLion -- run configurations, the test tree, debugging, the
// console's links, and mcpp-vscode's pages in a workbench (../../webui) -- over the mcpp-ide backend (`mcpp-ide serve`).
//
// Language features are mcppls's: this plugin depends on the mcppls CLion plugin and registers no
// language service. The declarations generated from ../../spec (actions, message bundles) are written
// by the backend's `mcpp-ide-dev gen jetbrains` before the resources are processed; nothing
// generated is in the repository.
//
// Gradle and the JDK it runs on come from `xim:gradle`.
import org.jetbrains.intellij.platform.gradle.IntelliJPlatformType
import org.jetbrains.intellij.platform.gradle.TestFrameworkType
import org.jetbrains.kotlin.gradle.dsl.JvmDefaultMode

plugins {
    id("java")
    id("org.jetbrains.kotlin.jvm") version "2.4.0"
    id("org.jetbrains.intellij.platform") version "2.19.0"
}

group = "io.github.mcpp-community"
version = providers.gradleProperty("pluginVersion").get()

// The repository: this plugin is mide-plugins/clion of it.
val repositoryRoot: File = rootDir.resolve("../..").canonicalFile

repositories {
    mavenCentral()
    intellijPlatform { defaultRepositories() }
}

// The mcppls CLion plugin, from its release: the dependency the sandbox, the tests and the verifier
// load beside this plugin. Fetched once, when the build is configured, because the platform's
// dependency resolution reads it then. mcppls ships one package per platform, each carrying that
// platform's server (mcppls 0.0.19); this machine's is the one loaded here.
val mcpplsVersion = providers.gradleProperty("mcpplsPluginVersion").get()
val hostPlatform: String = run {
    val os = System.getProperty("os.name").lowercase()
    val system = when {
        os.startsWith("windows") -> "win32"
        os.startsWith("mac") -> "darwin"
        else -> "linux"
    }
    val arch = System.getProperty("os.arch").lowercase()
    "$system-${if (arch == "aarch64" || arch == "arm64") "arm64" else "x64"}"
}
val mcpplsArchive: File = layout.buildDirectory.file("mcppls/mcppls-clion-$mcpplsVersion-$hostPlatform.zip").get().asFile
if (!mcpplsArchive.exists()) {
    mcpplsArchive.parentFile.mkdirs()
    val url = "https://github.com/Sunrisepeak/mcpp-language-server/releases/download/v$mcpplsVersion/mcppls-clion-$mcpplsVersion-$hostPlatform.zip"
    val partial = File(mcpplsArchive.path + ".part")
    uri(url).toURL().openStream().use { input -> partial.outputStream().use { input.copyTo(it) } }
    partial.renameTo(mcpplsArchive)
}

dependencies {
    intellijPlatform {
        create(providers.gradleProperty("platformType").get(), providers.gradleProperty("platformVersion").get())
        bundledPlugin("com.intellij.clion")
        // CLion's debugger (CidrLocalDebugProcess, the GDB and LLDB drivers).
        bundledPlugin("com.intellij.nativeDebug")
        // The panels' browser and the test tree: a plugin and a module of their own since 2026.1.
        bundledPlugin("com.intellij.modules.jcef")
        bundledModule("intellij.platform.smRunner")
        bundledModule("intellij.platform.testRunner")
        localPlugin(mcpplsArchive.absolutePath)
        testFramework(TestFrameworkType.Platform)
    }
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.opentest4j:opentest4j:1.3.0")
}

kotlin {
    jvmToolchain(21)
    // Built against the current CLion, run on 2026.1 too: no platform default method is copied
    // into this plugin's classes as a call to `super` that 2026.1 lacks.
    compilerOptions { jvmDefault = JvmDefaultMode.NO_COMPATIBILITY }
}

// ../../spec -> the generated resources: the actions (META-INF/mcpp-actions.xml, included by plugin.xml)
// and the message bundles. `mcpp-ide-dev` is the backend package's program; MCPP_IDE_DEV names a
// built one, else `mcpp run` builds it.
val generatedResources = layout.buildDirectory.dir("generated/mcpp-ide")
val generateDeclarations = tasks.register<Exec>("generateDeclarations") {
    val out = generatedResources.get().asFile
    inputs.dir(repositoryRoot.resolve("spec"))
    outputs.dir(out)
    workingDir = repositoryRoot
    val dev = System.getenv("MCPP_IDE_DEV")
    if (dev != null) commandLine(dev, "gen", "jetbrains", "--out", out.absolutePath)
    else commandLine("mcpp", "run", "mcpp-ide-dev", "--", "gen", "jetbrains", "--out", out.absolutePath)
}
// The workbench JCEF loads: mcpp-vscode's pages over ../../webui, bundled by the extension's
// tools/bundle-webui.mjs (Node; `npm ci` in ../vscode first), with webui/files.txt naming its files.
val webuiOut = layout.buildDirectory.dir("generated/webui/webui")
val bundleWebui = tasks.register<Exec>("bundleWebui") {
    val extension = repositoryRoot.resolve("mide-plugins/vscode")
    inputs.dir(repositoryRoot.resolve("webui/src"))
    inputs.files(repositoryRoot.resolve("webui/index.html"), repositoryRoot.resolve("webui/workbench.css"))
    inputs.dir(extension.resolve("src"))
    inputs.dir(extension.resolve("media"))
    inputs.file(extension.resolve("package.json"))
    outputs.dir(webuiOut)
    workingDir = extension
    doFirst {
        if (!extension.resolve("node_modules/esbuild").isDirectory)
            throw GradleException("the workbench is bundled with the extension's dependencies: run `npm ci` in ${extension}")
    }
    commandLine("node", "tools/bundle-webui.mjs", "--out", webuiOut.get().asFile.absolutePath)
}
sourceSets {
    main {
        resources.srcDir(generatedResources)
        resources.srcDir(layout.buildDirectory.dir("generated/webui"))
    }
}
tasks.processResources { dependsOn(generateDeclarations, bundleWebui) }

// What the plugin carries of the backend (mcpp design 2026-10-28 part 2 §15), as the VS Code
// extension carries it: MCPP_IDE_OKB, the ok-image bundle (backend/mcpp-ide.okb, one for every
// system), and MCPP_IDE_OKRUN, okrun per system and architecture (backend/okrun/<os>-<arch>/).
val okb = providers.environmentVariable("MCPP_IDE_OKB")
val okrun = providers.environmentVariable("MCPP_IDE_OKRUN")
tasks.prepareSandbox {
    val plugin = intellijPlatform.projectName.get()
    if (okb.isPresent) from(okb.get()) { into("$plugin/backend"); rename { "mcpp-ide.okb" } }
    if (okrun.isPresent) from(okrun.get()) { into("$plugin/backend/okrun") }
}

intellijPlatform {
    // The settings page is the shared web panel: there is nothing for the settings search to index,
    // and the index is built by starting an IDE, which a machine without a display cannot.
    buildSearchableOptions = false
    pluginConfiguration {
        id = "io.github.mcpp-community.mcpp-clion"
        name = "mcpp"
        version = providers.gradleProperty("pluginVersion")
        // 2026.1, as the mcppls plugin it depends on: the first IDE the JetBrains Marketplace gives
        // the mcppls package of its own system.
        ideaVersion {
            sinceBuild = "261"
            untilBuild = provider { null }
        }
    }
    // `gradle verifyPlugin`: the oldest CLion the plugin claims and the one it is built on.
    pluginVerification {
        ides {
            create(IntelliJPlatformType.CLion, "2026.1")
            create(IntelliJPlatformType.CLion, providers.gradleProperty("platformVersion").get())
        }
    }
}

tasks.test {
    systemProperty("mcppide.fixtures", repositoryRoot.resolve("protocol/fixtures").absolutePath)
    providers.environmentVariable("MCPP_IDE_BACKEND").orNull?.let { systemProperty("mcppide.backend", it) }
    providers.environmentVariable("MCPP_IDE_MOCK_MCPP").orNull?.let { systemProperty("mcppide.mockMcpp", it) }
    systemProperty("patch.engine.backend.freeze.timeout", "0")
    testLogging { showStandardStreams = true; events("passed", "failed", "skipped") }
}
