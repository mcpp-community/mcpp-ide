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
        session.request(method, params).whenComplete { r, e ->
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
 * A test job whose results fill the IDE's test tree: each `test/result` becomes the TeamCity
 * service messages the test runner reads, located at the file that holds the test's `main`.
 */
class TestJobProcessHandler(session: McppSession, params: JsonObject, private val mains: Map<String, String>, method: String = "job/start") :
    JobProcessHandler(session, params, method) {
    private fun escape(s: String) = s.replace("|", "||").replace("'", "|'").replace("\n", "|n").replace("\r", "|r").replace("[", "|[").replace("]", "|]")
    /** The program whose cases are reported, once its first case is (test/runCases). */
    private var suite: String? = null

    override fun handle(method: String, p: JsonObject) {
        when (method) {
            // A case of a framework (§7.4): the program is a suite, each case a test in it.
            "test/caseResult" -> {
                val program = p["test"].asString
                val out = StringBuilder()
                if (suite == null) {
                    suite = program
                    val hint = mains[program]?.let { " locationHint='file://${escape(it)}'" } ?: ""
                    out.append("##teamcity[testSuiteStarted name='${escape(program)}'$hint]\n")
                }
                val name = escape(p["case"].asString)
                // The case's place in its source, for the editor's ▶ to show the last outcome (TestStateStorage).
                val hint = caseUrl(session, p["root"]?.asString ?: "", program, p["case"].asString, mains[program])
                    ?.let { " locationHint='${escape(it)}'" } ?: ""
                out.append("##teamcity[testStarted name='$name'$hint]\n")
                when (p["status"]?.asString) {
                    "passed" -> {}
                    "skipped" -> out.append("##teamcity[testIgnored name='$name']\n")
                    else -> out.append("##teamcity[testFailed name='$name' message='${escape(p["message"]?.asString ?: "")}']\n")
                }
                out.append("##teamcity[testFinished name='$name' duration='${p["durationMs"]?.asLong ?: 0}']\n")
                notifyTextAvailable(out.toString(), ProcessOutputTypes.STDOUT)
            }
            "job/finished" -> {
                suite?.let { notifyTextAvailable("##teamcity[testSuiteFinished name='${escape(it)}']\n", ProcessOutputTypes.STDOUT) }
                super.handle(method, p)
            }
            "test/result" -> {
                val name = escape(p["test"].asString)
                val hint = mains[p["test"].asString]?.let { " locationHint='file://${escape(it)}'" } ?: ""
                val out = StringBuilder("##teamcity[testStarted name='$name'$hint]\n")
                val output = p["output"]?.asString ?: ""
                if (output.isNotEmpty()) out.append("##teamcity[testStdOut name='$name' out='${escape(output)}']\n")
                when (p["status"]?.asString) {
                    "passed" -> {}
                    "skipped" -> out.append("##teamcity[testIgnored name='$name']\n")
                    else -> out.append("##teamcity[testFailed name='$name' message='${escape(p["reason"]?.asString?.ifBlank { null } ?: p["status"].asString)}']\n")
                }
                out.append("##teamcity[testFinished name='$name' duration='${p["durationMs"]?.asLong ?: 0}']\n")
                notifyTextAvailable(out.toString(), ProcessOutputTypes.STDOUT)
            }
            else -> super.handle(method, p)
        }
    }
}

class McppTestState(private val c: McppRunConfiguration, env: ExecutionEnvironment) : CommandLineState(env) {
    override fun startProcess(): ProcessHandler {
        val session = McppSession.get(c.project)
        val mains = runCatching {
            session.request("test/list", JsonObject().apply { addProperty("root", c.root) }).get()
                .asJsonObject.getAsJsonArray("tests").associate { it.asJsonObject["test"].asString to it.asJsonObject["main"].asString }
        }.getOrDefault(emptyMap())
        // A framework's cases (test/runCases): the program receives the framework's filter.
        if (c.options.framework.isNotBlank() && c.options.filter.isNotBlank())
            return TestJobProcessHandler(session, jobParams(c, "test").apply {
                remove("kind")
                addProperty("test", c.options.filter)
                addProperty("framework", c.options.framework)
                add("cases", JsonArray().apply { c.options.cases.lines().filter { it.isNotBlank() }.forEach { add(it) } })
            }, mains, "test/runCases")
        return TestJobProcessHandler(session, jobParams(c, "test").apply {
            if (c.options.filter.isNotBlank()) addProperty("filter", c.options.filter)
            if (c.programArgs.isNotEmpty()) add("args", JsonArray().apply { add("--"); c.programArgs.forEach { add(it) } })
        }, mains)
    }

    override fun execute(executor: Executor, runner: ProgramRunner<*>): ExecutionResult {
        val handler = startProcess()
        val properties = SMTRunnerConsoleProperties(c, "mcpp", executor)
        val console = SMTestRunnerConnectionUtil.createAndAttachConsole("mcpp", handler, properties)
        return DefaultExecutionResult(console, handler)
    }
}

/** The backend's answer to "how does the program start" for a configuration (mcpp run --print-launch). */
internal fun launch(c: McppRunConfiguration): CompletableFuture<JsonObject> {
    val session = McppSession.get(c.project)
    val params = JsonObject().apply {
        addProperty("root", c.root)
        if (c.options.target.isNotBlank()) addProperty("target", c.options.target)
        if (c.options.profile.isNotBlank()) addProperty("profile", c.options.profile)
        if (c.options.bin.isNotBlank()) addProperty("bin", c.options.bin)
        add("args", JsonArray().apply { c.programArgs.forEach { add(it) } })
    }
    return session.request("run/launch", params).thenApply { it.asJsonObject }
}

internal fun commandLine(l: JsonObject, pty: Boolean): GeneralCommandLine {
    val argv = l.getAsJsonArray("argv").map { it.asString }
    val cmd = if (pty) PtyCommandLine(argv) else GeneralCommandLine(argv)
    cmd.withWorkDirectory(l["cwd"].asString)
    l.getAsJsonObject("env")?.entrySet()?.forEach { (k, v) -> cmd.withEnvironment(k, v.asString) }
    cmd.withParentEnvironmentType(GeneralCommandLine.ParentEnvironmentType.CONSOLE)
    return cmd
}

/**
 * The program: built and described by the backend, then started in a PTY so it reads input and
 * colours its output as in a terminal. The build happens before the program's own process exists,
 * so this handler stands for both.
 */
class LaunchingProcessHandler(private val c: McppRunConfiguration) : ProcessHandler() {
    @Volatile private var inner: ProcessHandler? = null
    @Volatile private var cancelled = false

    override fun startNotify() {
        super.startNotify()
        val session = McppSession.get(c.project)
        notifyTextAvailable("${session.t("mcpp: building…")}\n", ProcessOutputTypes.SYSTEM)
        launch(c).whenComplete { l, e ->
            if (e != null) {
                val cause = e.cause ?: e
                val narration = (cause as? io.github.mcppcommunity.mcppclion.backend.BackendError)?.data?.takeIf { it.isJsonObject }
                    ?.asJsonObject?.get("narration")?.asString
                if (narration != null) notifyTextAvailable(narration, ProcessOutputTypes.STDERR)
                notifyTextAvailable("${cause.message}\n", ProcessOutputTypes.STDERR)
                notifyProcessTerminated(1)
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
        }
    }

    override fun destroyProcessImpl() {
        cancelled = true
        inner?.destroyProcess() ?: notifyProcessTerminated(130)
    }

    override fun detachProcessImpl() {
        inner?.detachProcess()
        notifyProcessDetached()
    }

    override fun detachIsDefault(): Boolean = false
    override fun getProcessInput(): OutputStream? = inner?.processInput
}

class McppProgramState(private val c: McppRunConfiguration, env: ExecutionEnvironment) : CommandLineState(env) {
    override fun startProcess(): ProcessHandler = LaunchingProcessHandler(c)
}

/** The URL of a case: its program's source and the line of its macro (`file://<path>:<line>`, 1-based). */
internal fun caseUrl(session: McppSession, root: String, test: String, case: String, main: String?): String? {
    val file = main?.takeIf { it.isNotEmpty() } ?: return null
    val cases = session.casesFor(root, test) ?: return null
    val line = cases.getAsJsonArray("cases")?.firstOrNull { it.asJsonObject["name"]?.asString == case }
        ?.asJsonObject?.get("line")?.asInt ?: return null
    return "file://$file:${line + 1}"
}

