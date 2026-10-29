// The workbench (../../webui): mcpp-vscode's pages -- the project tree, the libraries and a package's
// page, the cache, the settings, the graphs -- in a JCEF browser, as the same modules VS Code runs.
// What the pages ask of VS Code is answered here, in the IDE's own terms: the backend's methods, the
// settings store, dialogs and popups, editors, the clipboard, the browser, files, mcpp's commands.
//
// Asynchronous requests arrive through `window.mcppHostSend` (a JBCefJSQuery) and are answered
// through `window.mcppHostReceive`; a synchronous one (a page's synchronous file read) is a
// `prompt("mcpp-host:...")` answered by the JS dialog handler, the one synchronous channel a page has.
package io.github.mcppcommunity.mcppclion.webui

import com.google.gson.Gson
import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonNull
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.google.gson.JsonPrimitive
import com.intellij.ide.BrowserUtil
import com.intellij.ide.trustedProjects.TrustedProjects
import com.intellij.notification.NotificationAction
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.PathManager
import com.intellij.openapi.editor.colors.EditorColorsManager
import com.intellij.openapi.fileChooser.FileChooserFactory
import com.intellij.openapi.fileChooser.FileSaverDescriptor
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileEditor.FileDocumentManagerListener
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.fileEditor.FileEditorManagerEvent
import com.intellij.openapi.fileEditor.FileEditorManagerListener
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.ide.CopyPasteManager
import com.intellij.openapi.progress.ProgressIndicator
import com.intellij.openapi.progress.ProgressManager
import com.intellij.openapi.progress.Task
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.Messages
import com.intellij.openapi.ui.popup.JBPopupFactory
import com.intellij.openapi.ui.popup.PopupStep
import com.intellij.openapi.ui.popup.util.BaseListPopupStep
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.vfs.VirtualFileManager
import com.intellij.openapi.vfs.newvfs.BulkFileListener
import com.intellij.openapi.vfs.newvfs.events.VFileCreateEvent
import com.intellij.openapi.vfs.newvfs.events.VFileDeleteEvent
import com.intellij.openapi.vfs.newvfs.events.VFileEvent
import com.intellij.testFramework.LightVirtualFile
import com.intellij.ui.JBColor
import com.intellij.ui.jcef.JBCefApp
import com.intellij.ui.jcef.JBCefBrowser
import com.intellij.ui.jcef.JBCefBrowserBase
import com.intellij.ui.jcef.JBCefJSQuery
import com.intellij.util.ui.JBUI
import com.intellij.util.ui.UIUtil
import io.github.mcppcommunity.mcppclion.McppSession
import io.github.mcppcommunity.mcppclion.actions.Dispatch
import io.github.mcppcommunity.mcppclion.settings.McppSettings
import org.cef.browser.CefBrowser
import org.cef.browser.CefFrame
import org.cef.callback.CefJSDialogCallback
import org.cef.handler.CefJSDialogHandler
import org.cef.handler.CefJSDialogHandlerAdapter
import org.cef.handler.CefLoadHandlerAdapter
import org.cef.misc.BoolRef
import java.awt.Color
import java.awt.datatransfer.StringSelection
import java.io.File
import java.nio.file.FileSystems
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.nio.file.attribute.BasicFileAttributes
import java.util.Base64
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger
import javax.swing.JComponent
import javax.swing.JLabel
import javax.swing.SwingConstants

/**
 * One workbench. [mode] `window` shows the views as tabs (the "mcpp" tool window); `settings` opens
 * the settings panel alone (Settings | Tools | mcpp).
 */
class Workbench(private val project: Project, private val mode: String, parent: Disposable) : Disposable {
    private val session = McppSession.get(project)
    private val browser: JBCefBrowser? = if (JBCefApp.isSupported()) JBCefBrowser() else null
    private val query: JBCefJSQuery? = browser?.let { JBCefJSQuery.create(it as JBCefBrowserBase) }
    private val unsubscribe = mutableListOf<() -> Unit>()
    private var loaded = false
    private val watched = Regex("/(mcpp\\.toml|mcpp\\.lock|[^/]+\\.(cpp|cc|cxx|cppm|ixx|mpp|ccm))$")
    private val waiting = mutableListOf<JsonObject>()

    val component: JComponent = browser?.component
        ?: JLabel(session.t("This IDE runs without JCEF; the mcpp panels need it."), SwingConstants.CENTER)

    init {
        Disposer.register(parent, this)
        unsubscribe += session.on("*") { m -> notify(m.asJsonObject["method"].asString, m.asJsonObject["params"] ?: JsonNull.INSTANCE) }
        if (browser != null && query != null) {
            query.addHandler { text -> receive(text); null }
            browser.jbCefClient.addLoadHandler(object : CefLoadHandlerAdapter() {
                override fun onLoadEnd(b: CefBrowser, frame: CefFrame, status: Int) {
                    if (!frame.isMain) return
                    b.executeJavaScript("window.mcppHostSend = function (text) { ${query.inject("text")} }; window.mcppHostAttached && window.mcppHostAttached();", b.url, 0)
                    ApplicationManager.getApplication().invokeLater { loaded = true; waiting.forEach(::post); waiting.clear() }
                }
            }, browser.cefBrowser)
            browser.jbCefClient.addJSDialogHandler(object : CefJSDialogHandlerAdapter() {
                override fun onJSDialog(b: CefBrowser, origin: String, type: CefJSDialogHandler.JSDialogType, message: String, defaultPrompt: String,
                                        callback: CefJSDialogCallback, suppress: BoolRef): Boolean {
                    if (type != CefJSDialogHandler.JSDialogType.JSDIALOGTYPE_PROMPT || !message.startsWith("mcpp-host:")) return false
                    callback.Continue(true, sync(message.removePrefix("mcpp-host:")).toString())
                    return true
                }
            }, browser.cefBrowser)
            val bus = project.messageBus.connect(this)
            bus.subscribe(FileEditorManagerListener.FILE_EDITOR_MANAGER, object : FileEditorManagerListener {
                override fun selectionChanged(event: FileEditorManagerEvent) {
                    notify("activeEditor", JsonObject().apply { event.newFile?.takeIf { it.isInLocalFileSystem }?.let { addProperty("path", it.path) } })
                }
            })
            bus.subscribe(FileDocumentManagerListener.TOPIC, object : FileDocumentManagerListener {
                override fun beforeDocumentSaving(document: com.intellij.openapi.editor.Document) {
                    FileDocumentManager.getInstance().getFile(document)?.let { f -> notify("documentSaved", JsonObject().apply { addProperty("path", f.path) }) }
                }
            })
            bus.subscribe(VirtualFileManager.VFS_CHANGES, object : BulkFileListener {
                override fun after(events: List<VFileEvent>) {
                    for (e in events) {
                        // What the pages watch: the manifests and the module sources (the graphs).
                        val path = e.path
                        if (!watched.containsMatchIn(path)) continue
                        val kind = when (e) { is VFileCreateEvent -> "created"; is VFileDeleteEvent -> "deleted"; else -> "changed" }
                        notify("fileChanged", JsonObject().apply { addProperty("path", path); addProperty("kind", kind) })
                    }
                }
            })
            ApplicationManager.getApplication().messageBus.connect(this).subscribe(McppSettings.CHANGED, McppSettings.Listener { key ->
                notify("settingsChanged", JsonObject().apply { add("keys", JsonArray().apply { add(key) }); add("levels", levels()) })
            })
            browser.loadURL(page().toURI().toString())
        }
    }

    /** The workbench's files, written once per build of the plugin where the browser can load them. */
    private fun page(): File {
        val jar = runCatching { File(Workbench::class.java.protectionDomain.codeSource.location.toURI()) }.getOrNull()
        val version = jar?.takeIf { it.isFile }?.let { "${it.nameWithoutExtension}-${it.lastModified()}" } ?: "dev"
        val dir = File(PathManager.getSystemPath(), "mcpp-clion/workbench-$version")
        val index = javaClass.getResource("/webui/files.txt")?.readText() ?: error("the workbench is not in the plugin (webui/files.txt)")
        for (f in index.lines().filter { it.isNotBlank() }) {
            val target = File(dir, f)
            if (target.isFile && version != "dev") continue
            target.parentFile.mkdirs()
            javaClass.getResourceAsStream("/webui/$f")?.use { input -> target.outputStream().use { input.copyTo(it) } }
        }
        return File(dir, "index.html")
    }

    /** Asks the workbench to bring a view or a panel on screen (McppPanels.show). */
    fun show(view: String, args: JsonElement? = null) {
        notify("show", JsonObject().apply { addProperty("view", view); args?.let { add("args", it) } })
    }

    private fun notify(method: String, params: JsonElement) {
        post(JsonObject().apply { addProperty("type", "notify"); addProperty("method", method); add("params", params) })
    }

    private fun post(message: JsonObject) {
        val b = browser ?: return
        ApplicationManager.getApplication().invokeLater {
            if (!loaded) { waiting += message; return@invokeLater }
            b.cefBrowser.executeJavaScript("window.mcppHostReceive && window.mcppHostReceive($message);", b.cefBrowser.url, 0)
        }
    }

    private fun answer(id: JsonElement, result: CompletableFuture<JsonElement>) {
        result.whenComplete { value, error ->
            post(JsonObject().apply {
                add("id", id)
                if (error == null) { addProperty("type", "result"); add("result", value ?: JsonNull.INSTANCE) }
                else { addProperty("type", "error"); add("error", JsonObject().apply { addProperty("message", (error.cause ?: error).message ?: "failed") }) }
            })
        }
    }

    private fun receive(text: String) {
        val m = runCatching { JsonParser.parseString(text).asJsonObject }.getOrNull() ?: return
        if (m["type"]?.asString != "request") return
        val action = m["action"]?.asString ?: return
        val params = m["params"]?.takeIf { it.isJsonObject }?.asJsonObject ?: JsonObject()
        answer(m["id"], handle(action, params))
    }

    // ── synchronous requests: files ──────────────────────────────────────────────────────────

    private fun sync(text: String): JsonObject = runCatching {
        val m = JsonParser.parseString(text).asJsonObject
        val params = m["params"]?.takeIf { it.isJsonObject }?.asJsonObject ?: JsonObject()
        JsonObject().apply { addProperty("ok", true); add("result", files(m["action"].asString, params)) }
    }.getOrElse { e -> JsonObject().apply { addProperty("ok", false); addProperty("error", e.message ?: "failed") } }

    private fun stat(path: Path, follow: Boolean): JsonElement {
        val options = if (follow) arrayOf() else arrayOf(LinkOption.NOFOLLOW_LINKS)
        val a = runCatching { Files.readAttributes(path, BasicFileAttributes::class.java, *options) }.getOrNull() ?: return JsonNull.INSTANCE
        return JsonObject().apply {
            addProperty("type", when { a.isSymbolicLink -> "symlink"; a.isDirectory -> "directory"; a.isRegularFile -> "file"; else -> "other" })
            addProperty("size", a.size())
            addProperty("mtimeMs", a.lastModifiedTime().toMillis())
            addProperty("ino", (a.fileKey()?.hashCode() ?: path.hashCode()).toLong() and 0xffffffffL)
        }
    }

    private fun files(action: String, p: JsonObject): JsonElement {
        val path = Path.of(p["path"].asString)
        return when (action) {
            "fs.stat" -> stat(path, p["follow"]?.asBoolean ?: true)
            "fs.read" -> if (Files.isRegularFile(path)) JsonPrimitive(Files.readString(path)) else JsonNull.INSTANCE
            "fs.list" -> if (!Files.isDirectory(path)) JsonNull.INSTANCE else JsonArray().also { out ->
                Files.list(path).use { stream ->
                    stream.forEach { child ->
                        val s = stat(child, false)
                        if (s.isJsonObject) out.add(s.asJsonObject.apply { addProperty("name", child.fileName.toString()) })
                    }
                }
            }
            "fs.realpath" -> if (Files.exists(path)) JsonPrimitive(path.toRealPath().toString()) else JsonNull.INSTANCE
            "fs.write" -> {
                val text = p["text"].asString
                Files.createDirectories(path.parent)
                if (p["base64"]?.asBoolean == true) Files.write(path, Base64.getDecoder().decode(text)) else Files.writeString(path, text)
                LocalFileSystem.getInstance().refreshAndFindFileByPath(path.toString())
                JsonPrimitive(true)
            }
            else -> error("unknown file action $action")
        }
    }

    // ── asynchronous requests ────────────────────────────────────────────────────────────────

    private fun done(value: JsonElement = JsonNull.INSTANCE) = CompletableFuture.completedFuture(value)

    private fun onEdt(block: (CompletableFuture<JsonElement>) -> Unit): CompletableFuture<JsonElement> {
        val f = CompletableFuture<JsonElement>()
        ApplicationManager.getApplication().invokeLater { runCatching { block(f) }.onFailure { f.completeExceptionally(it) } }
        return f
    }

    private fun levels(): JsonObject = JsonObject().apply {
        add("user", McppSettings.get().values())
        add("workspace", JsonObject())
    }

    private fun handle(action: String, p: JsonObject): CompletableFuture<JsonElement> = when (action) {
        "init" -> done(JsonObject().apply {
            add("info", info())
            add("theme", Theme.variables())
            addProperty("themeKind", if (JBColor.isBright()) "vscode-light" else "vscode-dark")
            addProperty("mode", mode)
        })
        "rpc" -> session.request(p["method"].asString, p["params"] ?: JsonObject())
        "settings.read" -> done(levels())
        "settings.write" -> {
            McppSettings.get().set(p["key"].asString, p["value"]?.takeIf { !it.isJsonNull })
            done(levels())
        }
        "message" -> message(p)
        "input" -> onEdt { f ->
            val value = Messages.showInputDialog(project, p["prompt"]?.asString ?: "", p["title"]?.asString ?: "mcpp", null, p["value"]?.asString ?: "", null)
            f.complete(value?.let { JsonPrimitive(it) } ?: JsonNull.INSTANCE)
        }
        "pick" -> onEdt { f -> pick(p, f) }
        "saveDialog" -> onEdt { f ->
            val filters = p["filters"]?.takeIf { it.isJsonObject }?.asJsonObject?.entrySet().orEmpty()
            val start = p["path"]?.asString?.let { File(it) }
            val descriptor = FileSaverDescriptor(p["title"]?.asString ?: "mcpp", "")
            // VS Code's filters (label -> extensions) as the chooser's one filter: the first label, every extension.
            if (filters.isNotEmpty())
                descriptor.withExtensionFilter(filters.first().key, *filters.flatMap { e -> e.value.asJsonArray.map { it.asString } }.toTypedArray())
            val dialog = FileChooserFactory.getInstance().createSaveFileDialog(descriptor, project)
            val chosen = dialog.save(start?.parentFile?.toPath(), start?.name)
            f.complete(chosen?.file?.path?.let { JsonPrimitive(it) } ?: JsonNull.INSTANCE)
        }
        "progress.start" -> progressStart(p["title"]?.asString ?: "mcpp")
        "progress.report" -> { progress[p["id"].asInt]?.text2 = p["message"]?.asString; done() }
        "progress.end" -> { progressEnd[p["id"].asInt]?.complete(Unit); done() }
        "openFile" -> onEdt { f ->
            val file = LocalFileSystem.getInstance().refreshAndFindFileByPath(p["path"].asString)
            if (file != null) OpenFileDescriptor(project, file, maxOf(0, (p["line"]?.takeIf { !it.isJsonNull }?.asInt ?: 1) - 1), 0).navigate(true)
            f.complete(JsonPrimitive(file != null))
        }
        "openText" -> onEdt { f ->
            val file = LightVirtualFile(p["name"].asString, p["text"].asString).apply { isWritable = false }
            FileEditorManager.getInstance(project).openFile(file, true)
            f.complete(JsonPrimitive(true))
        }
        "clipboard" -> { CopyPasteManager.getInstance().setContents(StringSelection(p["text"].asString)); done() }
        "openExternal" -> { BrowserUtil.browse(p["url"].asString); done(JsonPrimitive(true)) }
        "log" -> { session.log(p["text"].asString); done() }
        "log.show" -> done()
        "command" -> onEdt { f ->
            val id = p["id"].asString
            if (session.commands.any { it["id"].asString == id }) {
                val args = p["args"]?.takeIf { it.isJsonArray }?.asJsonArray
                val first = args?.firstOrNull()
                Dispatch.run(project, id, if (first != null && first.isJsonObject) first.asJsonObject.deepCopy() else JsonObject())
            }
            f.complete(JsonNull.INSTANCE)
        }
        "watch" -> done()
        "findFiles" -> CompletableFuture.supplyAsync { findFiles(p) }
        "fs.read", "fs.list", "fs.stat", "fs.realpath", "fs.write" -> CompletableFuture.supplyAsync { files(action, p) }
        else -> CompletableFuture.failedFuture(IllegalArgumentException("unknown workbench action $action"))
    }

    private fun info(): JsonObject = JsonObject().apply {
        val os = System.getProperty("os.name").lowercase()
        addProperty("platform", when { os.startsWith("windows") -> "win32"; os.startsWith("mac") -> "darwin"; else -> "linux" })
        addProperty("arch", if (System.getProperty("os.arch").lowercase().let { it == "aarch64" || it == "arm64" }) "arm64" else "x64")
        addProperty("language", com.intellij.DynamicBundle.getLocale().toLanguageTag().lowercase().let { if (it.startsWith("zh")) "zh-cn" else it })
        addProperty("home", System.getProperty("user.home"))
        addProperty("tmp", System.getProperty("java.io.tmpdir"))
        add("env", Gson().toJsonTree(System.getenv()))
        addProperty("extensionUri", page().parentFile.toURI().toString())
        add("workspaceFolders", JsonArray().apply { project.basePath?.let { add(it) } })
        addProperty("trusted", TrustedProjects.isProjectTrusted(project))
    }

    private fun message(p: JsonObject): CompletableFuture<JsonElement> = onEdt { f ->
        val items = p["items"]?.asJsonArray?.map { it.asString } ?: emptyList()
        val text = listOfNotNull(p["message"]?.asString, p["detail"]?.takeIf { !it.isJsonNull }?.asString).joinToString("\n\n")
        val modal = p["modal"]?.asBoolean == true
        if (modal || items.isNotEmpty() && p["level"]?.asString == "warning") {
            val icon = when (p["level"]?.asString) { "error" -> Messages.getErrorIcon(); "warning" -> Messages.getWarningIcon(); else -> Messages.getInformationIcon() }
            val buttons = (items + session.t("Cancel")).toTypedArray()
            val chosen = Messages.showDialog(project, text, "mcpp", buttons, items.size, icon)
            f.complete(if (chosen in items.indices) JsonPrimitive(chosen) else JsonNull.INSTANCE)
            return@onEdt
        }
        val type = when (p["level"]?.asString) { "error" -> NotificationType.ERROR; "warning" -> NotificationType.WARNING; else -> NotificationType.INFORMATION }
        val n = NotificationGroupManager.getInstance().getNotificationGroup("mcpp").createNotification(text, type)
        items.forEachIndexed { i, title ->
            n.addAction(NotificationAction.createSimpleExpiring(title) { f.complete(JsonPrimitive(i)) })
        }
        n.whenExpired { f.complete(JsonNull.INSTANCE) }
        n.notify(project)
        if (items.isEmpty()) f.complete(JsonNull.INSTANCE)
    }

    private fun pick(p: JsonObject, f: CompletableFuture<JsonElement>) {
        val rows = p["items"].asJsonArray.map { it.asJsonObject }
        val labels = rows.map { r -> listOfNotNull(r["label"]?.asString, r["description"]?.takeIf { !it.isJsonNull }?.asString).joinToString("  ·  ") }
        val step = object : BaseListPopupStep<String>(p["title"]?.takeIf { !it.isJsonNull }?.asString ?: p["placeHolder"]?.takeIf { !it.isJsonNull }?.asString, labels) {
            override fun onChosen(selectedValue: String, finalChoice: Boolean): PopupStep<*>? {
                f.complete(JsonPrimitive(labels.indexOf(selectedValue)))
                return FINAL_CHOICE
            }
            override fun isSpeedSearchEnabled(): Boolean = true
            override fun canceled() { f.complete(JsonNull.INSTANCE) }
        }
        JBPopupFactory.getInstance().createListPopup(step).showCenteredInCurrentWindow(project)
    }

    private val progress = ConcurrentHashMap<Int, ProgressIndicator>()
    private val progressEnd = ConcurrentHashMap<Int, CompletableFuture<Unit>>()
    private val nextProgress = AtomicInteger(1)

    private fun progressStart(title: String): CompletableFuture<JsonElement> {
        val id = nextProgress.getAndIncrement()
        val end = CompletableFuture<Unit>()
        progressEnd[id] = end
        ApplicationManager.getApplication().invokeLater {
            ProgressManager.getInstance().run(object : Task.Backgroundable(project, title, false) {
                override fun run(indicator: ProgressIndicator) {
                    progress[id] = indicator
                    indicator.isIndeterminate = true
                    runCatching { end.get() }
                    progress.remove(id)
                    progressEnd.remove(id)
                }
            })
        }
        return done(JsonPrimitive(id))
    }

    private fun findFiles(p: JsonObject): JsonElement {
        val base = Path.of(p["base"]?.takeIf { !it.isJsonNull }?.asString ?: project.basePath ?: return JsonArray())
        val matcher = FileSystems.getDefault().getPathMatcher("glob:${p["pattern"].asString}")
        val skip = Regex("(^|/)(node_modules|target|\\.git|\\.mcpp|build)(/|$)")
        val max = p["max"]?.takeIf { !it.isJsonNull }?.asInt ?: 10000
        val out = JsonArray()
        if (!Files.isDirectory(base)) return out
        Files.walk(base).use { stream ->
            stream.filter { Files.isRegularFile(it) }.filter { !skip.containsMatchIn(base.relativize(it).toString().replace('\\', '/')) }
                .filter { matcher.matches(base.relativize(it)) }.limit(max.toLong()).forEach { out.add(it.toString()) }
        }
        return out
    }

    override fun dispose() {
        unsubscribe.forEach { it() }
        progressEnd.values.forEach { it.complete(Unit) }
        query?.let { Disposer.dispose(it) }
        browser?.let { Disposer.dispose(it) }
    }
}

/**
 * The IDE's theme as the `--vscode-*` variables the pages' stylesheets read: VS Code's names, the
 * IDE's colours and fonts.
 */
object Theme {
    private fun css(c: Color) = if (c.alpha == 255) "rgb(${c.red}, ${c.green}, ${c.blue})" else "rgba(${c.red}, ${c.green}, ${c.blue}, ${c.alpha / 255.0})"
    private fun named(key: String, light: Int, dark: Int) = JBColor.namedColor(key, JBColor(light, dark))
    private fun pick(light: String, dark: String) = if (JBColor.isBright()) light else dark

    fun variables(): JsonObject = JsonObject().apply {
        val fg = UIUtil.getLabelForeground()
        val muted = UIUtil.getContextHelpForeground()
        val border = JBColor.border()
        val scheme = EditorColorsManager.getInstance().globalScheme
        val editorBg = scheme.defaultBackground
        val panel = UIUtil.getPanelBackground()
        val inputBg = UIUtil.getTextFieldBackground()
        val inputFg = UIUtil.getTextFieldForeground()
        val accent = named("Button.default.startBackground", 0x3574F0, 0x3574F0)
        val accentFg = named("Button.default.foreground", 0xFFFFFF, 0xFFFFFF)
        val secondary = named("Button.startBackground", 0xEBECF0, 0x3C3F41)
        val link = JBUI.CurrentTheme.Link.Foreground.ENABLED
        val hover = JBUI.CurrentTheme.List.Hover.background(true)
        val selection = UIUtil.getListSelectionBackground(true)
        val selectionFg = UIUtil.getListSelectionForeground(true)
        val popup = named("PopupMenu.background", 0xFFFFFF, 0x2B2D30)
        val warning = named("Component.warningFocusColor", 0xE2A53A, 0xE2A53A)
        val error = named("Label.errorForeground", 0xC7222D, 0xF75464)
        val font = UIUtil.getLabelFont()
        fun v(name: String, value: String) = addProperty("--vscode-$name", value)
        v("foreground", css(fg)); v("editorForeground", css(fg)); v("icon-foreground", css(fg))
        v("descriptionForeground", css(muted)); v("editorCodeLens-foreground", css(muted)); v("input-placeholderForeground", css(muted))
        v("editor-background", css(editorBg)); v("sideBar-background", css(panel)); v("editorWidget-background", css(popup))
        v("textCodeBlock-background", css(ColorUtilCompat.blend(editorBg, fg, 0.06)))
        for (b in listOf("panel-border", "contrastBorder", "widget-border", "menu-border", "sideBarSectionHeader-border", "input-border", "dropdown-border")) v(b, css(border))
        v("focusBorder", css(JBUI.CurrentTheme.Focus.focusColor()))
        v("textLink-foreground", css(link)); v("textLink-activeForeground", css(link))
        v("input-background", css(inputBg)); v("input-foreground", css(inputFg))
        v("dropdown-background", css(inputBg)); v("dropdown-foreground", css(inputFg))
        v("checkbox-background", css(inputBg)); v("checkbox-selectBackground", css(inputBg))
        v("inputOption-activeBackground", css(ColorUtilCompat.blend(inputBg, accent, 0.25))); v("inputOption-activeBorder", css(accent)); v("inputOption-activeForeground", css(fg))
        v("inputValidation-warningBackground", css(ColorUtilCompat.blend(inputBg, warning, 0.2))); v("inputValidation-warningBorder", css(warning))
        v("inputValidation-infoBackground", css(ColorUtilCompat.blend(inputBg, accent, 0.15))); v("inputValidation-infoBorder", css(accent))
        v("button-background", css(accent)); v("button-foreground", css(accentFg)); v("button-hoverBackground", css(ColorUtilCompat.blend(accent, Color.BLACK, 0.1)))
        v("button-secondaryBackground", css(secondary)); v("button-secondaryForeground", css(fg)); v("button-secondaryHoverBackground", css(ColorUtilCompat.blend(secondary, fg, 0.08)))
        v("button-border", "transparent"); v("button-separator", "transparent")
        v("list-hoverBackground", css(hover)); v("list-activeSelectionBackground", css(selection)); v("list-activeSelectionForeground", css(selectionFg))
        v("list-inactiveSelectionBackground", css(UIUtil.getListSelectionBackground(false)))
        v("toolbar-hoverBackground", css(JBUI.CurrentTheme.ActionButton.hoverBackground())); v("toolbar-activeBackground", css(JBUI.CurrentTheme.ActionButton.pressedBackground()))
        v("badge-background", css(named("Counter.background", 0xDFE1E5, 0x4E5157))); v("badge-foreground", css(named("Counter.foreground", 0x000000, 0xDFE1E5)))
        v("menu-background", css(popup)); v("menu-foreground", css(fg))
        v("menu-selectionBackground", css(selection)); v("menu-selectionForeground", css(selectionFg))
        v("sideBarSectionHeader-background", css(panel)); v("sideBarSectionHeader-foreground", css(fg))
        v("widget-shadow", "rgba(0, 0, 0, 0.36)")
        v("errorForeground", css(error)); v("editorWarning-foreground", css(warning))
        v("charts-red", pick("#e51400", "#f14c4c")); v("charts-blue", pick("#1a85ff", "#3794ff")); v("charts-yellow", pick("#bf8803", "#cca700"))
        v("charts-orange", pick("#d18616", "#d18616")); v("charts-green", pick("#388a34", "#89d185")); v("charts-purple", pick("#652d90", "#b180d7"))
        v("testing-iconPassed", pick("#388a34", "#73c991"))
        v("symbolIcon-keywordForeground", css(fg)); v("symbolIcon-interfaceForeground", pick("#1a85ff", "#75beff")); v("symbolIcon-operatorForeground", css(fg))
        v("mcpp-cacheOkForeground", pick("#388a34", "#89d185")); v("mcpp-cacheStaleForeground", pick("#bf8803", "#e2c08d"))
        v("font-family", "\"${font.family}\", system-ui, sans-serif"); v("font-size", "${font.size}px")
        v("editor-font-family", "\"${scheme.editorFontName}\", monospace")
    }
}

/** Colour arithmetic the theme needs (a mix of two colours). */
private object ColorUtilCompat {
    fun blend(a: Color, b: Color, t: Double): Color = Color(
        (a.red + (b.red - a.red) * t).toInt().coerceIn(0, 255),
        (a.green + (b.green - a.green) * t).toInt().coerceIn(0, 255),
        (a.blue + (b.blue - a.blue) * t).toInt().coerceIn(0, 255),
    )
}
