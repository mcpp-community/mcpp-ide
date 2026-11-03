// mcpp.toml and build.mcpp in CLion: the backend's rules layer (mcpp design 2026-10-30 part 2 §8) --
// completion, documentation, the jumps of mcpp.toml and the diagnostics of both -- over the
// session's own connection to `mcpp-ide serve`, which is an LSP server. The platform's LSP client
// starts the server it talks to, and a second backend process is what §8 rules out, so the requests
// are LSP's, sent by the session, and their answers reach CLion through its own extension points.
//
// The editor's text reaches the backend whole (`didOpen`, then `didChange` with each new version);
// positions are a 0-based line and a UTF-16 column, which a Document's offsets already count.
package io.github.mcppcommunity.mcppclion.editor

import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonObject
import com.intellij.codeInsight.completion.CompletionContributor
import com.intellij.codeInsight.completion.CompletionParameters
import com.intellij.codeInsight.completion.CompletionResultSet
import com.intellij.codeInsight.lookup.LookupElementBuilder
import com.intellij.codeInsight.navigation.actions.GotoDeclarationHandler
import com.intellij.lang.annotation.AnnotationHolder
import com.intellij.lang.annotation.ExternalAnnotator
import com.intellij.lang.annotation.HighlightSeverity
import com.intellij.lang.documentation.AbstractDocumentationProvider
import com.intellij.openapi.Disposable
import com.intellij.openapi.components.Service
import com.intellij.openapi.editor.Document
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.EditorFactory
import com.intellij.openapi.editor.event.DocumentEvent
import com.intellij.openapi.editor.event.DocumentListener
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.fileEditor.FileEditorManagerListener
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Key
import com.intellij.openapi.util.TextRange
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.PsiDocumentManager
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiFile
import com.intellij.psi.PsiManager
import io.github.mcppcommunity.mcppclion.McppSession
import io.github.mcppcommunity.mcppclion.uriOf
import java.net.URLDecoder
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit

/** The files the rules layer reads. */
internal fun served(file: VirtualFile?): Boolean =
    file != null && file.isInLocalFileSystem && (file.name == "mcpp.toml" || file.name == "build.mcpp")

/** A `file:` URI's path, as the backend wrote it (`file:///C:/x` is `C:/x`). */
internal fun pathOf(uri: String): String {
    var p = URLDecoder.decode(uri.removePrefix("file://").replace("+", "%2B"), Charsets.UTF_8)
    if (p.length >= 3 && p[0] == '/' && p[1].isLetter() && p[2] == ':') p = p.substring(1)
    return p
}

private fun position(document: Document, offset: Int): JsonObject {
    val line = document.getLineNumber(offset)
    return JsonObject().apply {
        addProperty("line", line)
        addProperty("character", offset - document.getLineStartOffset(line))
    }
}

private fun offsetOf(document: Document, position: JsonObject): Int {
    val line = position["line"].asInt
    if (line >= document.lineCount) return document.textLength
    val start = document.getLineStartOffset(line)
    return (start + position["character"].asInt).coerceIn(start, document.getLineEndOffset(line))
}

/** The documents the backend holds, and what it published about each. */
@Service(Service.Level.PROJECT)
class McppDocuments(private val project: Project) : Disposable {
    private val versions = ConcurrentHashMap<String, Int>()
    private val sent = ConcurrentHashMap<String, String>()
    private val published = ConcurrentHashMap<String, JsonObject>()
    private val waiting = ConcurrentHashMap<String, MutableList<Pair<Int, CompletableFuture<JsonObject>>>>()
    @Volatile private var installed = false

    fun install() {
        if (installed) return
        installed = true
        val session = McppSession.get(project)
        session.on("textDocument/publishDiagnostics") { p -> onPublished(p.asJsonObject) }
        // A backend that started again knows no document: each open one is sent again.
        session.whenStarted { reopen() }
        project.messageBus.connect(this).subscribe(FileEditorManagerListener.FILE_EDITOR_MANAGER, object : FileEditorManagerListener {
            override fun fileOpened(source: FileEditorManager, file: VirtualFile) {
                if (served(file)) FileDocumentManager.getInstance().getDocument(file)?.let { sync(file, it) }
            }

            override fun fileClosed(source: FileEditorManager, file: VirtualFile) {
                if (served(file)) close(file)
            }
        })
        EditorFactory.getInstance().eventMulticaster.addDocumentListener(object : DocumentListener {
            override fun documentChanged(event: DocumentEvent) {
                val file = FileDocumentManager.getInstance().getFile(event.document) ?: return
                if (served(file) && sent.containsKey(uriOf(file.path))) sync(file, event.document)
            }
        }, this)
        reopen()
    }

    private fun reopen() {
        sent.clear()
        for (file in FileEditorManager.getInstance(project).openFiles)
            if (served(file)) FileDocumentManager.getInstance().getDocument(file)?.let { sync(file, it) }
    }

    /** Sends the document's text when it differs from what the backend holds: its uri and version. */
    fun sync(file: VirtualFile, document: Document): Pair<String, Int>? {
        val session = McppSession.get(project)
        if (!session.running) return null
        val uri = uriOf(file.path)
        val text = document.immutableCharSequence.toString()
        synchronized(this) {
            val last = sent[uri]
            if (last == text) return uri to (versions[uri] ?: 0)
            val version = (versions[uri] ?: 0) + 1
            versions[uri] = version
            if (last == null) {
                session.notifyBackend("textDocument/didOpen", JsonObject().apply {
                    add("textDocument", JsonObject().apply {
                        addProperty("uri", uri)
                        addProperty("languageId", if (file.name == "mcpp.toml") "mcpp-toml" else "cpp")
                        addProperty("version", version)
                        addProperty("text", text)
                    })
                })
            } else {
                session.notifyBackend("textDocument/didChange", JsonObject().apply {
                    add("textDocument", JsonObject().apply { addProperty("uri", uri); addProperty("version", version) })
                    add("contentChanges", JsonArray().apply { add(JsonObject().apply { addProperty("text", text) }) })
                })
            }
            sent[uri] = text
            return uri to version
        }
    }

    fun close(file: VirtualFile) {
        val uri = uriOf(file.path)
        if (sent.remove(uri) != null)
            McppSession.get(project).notifyBackend("textDocument/didClose", JsonObject().apply {
                add("textDocument", JsonObject().apply { addProperty("uri", uri) })
            })
        published.remove(uri)
    }

    private fun onPublished(p: JsonObject) {
        val uri = p["uri"]?.asString ?: return
        published[uri] = p
        val version = p["version"]?.takeIf { !it.isJsonNull }?.asInt ?: Int.MAX_VALUE
        waiting[uri]?.let { list ->
            synchronized(list) {
                list.removeIf { (wanted, future) -> (version >= wanted).also { if (it) future.complete(p) } }
            }
        }
    }

    /** The diagnostics the backend published for `version` of the document or a later one. */
    fun diagnostics(uri: String, version: Int, timeoutMs: Long): JsonArray? {
        published[uri]?.let { p -> if ((p["version"]?.takeIf { !it.isJsonNull }?.asInt ?: 0) >= version) return p.getAsJsonArray("diagnostics") }
        val future = CompletableFuture<JsonObject>()
        val list = waiting.computeIfAbsent(uri) { mutableListOf() }
        synchronized(list) { list += version to future }
        // Published while this waiter was being registered.
        published[uri]?.let { onPublished(it) }
        return runCatching { future.get(timeoutMs, TimeUnit.MILLISECONDS).getAsJsonArray("diagnostics") }.getOrNull()
    }

    override fun dispose() {}

    companion object {
        fun get(project: Project): McppDocuments = project.getService(McppDocuments::class.java)
    }
}

/** The rules' diagnostics as CLion's annotations. */
class McppAnnotator : ExternalAnnotator<McppAnnotator.Input, JsonArray>() {
    class Input(val project: Project, val file: VirtualFile, val document: Document)

    override fun collectInformation(file: PsiFile, editor: Editor, hasErrors: Boolean): Input? {
        val vf = file.virtualFile ?: return null
        if (!served(vf)) return null
        return Input(file.project, vf, editor.document)
    }

    override fun doAnnotate(input: Input): JsonArray? {
        val documents = McppDocuments.get(input.project)
        documents.install()
        val (uri, version) = documents.sync(input.file, input.document) ?: return null
        return documents.diagnostics(uri, version, 2000)
    }

    override fun apply(file: PsiFile, result: JsonArray?, holder: AnnotationHolder) {
        val document = PsiDocumentManager.getInstance(file.project).getDocument(file) ?: return
        for (element in result ?: return) {
            val d = element.asJsonObject
            val range = d.getAsJsonObject("range")
            val start = offsetOf(document, range.getAsJsonObject("start"))
            var end = offsetOf(document, range.getAsJsonObject("end"))
            if (end <= start) end = minOf(start + 1, document.textLength)
            if (end <= start) continue
            val severity = when (d["severity"]?.asInt) {
                1 -> HighlightSeverity.ERROR
                2 -> HighlightSeverity.WARNING
                else -> HighlightSeverity.WEAK_WARNING
            }
            val code = d["code"]?.takeIf { it.isJsonPrimitive }?.asString
            holder.newAnnotation(severity, d["message"].asString).range(TextRange(start, end))
                .apply { if (code != null) tooltip("${d["message"].asString} <i>($code)</i>") }.create()
        }
    }
}

/** A snippet (`${1:name}`, `$1`, `${2|a,b|}`) as plain text and where the caret goes: its first stop. */
internal fun expandSnippet(snippet: String): Pair<String, Int?> {
    val out = StringBuilder()
    var caret: Int? = null
    var firstStop = Int.MAX_VALUE
    var i = 0
    while (i < snippet.length) {
        val c = snippet[i]
        if (c == '\\' && i + 1 < snippet.length) { out.append(snippet[i + 1]); i += 2; continue }
        if (c == '$' && i + 1 < snippet.length) {
            val brace = snippet[i + 1] == '{'
            var j = if (brace) i + 2 else i + 1
            val digits = StringBuilder()
            while (j < snippet.length && snippet[j].isDigit()) digits.append(snippet[j++])
            if (digits.isNotEmpty()) {
                val stop = digits.toString().toInt()
                var text = ""
                if (brace) {
                    var depth = 1
                    val body = StringBuilder()
                    while (j < snippet.length && depth > 0) {
                        val d = snippet[j]
                        if (d == '{') depth++
                        if (d == '}') depth--
                        if (depth > 0) body.append(d)
                        j++
                    }
                    text = when {
                        body.startsWith(":") -> expandSnippet(body.substring(1)).first
                        body.startsWith("|") -> body.removePrefix("|").removeSuffix("|").split(',').first()
                        else -> ""
                    }
                }
                if (stop in 1 until firstStop) { firstStop = stop; caret = out.length }
                out.append(text)
                i = j
                continue
            }
        }
        out.append(c)
        i++
    }
    return out.toString() to caret
}

/** The backend's completion in mcpp.toml and build.mcpp. */
class McppCompletion : CompletionContributor() {
    override fun fillCompletionVariants(parameters: CompletionParameters, result: CompletionResultSet) {
        val vf = parameters.originalFile.virtualFile ?: return
        if (!served(vf)) return
        val project = parameters.originalFile.project
        val document = parameters.editor.document
        val documents = McppDocuments.get(project)
        documents.install()
        val (uri, _) = documents.sync(vf, document) ?: return
        val offset = parameters.offset
        val params = JsonObject().apply {
            add("textDocument", JsonObject().apply { addProperty("uri", uri) })
            add("position", position(document, offset))
        }
        val answer = runCatching { McppSession.get(project).request("textDocument/completion", params).get(2, TimeUnit.SECONDS) }
            .getOrNull()?.takeIf { it.isJsonObject }?.asJsonObject ?: return
        val lineStart = document.getLineStartOffset(document.getLineNumber(offset))
        for (element in answer.getAsJsonArray("items") ?: return) {
            val item = element.asJsonObject
            val label = item["label"].asString
            val edit = item["textEdit"]?.takeIf { it.isJsonObject }?.asJsonObject
            val start = edit?.getAsJsonObject("range")?.getAsJsonObject("start")?.let { lineStart + it["character"].asInt }
            val end = edit?.getAsJsonObject("range")?.getAsJsonObject("end")?.let { lineStart + it["character"].asInt }
            val text = edit?.get("newText")?.asString ?: item["insertText"]?.asString ?: label
            val (plain, caret) = if (item["insertTextFormat"]?.asInt == 2) expandSnippet(text) else text to null
            val lookup = item["filterText"]?.asString ?: label
            val built = LookupElementBuilder.create(lookup)
                .withPresentableText(label)
                .withTypeText(item["detail"]?.asString, true)
                .withInsertHandler { context, _ ->
                    // What the lookup inserted is replaced by the item's text, and what the edit
                    // covered past the cursor goes too.
                    val past = if (end != null) (end - offset).coerceAtLeast(0) else 0
                    val tail = minOf(context.tailOffset + past, context.document.textLength)
                    context.document.replaceString(context.startOffset, tail, plain)
                    context.editor.caretModel.moveToOffset(context.startOffset + (caret ?: plain.length))
                }
            val prefix = if (start != null && start <= offset) document.getText(TextRange(start, offset)) else result.prefixMatcher.prefix
            result.withPrefixMatcher(prefix).addElement(built)
        }
    }
}

/** The backend's hover as CLion's documentation, in mcpp.toml and build.mcpp. */
class McppDocumentation : AbstractDocumentationProvider() {
    override fun getCustomDocumentationElement(editor: Editor, file: PsiFile, contextElement: PsiElement?, targetOffset: Int): PsiElement? {
        if (!served(file.virtualFile)) return null
        file.putUserData(OFFSET, targetOffset)
        return contextElement ?: file
    }

    override fun generateDoc(element: PsiElement, originalElement: PsiElement?): String? {
        val file = element.containingFile ?: return null
        val vf = file.virtualFile
        if (!served(vf)) return null
        val project = file.project
        val document = PsiDocumentManager.getInstance(project).getDocument(file) ?: return null
        val documents = McppDocuments.get(project)
        documents.install()
        val (uri, _) = documents.sync(vf, document) ?: return null
        val offset = file.getUserData(OFFSET) ?: (originalElement ?: element).textOffset
        val params = JsonObject().apply {
            add("textDocument", JsonObject().apply { addProperty("uri", uri) })
            add("position", position(document, offset.coerceIn(0, document.textLength)))
        }
        val answer = runCatching { McppSession.get(project).request("textDocument/hover", params).get(2, TimeUnit.SECONDS) }.getOrNull()
        val value = answer?.takeIf { it.isJsonObject }?.asJsonObject?.getAsJsonObject("contents")?.get("value")?.asString ?: return null
        return markdownToHtml(value)
    }

    companion object {
        private val OFFSET = Key.create<Int>("mcpp.documentation.offset")
    }
}

/** The little markdown the rules write: paragraphs, **bold**, `code`, [text](link). */
internal fun markdownToHtml(markdown: String): String {
    fun inline(text: String): String {
        var s = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        s = Regex("\\[([^\\]]+)]\\(([^)]+)\\)").replace(s) { "<a href=\"${it.groupValues[2]}\">${it.groupValues[1]}</a>" }
        s = Regex("\\*\\*([^*]+)\\*\\*").replace(s) { "<b>${it.groupValues[1]}</b>" }
        s = Regex("`([^`]+)`").replace(s) { "<code>${it.groupValues[1]}</code>" }
        return s
    }
    return markdown.split(Regex("\\n\\s*\\n")).joinToString("") { "<p>${inline(it.trim()).replace("\n", "<br>")}</p>" }
}

/** mcpp.toml's jumps: a workspace dependency, a feature, a path dependency's manifest. */
class McppGotoDeclaration : GotoDeclarationHandler {
    override fun getGotoDeclarationTargets(sourceElement: PsiElement?, offset: Int, editor: Editor): Array<PsiElement>? {
        val file = sourceElement?.containingFile ?: return null
        val vf = file.virtualFile
        if (!served(vf) || vf.name != "mcpp.toml") return null
        val project = file.project
        val documents = McppDocuments.get(project)
        documents.install()
        val (uri, _) = documents.sync(vf, editor.document) ?: return null
        val params = JsonObject().apply {
            add("textDocument", JsonObject().apply { addProperty("uri", uri) })
            add("position", position(editor.document, offset))
        }
        val answer: JsonElement = runCatching { McppSession.get(project).request("textDocument/definition", params).get(2, TimeUnit.SECONDS) }
            .getOrNull() ?: return null
        val location = answer.takeIf { it.isJsonObject }?.asJsonObject ?: return null
        val target = LocalFileSystem.getInstance().findFileByPath(pathOf(location["uri"].asString)) ?: return null
        val psi = PsiManager.getInstance(project).findFile(target) ?: return null
        val document = FileDocumentManager.getInstance().getDocument(target) ?: return null
        val at = offsetOf(document, location.getAsJsonObject("range").getAsJsonObject("start"))
        return arrayOf(psi.findElementAt(at) ?: psi)
    }
}
