// What a run of an "mcpp" configuration is: a backend job (build), a backend test job feeding the
// test tree, or the program itself (run). Each is a ProcessHandler, so the console, the stop button
// and the run toolbar behave as for any other run in the IDE.
package io.github.mcppcommunity.mcppclion.run

import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonObject
import com.intellij.execution.DefaultExecutionResult
import com.intellij.execution.ExecutionResult
import com.intellij.execution.Executor
import com.intellij.execution.configurations.CommandLineState
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.configurations.PtyCommandLine
import com.intellij.execution.process.AnsiEscapeDecoder
import com.intellij.execution.process.KillableColoredProcessHandler
import com.intellij.execution.process.ProcessEvent
import com.intellij.execution.process.ProcessHandler
import com.intellij.execution.process.ProcessListener
import com.intellij.execution.process.ProcessOutputTypes
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.execution.runners.ProgramRunner
import com.intellij.execution.testframework.sm.SMTestRunnerConnectionUtil
import com.intellij.execution.testframework.sm.runner.SMTRunnerConsoleProperties
import com.intellij.openapi.util.Key
import io.github.mcppcommunity.mcppclion.McppSession
import java.io.OutputStream
import java.util.concurrent.CompletableFuture

/** The parameters every job of a configuration shares: project, target, profile. */
internal fun jobParams(c: McppRunConfiguration, kind: String): JsonObject = JsonObject().apply {
    addProperty("kind", kind)
    addProperty("root", c.root)
    addProperty("color", true)
    if (c.options.target.isNotBlank()) addProperty("target", c.options.target)
    if (c.options.profile.isNotBlank()) addProperty("profile", c.options.profile)
}

/**
 * A backend job as a process: started on `startNotify`, its narration (ANSI colours decoded) as the
 * output, its end as the exit code; stopping it cancels the job, which mcpp ends cleanly.
 */
open class JobProcessHandler(protected val session: McppSession, private val params: JsonObject, private val method: String = "job/start") :
    ProcessHandler(), AnsiEscapeDecoder.ColoredTextAcceptor {
    private val decoder = AnsiEscapeDecoder()
    @Volatile protected var jobId: String? = null
    private val early = mutableListOf<Pair<String, JsonElement>>()
    private val subscriptions = mutableListOf<() -> Unit>()

    override fun startNotify() {
        super.startNotify()
        for (m in listOf("job/output", "job/finished", "test/result", "test/summary", "test/caseResult"))
            subscriptions += session.on(m) { p -> route(m, p) }
        session.ensureMcppForAction().thenCompose { ready ->
            if (ready) session.request(method, params)
            else CompletableFuture.failedFuture<JsonElement>(IllegalStateException("mcpp is not installed"))
        }.whenComplete { r, e ->
            if (e != null) {
                print("${(e.cause ?: e).message}\n", ProcessOutputTypes.STDERR)
                finish(1)
                return@whenComplete
            }
            val id = r.asJsonObject["jobId"].asString
            val replay: List<Pair<String, JsonElement>>
            synchronized(early) { jobId = id; replay = early.filter { it.second.asJsonObject["jobId"]?.asString == id }; early.clear() }
            replay.forEach { (m, p) -> handle(m, p.asJsonObject) }
        }
    }

    private fun route(method: String, p: JsonElement) {
        val o = p.asJsonObject
        synchronized(early) {
            if (jobId == null) { early += method to p; return }
        }
        if (o["jobId"]?.asString == jobId) handle(method, o)
    }

    protected open fun handle(method: String, p: JsonObject) {
        when (method) {
            "job/output" -> print(p["text"].asString, ProcessOutputTypes.STDOUT)
            "job/finished" -> {
                val status = p["status"]?.asString ?: "failed"
                print("\n${session.t("mcpp {0}: {1}", params["kind"]?.asString ?: "job", status)}\n", ProcessOutputTypes.SYSTEM)
                finish(p["exitCode"]?.takeIf { !it.isJsonNull }?.asInt ?: if (status == "ok") 0 else 1)
            }
        }
    }

    protected fun print(text: String, type: Key<*>) = decoder.escapeText(text.replace("\r\n", "\n"), type, this)

    override fun coloredTextAvailable(text: String, attributes: Key<*>) = notifyTextAvailable(text, attributes)

    private fun finish(code: Int) {
        subscriptions.forEach { it() }
        subscriptions.clear()
        notifyProcessTerminated(code)
    }

    override fun destroyProcessImpl() {
        val id = jobId
        if (id == null) { finish(130); return }
        session.request("job/cancel", JsonObject().apply { addProperty("jobId", id) })
    }

    override fun detachProcessImpl() {
        subscriptions.forEach { it() }
        notifyProcessDetached()
    }

    override fun detachIsDefault(): Boolean = false
    override fun getProcessInput(): OutputStream? = null
}

class McppJobState(private val c: McppRunConfiguration, env: ExecutionEnvironment) : CommandLineState(env) {
    override fun startProcess(): ProcessHandler = JobProcessHandler(McppSession.get(c.project), jobParams(c, "build").apply {
        if (c.programArgs.isNotEmpty()) add("args", JsonArray().apply { c.programArgs.forEach { add(it) } })
    })
}

/**
 * A test job whose results fill the IDE's test tree (design 2026-11-10), as TeamCity service messages
 * of an ID-based tree, so that the parts of a program that run at once report into their own nodes: a
 * program is a suite, a file of a program of several files (`unit/a` of `unit`) a suite inside it, and
 * each case a test, located at its line. A case starts and finishes as the program writes it when the
 * project's mcpp states it (`test/caseEvent`), else with its part's record (`test/result`); a program
 * without cases is one test. A failure that states both sides is a comparison failure, whose "Click to
 * see difference" opens the IDE's diff; a case's metrics are said in its output beside their bounds.
 *
 * EVERY TEST AND CASE HAS ITS OWN OUTPUT (design 2026-11-07, D16): `testStdOut` carries a program's
 * whole output and a case's own (GoogleTest's console between `[ RUN ]` and its result), passed or
 * not; a failure's `details` start with the place its output names (`file:line`, which the console
 * filter links), then the output.
 */
class TestJobProcessHandler(session: McppSession, params: JsonObject, places: Map<String, String>, method: String = "job/start") :
    JobProcessHandler(session, params, method) {
    private val tree = TestTree(places, { session.t(it) }) { root, program, case, file -> caseUrl(session, root, program, case, file) }

    override fun handle(method: String, p: JsonObject) {
        when (method) {
            "test/caseEvent", "test/caseResult", "test/result" -> tree.messages(method, p).takeIf { it.isNotEmpty() }
                ?.let { notifyTextAvailable(it, ProcessOutputTypes.STDOUT) }
            "job/finished" -> {
                tree.close().takeIf { it.isNotEmpty() }?.let { notifyTextAvailable(it, ProcessOutputTypes.STDOUT) }
                super.handle(method, p)
            }
            else -> super.handle(method, p)
        }
    }
}

class McppTestState(private val c: McppRunConfiguration, env: ExecutionEnvironment) : CommandLineState(env) {
    override fun startProcess(): ProcessHandler {
        val session = McppSession.get(c.project)
        // Where each program and each part of one is: its `main` (else its first file), and each part's file.
        val places = runCatching {
            val out = mutableMapOf<String, String>()
            for (t in session.request("test/list", JsonObject().apply { addProperty("root", c.root) }).get().asJsonObject.getAsJsonArray("tests")) {
                val o = t.asJsonObject
                val name = o["test"].asString
                out[name] = o["main"]?.takeIf { it.isJsonPrimitive }?.asString?.ifEmpty { null }
                    ?: o.getAsJsonArray("files")?.firstOrNull()?.asString ?: ""
                o.getAsJsonArray("parts")?.forEach { part -> out[part.asJsonObject["name"].asString] = part.asJsonObject["file"].asString }
            }
            out.toMap()
        }.getOrDefault(emptyMap())
        // A framework's cases (test/runCases): the program receives the framework's filter.
        if (c.options.framework.isNotBlank() && c.options.filter.isNotBlank())
            return TestJobProcessHandler(session, jobParams(c, "test").apply {
                remove("kind")
                addProperty("test", c.options.filter)
                addProperty("framework", c.options.framework)
                add("cases", JsonArray().apply { c.options.cases.lines().filter { it.isNotBlank() }.forEach { add(it) } })
            }, places, "test/runCases")
        return TestJobProcessHandler(session, jobParams(c, "test").apply {
            if (c.options.filter.isNotBlank()) addProperty("filter", c.options.filter)
            if (c.programArgs.isNotEmpty()) add("args", JsonArray().apply { add("--"); c.programArgs.forEach { add(it) } })
        }, places)
    }

    override fun execute(executor: Executor, runner: ProgramRunner<*>): ExecutionResult {
        val handler = startProcess()
        val properties = SMTRunnerConsoleProperties(c, "mcpp", executor).apply { isIdBasedTestTree = true }
        val console = SMTestRunnerConnectionUtil.createAndAttachConsole("mcpp", handler, properties)
        return DefaultExecutionResult(console, handler)
    }
}

/** The parameters of the launch of a configuration: its program, its arguments, and the debugger's engine when debugging. */
internal fun launchParams(c: McppRunConfiguration, debugEngine: String? = null): JsonObject = JsonObject().apply {
    addProperty("root", c.root)
    if (c.options.target.isNotBlank()) addProperty("target", c.options.target)
    if (c.options.profile.isNotBlank()) addProperty("profile", c.options.profile)
    if (c.options.bin.isNotBlank()) addProperty("bin", c.options.bin)
    add("args", JsonArray().apply { c.programArgs.forEach { add(it) } })
    if (debugEngine != null) addProperty("engine", debugEngine)
}

/**
 * A backend job followed to its end (design 2026-11-07 §4.9): mcpp's narration and its progress as
 * they arrive, then the job's `job/finished`. A launch job (`kind: launch`) builds a program as
 * `mcpp run` does and ends with the program's launch (`launch`), so a Run shows the build as the
 * terminal shows it rather than a fixed line, and the program starts the moment it is built.
 */
internal class FollowedJob(
    private val session: McppSession,
    private val params: JsonObject,
    private val onOutput: (String) -> Unit,
    private val onProgress: (Int, Int) -> Unit = { _, _ -> },
) {
    val finished = CompletableFuture<JsonObject>()
    @Volatile var jobId: String? = null
        private set
    private val early = mutableListOf<Pair<String, JsonObject>>()
    private val subscriptions = mutableListOf<() -> Unit>()

    fun start(): FollowedJob {
        for (m in listOf("job/output", "job/progress", "job/finished"))
            subscriptions += session.on(m) { p -> route(m, p.asJsonObject) }
        val requested = System.nanoTime()
        session.ensureMcppForAction().thenCompose { ready ->
            if (ready) session.request("job/start", params)
            else CompletableFuture.failedFuture<JsonElement>(IllegalStateException("mcpp is not installed"))
        }.whenComplete { r, e ->
            if (e != null) { close(); finished.completeExceptionally(e.cause ?: e); return@whenComplete }
            val id = r.asJsonObject["jobId"].asString
            LOG.info("mcpp ${params["kind"]?.asString} job $id accepted in ${(System.nanoTime() - requested) / 1_000_000} ms")
            val replay: List<Pair<String, JsonObject>>
            synchronized(early) { jobId = id; replay = early.filter { it.second["jobId"]?.asString == id }; early.clear() }
            replay.forEach { (m, p) -> handle(m, p) }
        }
        return this
    }

    fun cancel() {
        val id = jobId ?: run { close(); finished.cancel(false); return }
        session.request("job/cancel", JsonObject().apply { addProperty("jobId", id) })
    }

    private fun route(method: String, p: JsonObject) {
        synchronized(early) { if (jobId == null) { early += method to p; return } }
        if (p["jobId"]?.asString == jobId) handle(method, p)
    }

    private fun handle(method: String, p: JsonObject) {
        when (method) {
            "job/output" -> onOutput(p["text"].asString)
            "job/progress" -> {
                val done = p["finished"]?.takeIf { !it.isJsonNull }?.asInt
                val total = p["total"]?.takeIf { !it.isJsonNull }?.asInt
                if (done != null && total != null) onProgress(done, total)
            }
            "job/finished" -> { close(); finished.complete(p) }
        }
    }

    private fun close() {
        subscriptions.forEach { it() }
        subscriptions.clear()
    }

    companion object {
        private val LOG = com.intellij.openapi.diagnostic.Logger.getInstance(FollowedJob::class.java)
    }
}

/** The launch of a finished launch job, or why there is none: the status and mcpp's diagnostics. */
internal fun launchOf(finished: JsonObject, session: McppSession): JsonObject {
    val launch = finished["launch"]?.takeIf { it.isJsonObject }?.asJsonObject
    if (finished["status"]?.asString == "ok" && launch != null) return launch
    val messages = finished.getAsJsonArray("diagnostics")?.mapNotNull { it.asJsonObject["message"]?.asString }.orEmpty()
    throw IllegalStateException(messages.firstOrNull() ?: session.t("the program could not be built"))
}

/** The debugger's configuration of the program a launch job built (debug/configuration; mcpp takes its fast path). */
internal fun debugLaunch(c: McppRunConfiguration, engine: String): CompletableFuture<JsonObject> =
    McppSession.get(c.project).request("debug/configuration", launchParams(c, engine)).thenApply { it.asJsonObject.getAsJsonObject("launch") }

internal fun commandLine(l: JsonObject, pty: Boolean): GeneralCommandLine {
    val argv = l.getAsJsonArray("argv").map { it.asString }
    val cmd = if (pty) PtyCommandLine(argv) else GeneralCommandLine(argv)
    cmd.withWorkDirectory(l["cwd"].asString)
    l.getAsJsonObject("env")?.entrySet()?.forEach { (k, v) -> cmd.withEnvironment(k, v.asString) }
    cmd.withParentEnvironmentType(GeneralCommandLine.ParentEnvironmentType.CONSOLE)
    return cmd
}

/**
 * The program: built by a launch job, whose narration is this console's output as it is written,
 * then started in a PTY so it reads input and colours its output as in a terminal. The build happens
 * before the program's own process exists, so this handler stands for both.
 */
class LaunchingProcessHandler(private val c: McppRunConfiguration) : ProcessHandler(), AnsiEscapeDecoder.ColoredTextAcceptor {
    @Volatile private var inner: ProcessHandler? = null
    @Volatile private var cancelled = false
    @Volatile private var job: FollowedJob? = null
    private val decoder = AnsiEscapeDecoder()

    override fun coloredTextAvailable(text: String, attributes: Key<*>) = notifyTextAvailable(text, attributes)

    override fun startNotify() {
        super.startNotify()
        val session = McppSession.get(c.project)
        val started = System.nanoTime()
        val params = launchParams(c).apply { addProperty("kind", "launch"); addProperty("color", true) }
        val followed = FollowedJob(session, params, { text -> decoder.escapeText(text.replace("\r\n", "\n"), ProcessOutputTypes.SYSTEM, this) })
        job = followed
        followed.start().finished.whenComplete { finished, e ->
            val built = System.nanoTime()
            val l = runCatching { if (e != null) throw (e.cause ?: e) else launchOf(finished, session) }.getOrElse {
                notifyTextAvailable("${it.message}\n", ProcessOutputTypes.STDERR)
                notifyProcessTerminated(finished?.get("exitCode")?.takeIf { x -> !x.isJsonNull }?.asInt?.takeIf { x -> x != 0 } ?: 1)
                return@whenComplete
            }
            if (cancelled) { notifyProcessTerminated(130); return@whenComplete }
            val handler = runCatching { KillableColoredProcessHandler(commandLine(l, pty = true)) }.getOrElse {
                notifyTextAvailable("${it.message}\n", ProcessOutputTypes.STDERR)
                notifyProcessTerminated(1)
                return@whenComplete
            }
            handler.addProcessListener(object : ProcessListener {
                override fun onTextAvailable(event: ProcessEvent, outputType: Key<*>) = notifyTextAvailable(event.text, outputType)
                override fun processTerminated(event: ProcessEvent) = notifyProcessTerminated(event.exitCode)
            })
            inner = handler
            handler.startNotify()
            LOG.info("mcpp run: built in ${(built - started) / 1_000_000} ms, program started ${(System.nanoTime() - built) / 1_000_000} ms later")
        }
    }

    override fun destroyProcessImpl() {
        cancelled = true
        inner?.destroyProcess() ?: run { job?.cancel(); notifyProcessTerminated(130) }
    }

    override fun detachProcessImpl() {
        inner?.detachProcess()
        notifyProcessDetached()
    }

    override fun detachIsDefault(): Boolean = false
    override fun getProcessInput(): OutputStream? = inner?.processInput

    companion object {
        private val LOG = com.intellij.openapi.diagnostic.Logger.getInstance(LaunchingProcessHandler::class.java)
    }
}

class McppProgramState(private val c: McppRunConfiguration, env: ExecutionEnvironment) : CommandLineState(env) {
    override fun startProcess(): ProcessHandler = LaunchingProcessHandler(c)
}

/** The URL of a case: the file that declares it (else its program's) and its line (`file://<path>:<line>`, 1-based). */
internal fun caseUrl(session: McppSession, root: String, test: String, case: String, main: String?): String? {
    val cases = session.casesFor(root, test) ?: return null
    val row = cases.getAsJsonArray("cases")?.firstOrNull { it.asJsonObject["name"]?.asString == case }?.asJsonObject ?: return null
    val file = row["file"]?.asString?.takeIf { it.isNotEmpty() } ?: main?.takeIf { it.isNotEmpty() } ?: return null
    val line = row["line"]?.asInt ?: return null
    return "file://$file:${line + 1}"
}

