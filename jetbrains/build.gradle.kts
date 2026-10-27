// mcpp-clion: mcpp's project features in CLion -- run configurations, the test tree, debugging, the
// console's links, and the shared panels (../webui) -- over the mcpp-ide backend (`mcpp-ide serve`).
//
// Language features are mcppls's: this plugin depends on the mcppls CLion plugin and registers no
// language service. The declarations generated from ../spec (actions, message bundles) are written
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

repositories {
    mavenCentral()
    intellijPlatform { defaultRepositories() }
}

// The mcppls CLion plugin, from its release: the dependency the sandbox, the tests and the verifier
// load beside this plugin. Fetched once, when the build is configured, because the platform's
// dependency resolution reads it then.
val mcpplsVersion = providers.gradleProperty("mcpplsPluginVersion").get()
val mcpplsArchive: File = layout.buildDirectory.file("mcppls/mcppls-clion-$mcpplsVersion.zip").get().asFile
if (!mcpplsArchive.exists()) {
    mcpplsArchive.parentFile.mkdirs()
    val url = "https://github.com/Sunrisepeak/mcpp-language-server/releases/download/v$mcpplsVersion/mcppls-clion-$mcpplsVersion.zip"
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
    // Built against the current CLion, run on 2025.2 too: no platform default method is copied
    // into this plugin's classes as a call to `super` that 2025.2 lacks.
    compilerOptions { jvmDefault = JvmDefaultMode.NO_COMPATIBILITY }
}

// ../spec -> the generated resources: the actions (META-INF/mcpp-actions.xml, included by plugin.xml)
// and the message bundles. `mcpp-ide-dev` is the backend package's program; MCPP_IDE_DEV names a
// built one, else `mcpp run` builds it.
val generatedResources = layout.buildDirectory.dir("generated/mcpp-ide")
val generateDeclarations = tasks.register<Exec>("generateDeclarations") {
    val out = generatedResources.get().asFile
    inputs.dir(rootDir.resolve("../spec"))
    outputs.dir(out)
    workingDir = rootDir.resolve("..")
    val dev = System.getenv("MCPP_IDE_DEV")
    if (dev != null) commandLine(dev, "gen", "jetbrains", "--out", out.absolutePath)
    else commandLine("mcpp", "run", "mcpp-ide-dev", "--", "gen", "jetbrains", "--out", out.absolutePath)
}
// ../webui, as it is: the panels JCEF loads.
val copyWebui = tasks.register<Sync>("copyWebui") {
    from(rootDir.resolve("../webui")) { exclude("tests/**", "tsconfig.json", "js/types.d.ts") }
    into(layout.buildDirectory.dir("generated/webui/webui"))
}
sourceSets {
    main {
        resources.srcDir(generatedResources)
        resources.srcDir(layout.buildDirectory.dir("generated/webui"))
    }
}
tasks.processResources { dependsOn(generateDeclarations, copyWebui) }

// The backend programs the plugin carries, one per system (`bin/<platform>/mcpp-ide[.exe]`), from
// MCPP_IDE_BACKENDS (a directory laid out that way). Without it the plugin finds the backend on PATH
// or through xlings.
val backends = providers.environmentVariable("MCPP_IDE_BACKENDS")
tasks.prepareSandbox {
    if (backends.isPresent) from(backends.get()) { into("${intellijPlatform.projectName.get()}/bin") }
}

intellijPlatform {
    // The settings page is the shared web panel: there is nothing for the settings search to index,
    // and the index is built by starting an IDE, which a machine without a display cannot.
    buildSearchableOptions = false
    pluginConfiguration {
        id = "io.github.mcpp-community.mcpp-clion"
        name = "mcpp"
        version = providers.gradleProperty("pluginVersion")
        ideaVersion {
            sinceBuild = "252"
            untilBuild = provider { null }
        }
    }
    // `gradle verifyPlugin`: the oldest CLion the plugin claims and the one it is built on.
    pluginVerification {
        ides {
            create(IntelliJPlatformType.CLion, "2025.2")
            create(IntelliJPlatformType.CLion, providers.gradleProperty("platformVersion").get())
        }
    }
}

tasks.test {
    systemProperty("mcppide.fixtures", rootDir.resolve("../protocol/fixtures").absolutePath)
    providers.environmentVariable("MCPP_IDE_BACKEND").orNull?.let { systemProperty("mcppide.backend", it) }
    providers.environmentVariable("MCPP_IDE_MOCK_MCPP").orNull?.let { systemProperty("mcppide.mockMcpp", it) }
    systemProperty("patch.engine.backend.freeze.timeout", "0")
    testLogging { showStandardStreams = true; events("passed", "failed", "skipped") }
}
