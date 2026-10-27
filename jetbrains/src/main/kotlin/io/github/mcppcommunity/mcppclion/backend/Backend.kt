// The backend's wire, as the plugin speaks it: `mcpp-ide serve` as a child process, JSON-RPC 2.0
// framed as the LSP base protocol on its standard streams. Requests answer futures; notifications
// go to the listeners of their method ("*" receives every one).
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

class Backend(command: List<String>, private val log: (String) -> Unit = {}) : AutoCloseable {
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
            pending.values.forEach { it.second.completeExceptionally(ended) }
            pending.clear()
            exited.complete(process.exitValue())
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
                future.completeExceptionally(BackendError(method, e.get("code")?.asInt ?: -32603, e.get("message")?.asString ?: "failed", e.get("data")))
            } else future.complete(message.get("result") ?: JsonNull.INSTANCE)
            return
        }
        val method = message.get("method")?.asString ?: return
        val params = message.get("params") ?: JsonObject()
        listeners[method]?.forEach { runCatching { it(method, params) }.onFailure { e -> log("$method: ${e.message}") } }
        listeners["*"]?.forEach { runCatching { it(method, params) } }
    }

    fun request(method: String, params: JsonElement = JsonObject()): CompletableFuture<JsonElement> {
        val future = CompletableFuture<JsonElement>()
        if (!process.isAlive) {
            future.completeExceptionally(BackendError(method, -32099, "the mcpp-ide backend is not running", null))
            return future
        }
        val id = nextId.getAndIncrement()
        pending[id] = method to future
        send(JsonObject().apply { addProperty("jsonrpc", "2.0"); addProperty("id", id); addProperty("method", method); add("params", params) })
        return future
    }

    fun notify(method: String, params: JsonElement = JsonObject()) {
        if (process.isAlive) send(JsonObject().apply { addProperty("jsonrpc", "2.0"); addProperty("method", method); add("params", params) })
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
