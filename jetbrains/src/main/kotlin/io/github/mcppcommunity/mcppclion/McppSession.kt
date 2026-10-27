// The session with the backend for one project: started when an mcpp project opens, given the
// IDE's settings, trust, language and the project's directory, kept current as they change, and
// restarted once when it ends. Everything else in the plugin reaches the backend through it.
package io.github.mcppcommunity.mcppclion

import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonObject
import com.intellij.DynamicBundle
import com.intellij.ide.impl.isTrusted
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationInfo
import com.intellij.openapi.components.Service
import com.intellij.openapi.diagnostic.logger
import com.intellij.openapi.project.Project
import io.github.mcppcommunity.mcppclion.backend.Backend
import io.github.mcppcommunity.mcppclion.backend.Locate
import io.github.mcppcommunity.mcppclion.settings.McppSettings
import java.io.File
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CopyOnWriteArrayList

@Service(Service.Level.PROJECT)
class McppSession(private val project: Project) : Disposable {
    private val log = logger<McppSession>()
    @Volatile private var backend: Backend? = null
    @Volatile private var started: CompletableFuture<JsonObject>? = null
    @Volatile var initialized: JsonObject? = null; private set
    @Volatile private var bundle: Map<String, String> = emptyMap()
    @Volatile var commands: List<JsonObject> = emptyList(); private set
    private val listeners = CopyOnWriteArrayList<Pair<String, (JsonElement) -> Unit>>()
    private var restarts = 0

    init {
        project.messageBus.connect(this).subscribe(McppSettings.CHANGED, McppSettings.Listener { key ->
            if (key == "mcpp.ide.backendPath") restart() else pushSettings()
        })
        on("manifest/changed") { p -> p.asJsonObject["root"]?.asString?.let { refreshTests(it) } }
    }

    val root: String? get() = projects().firstOrNull()?.get("root")?.asString ?: project.basePath?.takeIf { File(it, "mcpp.toml").isFile }

    fun projects(): List<JsonObject> = initialized?.getAsJsonArray("projects")?.map { it.asJsonObject } ?: emptyList()

    /** A runtime string in the session's language. */
    fun t(key: String, vararg args: Any?): String {
        var text = bundle[key] ?: key
        args.forEachIndexed { i, a -> text = text.replace("{$i}", a.toString()) }
        return text
    }

    val strings: Map<String, String> get() = bundle

    fun on(method: String, listener: (JsonElement) -> Unit): () -> Unit {
        val entry = method to listener
        listeners += entry
        return { listeners -= entry }
    }

    /** The plugin's directory: the parent of the lib/ holding this class's jar. */
    private fun pluginRoot(): File? = pluginDirectory()

    /**
     * The servers to ask first. The mcppls CLion plugin starts mcppls from PATH or from the payload its
     * `--install` puts in the user data directory, which the backend looks at itself; MCPPLS_SERVER
     * names another.
     */
    private fun mcpplsCandidates(): JsonArray = JsonArray().apply { System.getenv("MCPPLS_SERVER")?.let { add(it) } }

    fun start(): CompletableFuture<JsonObject> {
        started?.let { if (backend?.running == true || !it.isDone) return it }
        val future = CompletableFuture<JsonObject>()
        started = future
        val configured = if (project.isTrusted()) McppSettings.get().string("mcpp.ide.backendPath") else ""
        val located = Locate.backend(configured, pluginRoot())
        if (located == null) {
            notify(t("The mcpp-ide backend was not found. Install it with `xlings install mcpp-ide`, or set mcpp.ide.backendPath."), NotificationType.ERROR)
            future.completeExceptionally(IllegalStateException("mcpp-ide was not found"))
            return future
        }
        log.info("mcpp-ide: starting ${located.program} (${located.source})")
        val b = Backend(listOf(located.program.path, "serve")) { line -> log.info("[backend] $line") }
        backend = b
        b.on("*") { method, params -> listeners.filter { it.first == method || it.first == "*" }.forEach { (m, fn) ->
            runCatching { fn(if (m == "*") JsonObject().apply { addProperty("method", method); add("params", params) } else params) }
        } }
        b.exited.thenAccept { code -> if (backend === b) onExit(code) }
        val params = JsonObject().apply {
            add("client", JsonObject().apply { addProperty("name", ApplicationInfo.getInstance().versionName); addProperty("version", ApplicationInfo.getInstance().fullVersion) })
            addProperty("editor", "jetbrains")
            addProperty("locale", DynamicBundle.getLocale().toLanguageTag())
            addProperty("trusted", project.isTrusted())
            add("settings", McppSettings.get().values())
            add("workspaceFolders", JsonArray().apply { project.basePath?.let { add(it) } })
            add("languageService", JsonObject().apply { add("candidates", mcpplsCandidates()) })
        }
        b.request("initialize", params).thenCompose { init ->
            initialized = init.asJsonObject
            b.request("i18n/bundle")
        }.thenCompose { strings ->
            bundle = strings.asJsonObject.entrySet().associate { it.key to it.value.asString }
            b.request("commands/list", JsonObject().apply { addProperty("editor", "jetbrains") })
        }.whenComplete { table, error ->
            if (error != null) { future.completeExceptionally(error); return@whenComplete }
            commands = table.asJsonObject.getAsJsonArray("commands").map { it.asJsonObject }
            future.complete(initialized!!)
        }
        return future
    }

    private fun onExit(code: Int) {
        log.info("mcpp-ide: the backend ended ($code)")
        backend = null
        if (restarts++ < 1) { start(); return }
        notify(t("The mcpp-ide backend ended."), NotificationType.ERROR)
    }

    fun restart() {
        val old = backend
        backend = null
        started = null
        restarts = 0
        old?.close()
        start()
    }

    fun request(method: String, params: JsonElement = JsonObject()): CompletableFuture<JsonElement> =
        start().thenCompose { backend?.request(method, params) ?: CompletableFuture.failedFuture(IllegalStateException(t("the mcpp-ide backend is not running"))) }

    // The tests of each project by the file holding their `main` (mcpp test --list), for the gutter
    // and the run configurations from context, which must not wait for mcpp.
    private val testMains = java.util.concurrent.ConcurrentHashMap<String, Map<String, String>>()

    /** The test whose `main` is `path`, from what is known; asks again in the background when nothing is. */
    fun testForFile(root: String, path: String): String? {
        val known = testMains[root]
        if (known == null) refreshTests(root)
        return known?.get(File(path).canonicalPath)
    }

    fun refreshTests(root: String) {
        request("test/list", JsonObject().apply { addProperty("root", root) }).thenAccept { r ->
            testMains[root] = r.asJsonObject.getAsJsonArray("tests").map { it.asJsonObject }
                .filter { it["main"]?.asString?.isNotEmpty() == true }
                .associate { File(it["main"].asString).canonicalPath to it["test"].asString }
        }
    }

    fun pushSettings() {
        val b = backend ?: return
        b.request("settings/update", JsonObject().apply {
            add("settings", McppSettings.get().values())
            addProperty("trusted", project.isTrusted())
            addProperty("locale", DynamicBundle.getLocale().toLanguageTag())
        })
    }

    fun notify(message: String, type: NotificationType = NotificationType.INFORMATION) {
        NotificationGroupManager.getInstance().getNotificationGroup("mcpp").createNotification("mcpp", message, type).notify(project)
    }

    override fun dispose() {
        val b = backend
        backend = null
        b?.close()
    }

    companion object {
        /** <plugin>/lib/<jar> -> <plugin>, from where this class was loaded. */
        fun pluginDirectory(): File? = runCatching {
            File(McppSession::class.java.protectionDomain.codeSource.location.toURI()).parentFile?.parentFile
        }.getOrNull()

        const val PLUGIN_ID = "io.github.mcpp-community.mcpp-clion"
        const val MCPPLS_PLUGIN_ID = "io.github.sunrisepeak.mcppls"
        fun get(project: Project): McppSession = project.getService(McppSession::class.java)
    }
}
