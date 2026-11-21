// The backend's wire, as the plugin speaks it: `mcpp-ide serve` as a child process, an LSP server
// (protocol 2, mcpp design 2026-10-30 part 2 §8) on its standard streams. LSP's methods go as they
// are named; mcpp-ide's own are under `mcpp-ide/`, which this class adds to a request and takes off
// a notification. Requests answer futures; notifications go to the listeners of their method ("*"
// receives every one).
//
// The thread that reads the backend's output only decodes frames: a notification's listeners run on
// `events`, one at a time and in the order they arrived, and a request's future is completed on
// `answers`, where whatever waits on it continues. A listener that reads the IDE's model therefore
// never runs on the reading thread, and a slow one never holds up the next answer (#13).
package io.github.mcppcommunity.mcppclion.backend

import com.google.gson.JsonElement
import com.google.gson.JsonNull
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.nio.charset.StandardCharsets
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.Executor
import java.util.concurrent.Executors
import java.util.concurrent.ForkJoinPool
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class BackendError(val method: String, val code: Int, message: String, val data: JsonElement?) : RuntimeException(message)

/** Splits a byte stream into the bodies of Content-Length frames. */
class FrameReader {
    private val buffer = ByteArrayOutputStream()

    fun feed(bytes: ByteArray, length: Int = bytes.size): List<String> {
        buffer.write(bytes, 0, length)
        val out = mutableListOf<String>()
        while (true) {
            val all = buffer.toByteArray()
            val headerEnd = indexOf(all, "\r\n\r\n".toByteArray())
            if (headerEnd < 0) break
            val headers = String(all, 0, headerEnd, StandardCharsets.US_ASCII)
            val match = Regex("content-length:\\s*(\\d+)", RegexOption.IGNORE_CASE).find(headers)
            val start = headerEnd + 4
            if (match == null) { reset(all, start); continue }
            val n = match.groupValues[1].toInt()
            if (all.size < start + n) break
            out += String(all, start, n, StandardCharsets.UTF_8)
            reset(all, start + n)
        }
        return out
    }

    private fun reset(all: ByteArray, from: Int) {
        buffer.reset()
        buffer.write(all, from, all.size - from)
    }

    private fun indexOf(hay: ByteArray, needle: ByteArray): Int {
        outer@ for (i in 0..hay.size - needle.size) {
            for (j in needle.indices) if (hay[i + j] != needle[j]) continue@outer
            return i
        }
        return -1
    }
}

fun encode(message: JsonObject): ByteArray {
    val body = message.toString().toByteArray(StandardCharsets.UTF_8)
    return "Content-Length: ${body.size}\r\n\r\n".toByteArray(StandardCharsets.US_ASCII) + body
}

/** mcpp-ide's methods and notifications are under this namespace; LSP's are not. */
const val NAMESPACE = "mcpp-ide/"

/** The name a method has on the wire: LSP's own as it is, mcpp-ide's under its namespace. */
fun wireName(method: String): String =
    if (method in setOf("initialize", "initialized", "shutdown", "exit") || method.startsWith("$/") ||
        method.startsWith("textDocument/") || method.startsWith("workspace/") || method.startsWith("window/") ||
        method.startsWith(NAMESPACE)) method
    else NAMESPACE + method

/** One daemon thread, for the notifications of a backend run without the IDE's executors (the tests). */
fun sequentialExecutor(name: String): Executor = Executors.newSingleThreadExecutor { r -> Thread(r, name).apply { isDaemon = true } }

class Backend(
    command: List<String>,
    private val log: (String) -> Unit = {},
    private val events: Executor = sequentialExecutor("mcpp-ide events"),
    private val answers: Executor = ForkJoinPool.commonPool(),
) : AutoCloseable {
    private val process: Process = ProcessBuilder(command).redirectErrorStream(false).start()
    private val input: OutputStream = process.outputStream
    private val pending = ConcurrentHashMap<Int, Pair<String, CompletableFuture<JsonElement>>>()
    private val listeners = ConcurrentHashMap<String, CopyOnWriteArrayList<(String, JsonElement) -> Unit>>()
    private val nextId = AtomicInteger(1)
    private val writeLock = Any()

    val running: Boolean get() = process.isAlive

    /** The exit status, once the process has ended. */
    val exited = CompletableFuture<Int>()

    init {
        thread("mcpp-ide stdout") { read(process.inputStream) }
        thread("mcpp-ide stderr") { process.errorStream.bufferedReader().forEachLine(log) }
        thread("mcpp-ide exit") {
            process.waitFor()
            val ended = BackendError("", -32099, "the mcpp-ide backend ended", null)
            val waiting = pending.values.toList()
            pending.clear()
            val code = process.exitValue()
            answers.execute { waiting.forEach { it.second.completeExceptionally(ended) } }
            events.execute { exited.complete(code) }
        }
    }

    private fun thread(name: String, body: () -> Unit) = Thread(body, name).apply { isDaemon = true; start() }

    private fun read(stream: InputStream) {
        val reader = FrameReader()
        val chunk = ByteArray(65536)
        while (true) {
            val n = try { stream.read(chunk) } catch (_: Exception) { -1 }
            if (n < 0) break
            for (body in reader.feed(chunk, n)) dispatch(body)
        }
    }

    private fun dispatch(body: String) {
        val message = try { JsonParser.parseString(body).asJsonObject } catch (_: Exception) { return }
        val id = message.get("id")
        if (id != null && !id.isJsonNull && (message.has("result") || message.has("error"))) {
            val (method, future) = pending.remove(id.asInt) ?: return
            if (message.has("error")) {
                val e = message.getAsJsonObject("error")
                val error = BackendError(method, e.get("code")?.asInt ?: -32603, e.get("message")?.asString ?: "failed", e.get("data"))
                answers.execute { future.completeExceptionally(error) }
            } else {
                val result = message.get("result") ?: JsonNull.INSTANCE
                answers.execute { future.complete(result) }
            }
            return
        }
        val method = message.get("method")?.asString?.removePrefix(NAMESPACE) ?: return
        val params = message.get("params") ?: JsonObject()
        events.execute {
            listeners[method]?.forEach { runCatching { it(method, params) }.onFailure { e -> log("$method: ${e.message}") } }
            listeners["*"]?.forEach { runCatching { it(method, params) } }
        }
    }

    fun request(method: String, params: JsonElement = JsonObject()): CompletableFuture<JsonElement> {
        val future = CompletableFuture<JsonElement>()
        if (!process.isAlive) {
            future.completeExceptionally(BackendError(method, -32099, "the mcpp-ide backend is not running", null))
            return future
        }
        val id = nextId.getAndIncrement()
        pending[id] = method to future
        send(JsonObject().apply { addProperty("jsonrpc", "2.0"); addProperty("id", id); addProperty("method", wireName(method)); add("params", params) })
        return future
    }

    fun notify(method: String, params: JsonElement = JsonObject()) {
        if (process.isAlive) send(JsonObject().apply { addProperty("jsonrpc", "2.0"); addProperty("method", wireName(method)); add("params", params) })
    }

    private fun send(message: JsonObject) {
        synchronized(writeLock) {
            try {
                input.write(encode(message))
                input.flush()
            } catch (e: Exception) {
                log("write failed: ${e.message}")
            }
        }
    }

    fun on(method: String, listener: (String, JsonElement) -> Unit): () -> Unit {
        val list = listeners.computeIfAbsent(method) { CopyOnWriteArrayList() }
        list += listener
        return { list -= listener }
    }

    override fun close() {
        if (!process.isAlive) return
        runCatching { request("shutdown").get(2, TimeUnit.SECONDS) }
        notify("exit")
        runCatching { input.close() }
        if (!process.waitFor(3, TimeUnit.SECONDS)) process.destroy()
    }
}
