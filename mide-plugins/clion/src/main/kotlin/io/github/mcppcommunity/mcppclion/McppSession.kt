// The session with the backend for one project: started when an mcpp project opens, given the
// IDE's settings, trust, language and the project's directory, kept current as they change, and
// started again after 1, 4 and 16 seconds when it ends; a fourth end within three minutes leaves the
// plugin in its degraded mode (design 2026-10-30 part 2 §3.1). Everything else in the plugin
// reaches the backend through it.
package io.github.mcppcommunity.mcppclion

import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonObject
import com.intellij.DynamicBundle
import com.intellij.ide.trustedProjects.TrustedProjects
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationInfo
import com.intellij.openapi.application.PathManager
import com.intellij.openapi.components.Service
import com.intellij.openapi.diagnostic.logger
import com.intellij.openapi.project.Project
import io.github.mcppcommunity.mcppclion.backend.Backend
import io.github.mcppcommunity.mcppclion.backend.Locate
import io.github.mcppcommunity.mcppclion.settings.McppSettings
import java.io.File
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CopyOnWriteArrayList

/** A path as the `file:` URI the backend names a document by (`file:///C:/x` for a drive path). */
fun uriOf(path: String): String {
    val p = path.replace('\\', '/')
    val out = StringBuilder("file://")
    if (!p.startsWith("/")) out.append('/')
    for (b in p.toByteArray(Charsets.UTF_8)) {
        val c = b.toInt() and 0xff
        val ch = c.toChar()
        if (ch.isLetterOrDigit() && c < 0x80 || ch in "/-_.~:") out.append(ch) else out.append("%%%02X".format(c))
    }
    return out.toString()
}

@Service(Service.Level.PROJECT)
class McppSession(private val project: Project) : Disposable {
    private val log = logger<McppSession>()
    @Volatile private var backend: Backend? = null
    @Volatile private var started: CompletableFuture<JsonObject>? = null
    @Volatile var initialized: JsonObject? = null; private set
    @Volatile private var bundle: Map<String, String> = emptyMap()
    @Volatile var commands: List<JsonObject> = emptyList(); private set
    private val listeners = CopyOnWriteArrayList<Pair<String, (JsonElement) -> Unit>>()
    /** When the backend ended, within the last three minutes. */
    private val exits = ArrayDeque<Long>()
    @Volatile var degraded = false; private set

    init {
        project.messageBus.connect(this).subscribe(McppSettings.CHANGED, McppSettings.Listener { key ->
            if (key == "mcpp.ide.backendPath") restart() else pushSettings()
        })
        on("manifest/changed") { forgetSources() }
        // A build ended: a test program can list the cases its macros generate (§7.3).
        on("job/finished") { p ->
            val o = p.asJsonObject
            if (o["status"]?.asString == "ok" && o["kind"]?.asString in setOf("build", "test")) {
                cases.keys.toList().forEach { key -> key.split('\u0000').takeIf { it.size == 2 }?.let { refreshCases(it[0], it[1], list = true) } }
            }
        }
        // A C++ source saved: what was read from it (main, a framework's cases) is read again.
        project.messageBus.connect(this).subscribe(com.intellij.openapi.vfs.VirtualFileManager.VFS_CHANGES,
            object : com.intellij.openapi.vfs.newvfs.BulkFileListener {
                override fun after(events: MutableList<out com.intellij.openapi.vfs.newvfs.events.VFileEvent>) {
                    for (e in events) {
                        val path = e.path
                        if (path.endsWith(".cpp") || path.endsWith(".cc") || path.endsWith(".cxx") || path.endsWith(".cppm")) forgetSource(path)
                    }
                }
            })
        // mcppls answered differently (another version, other actions): the commands are asked again,
        // since mcppls's actions are offered under the ids it states (§5.3).
        on("languageService/changed") { refreshCommands() }
        // The IDE regained focus: a `xlings use` or a changed pin happened outside it (§4.2).
        com.intellij.openapi.application.ApplicationManager.getApplication().messageBus.connect(this).subscribe(
            com.intellij.openapi.application.ApplicationActivationListener.TOPIC,
            object : com.intellij.openapi.application.ApplicationActivationListener {
                override fun applicationActivated(ideFrame: com.intellij.openapi.wm.IdeFrame) {
                    if (backend?.running != true) return
                    backend?.request("project/refresh", JsonObject())
                    languageServiceStatus()
                }
            })
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

    private val startedListeners = CopyOnWriteArrayList<() -> Unit>()

    /** Called each time a backend has started and answered `initialize`. */
    fun whenStarted(listener: () -> Unit) {
        startedListeners += listener
    }

    fun on(method: String, listener: (JsonElement) -> Unit): () -> Unit {
        val entry = method to listener
        listeners += entry
        return { listeners -= entry }
    }

    /**
     * Whether C++ Modules serves build.mcpp here: the mcppls CLion plugin is installed and enabled,
     * and the backend leaves it what it answers better (the unknownSymbol rule, the module names
     * after `import`).
     */
    fun mcpplsServes(): Boolean = runCatching {
        val id = com.intellij.openapi.extensions.PluginId.getId(MCPPLS_PLUGIN_ID)
        com.intellij.ide.plugins.PluginManagerCore.getPlugin(id) != null && !com.intellij.ide.plugins.PluginManagerCore.isDisabled(id)
    }.getOrDefault(false)

    /** The plugin's directory: the parent of the lib/ holding this class's jar. */
    private fun pluginRoot(): File? = pluginDirectory()

    /**
     * The servers to ask first: MCPPLS_SERVER, then the payload the mcppls CLion plugin carries
     * (`<plugins>/mcppls-clion/payload`, whose payload.json's `server` the backend starts: the runner
     * and the okb, the runner made executable again; looking a plugin up by its id is the platform's
     * internal API). PATH and xlings are the backend's own to look at.
     */
    private fun mcpplsCandidates(): JsonArray = JsonArray().apply {
        System.getenv("MCPPLS_SERVER")?.let { add(it) }
        runCatching { PathManager.getPluginsDir().resolve("mcppls-clion/payload").toFile() }.getOrNull()
            ?.takeIf { it.resolve("payload.json").isFile }?.let { add(Locate.runnablePayload(it).path) }
    }

    fun start(): CompletableFuture<JsonObject> {
        started?.let { if (backend?.running == true || !it.isDone) return it }
        val future = CompletableFuture<JsonObject>()
        started = future
        val configured = if (TrustedProjects.isProjectTrusted(project)) McppSettings.get().string("mcpp.ide.backendPath") else ""
        val located = Locate.backend(configured, pluginRoot())
        if (located == null) {
            notify(t("The mcpp-ide backend was not found. Install it with `xlings install mcpp-ide`, or set mcpp.ide.backendPath."), NotificationType.ERROR)
            future.completeExceptionally(IllegalStateException("mcpp-ide was not found"))
            return future
        }
        log.info("mcpp-ide: starting ${located.command.joinToString(" ")} (${located.source})")
        val b = Backend(located.command + "serve") { line -> log.info("[backend] $line") }
        backend = b
        b.on("*") { method, params -> listeners.filter { it.first == method || it.first == "*" }.forEach { (m, fn) ->
            runCatching { fn(if (m == "*") JsonObject().apply { addProperty("method", method); add("params", params) } else params) }
        } }
        b.exited.thenAccept { code -> if (backend === b) onExit(code) }
        // LSP's `initialize`: the client and the folder in LSP's fields, mcpp-ide's own in initializationOptions.
        val params = JsonObject().apply {
            addProperty("processId", ProcessHandle.current().pid())
            add("clientInfo", JsonObject().apply { addProperty("name", ApplicationInfo.getInstance().versionName); addProperty("version", ApplicationInfo.getInstance().fullVersion) })
            add("workspaceFolders", JsonArray().apply {
                project.basePath?.let { base -> add(JsonObject().apply { addProperty("uri", uriOf(base)); addProperty("name", project.name) }) }
            })
            add("capabilities", JsonObject())
            add("initializationOptions", JsonObject().apply {
                addProperty("editor", "jetbrains")
                addProperty("locale", DynamicBundle.getLocale().toLanguageTag())
                addProperty("trusted", TrustedProjects.isProjectTrusted(project))
                add("settings", McppSettings.get().values())
                add("languageService", JsonObject().apply { add("candidates", mcpplsCandidates()) })
                addProperty("mcpplsServes", mcpplsServes())
            })
        }
        b.request("initialize", params).thenCompose { init ->
            initialized = init.asJsonObject
            b.notify("initialized", JsonObject())
            b.request("i18n/bundle")
        }.thenCompose { strings ->
            bundle = strings.asJsonObject.entrySet().associate { it.key to it.value.asString }
            b.request("commands/list", JsonObject().apply { addProperty("editor", "jetbrains") })
        }.whenComplete { table, error ->
            if (error != null) { future.completeExceptionally(error); return@whenComplete }
            commands = table.asJsonObject.getAsJsonArray("commands").map { it.asJsonObject }
            future.complete(initialized!!)
            startedListeners.forEach { runCatching { it() } }
        }
        return future
    }

    private fun onExit(code: Int) {
        log.info("mcpp-ide: the backend ended ($code)")
        backend = null
        val now = System.currentTimeMillis()
        synchronized(exits) {
            exits.addLast(now)
            while (exits.isNotEmpty() && now - exits.first() >= 3 * 60_000) exits.removeFirst()
        }
        val delays = longArrayOf(1000, 4000, 16000)
        val count = synchronized(exits) { exits.size }
        if (count <= delays.size) {
            log.info("mcpp-ide: starting it again in ${delays[count - 1] / 1000} s")
            com.intellij.util.concurrency.AppExecutorUtil.getAppScheduledExecutorService()
                .schedule({ started = null; start() }, delays[count - 1], java.util.concurrent.TimeUnit.MILLISECONDS)
            return
        }
        degraded = true
        val n = NotificationGroupManager.getInstance().getNotificationGroup("mcpp").createNotification("mcpp",
            t("The mcpp-ide backend ended four times in three minutes; build, run, test and the project pages go on without it."),
            NotificationType.ERROR)
        n.addAction(com.intellij.notification.NotificationAction.createSimpleExpiring(t("Restart the backend")) { restart() })
        n.addAction(com.intellij.notification.NotificationAction.createSimpleExpiring(t("Export IDE Report")) {
            io.github.mcppcommunity.mcppclion.actions.Dispatch.exportReport(project)
        })
        n.notify(project)
    }

    /** What mcppls says about the project (§5.2); the backend tells `languageService/changed` when it differs. */
    fun languageServiceStatus(): CompletableFuture<JsonElement> =
        request("languageService/status", JsonObject().apply {
            add("candidates", mcpplsCandidates())
            root?.let { addProperty("root", it) }
        })

    private fun refreshCommands() {
        backend?.request("commands/list", JsonObject().apply { addProperty("editor", "jetbrains") })
            ?.thenAccept { table -> commands = table.asJsonObject.getAsJsonArray("commands").map { it.asJsonObject } }
    }

    fun restart() {
        val old = backend
        backend = null
        started = null
        synchronized(exits) { exits.clear() }
        degraded = false
        old?.close()
        start()
    }

    fun request(method: String, params: JsonElement = JsonObject()): CompletableFuture<JsonElement> {
        if (degraded && backend?.running != true)
            return CompletableFuture.failedFuture(IllegalStateException(t("the mcpp-ide backend is not running")))
        return start().thenCompose { backend?.request(method, params) ?: CompletableFuture.failedFuture(IllegalStateException(t("the mcpp-ide backend is not running"))) }
    }

    // What each C++ file is to its project (source/entries) and each test program's cases
    // (test/cases), for the gutter, which must not wait for the backend: asked in the background
    // when unknown, and the editor's markers recomputed when the answer arrives.
    private val sources = java.util.concurrent.ConcurrentHashMap<String, JsonObject>()
    private val cases = java.util.concurrent.ConcurrentHashMap<String, JsonObject>()

    fun entriesFor(path: String): JsonObject? {
        val known = sources[path]
        if (known == null) refreshEntries(path)
        return known
    }

    fun refreshEntries(path: String) {
        request("source/entries", JsonObject().apply { addProperty("file", path) }).thenAccept { r ->
            sources[path] = r.asJsonObject
            restartMarkers()
        }
    }

    fun casesFor(root: String, test: String): JsonObject? {
        val known = cases["$root\u0000$test"]
        if (known == null) refreshCases(root, test, list = false)
        return known
    }

    fun refreshCases(root: String, test: String, list: Boolean) {
        request("test/cases", JsonObject().apply {
            addProperty("root", root)
            addProperty("test", test)
            addProperty("list", list && TrustedProjects.isProjectTrusted(project))
        }).thenAccept { r ->
            cases["$root\u0000$test"] = r.asJsonObject
            restartMarkers()
        }
    }

    /** A source changed: what was read from it is read again on its next use. */
    fun forgetSource(path: String) {
        sources.remove(path)
        cases.keys.removeIf { it.split('\u0000').getOrNull(1)?.let { test -> path.replace('\\', '/').endsWith("/tests/$test.cpp") } == true }
    }

    private fun forgetSources() {
        sources.clear()
        cases.clear()
    }

    private fun restartMarkers() = com.intellij.openapi.application.ApplicationManager.getApplication().invokeLater {
        if (!project.isDisposed) com.intellij.codeInsight.daemon.DaemonCodeAnalyzer.getInstance(project).restart()
    }

    fun pushSettings() {
        val b = backend ?: return
        b.request("settings/update", JsonObject().apply {
            add("settings", McppSettings.get().values())
            addProperty("trusted", TrustedProjects.isProjectTrusted(project))
            addProperty("locale", DynamicBundle.getLocale().toLanguageTag())
            addProperty("mcpplsServes", mcpplsServes())
        })
    }

    /** A notification to the backend (LSP's document notifications), when it is running. */
    fun notifyBackend(method: String, params: JsonObject) {
        backend?.notify(method, params)
    }

    /** Whether the backend is running now (nothing is started to answer this). */
    val running: Boolean get() = backend?.running == true

    /** What the pages write to their output channel: the IDE's log. */
    fun log(text: String) {
        text.trimEnd().lines().forEach { log.info("[mcpp] $it") }
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
