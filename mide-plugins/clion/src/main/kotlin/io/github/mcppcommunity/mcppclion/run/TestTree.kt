// The test tree of a test job (design 2026-11-10): the TeamCity service messages of an ID-based tree
// that the IDE's test runner reads, made from the backend's notifications. Kept apart from the process
// handler that sends them, so that it is tested without an IDE.
package io.github.mcppcommunity.mcppclion.run

import com.google.gson.JsonObject

class TestTree(
    private val places: Map<String, String>,
    private val t: (String) -> String,
    private val locate: (root: String, program: String, case: String, file: String?) -> String?,
) {
    private fun escape(s: String) = s.replace("|", "||").replace("'", "|'").replace("\n", "|n").replace("\r", "|r").replace("[", "|[").replace("]", "|]")

    /** `path:line` of a result's `location` (its line 0-based), or "". */
    private fun place(p: JsonObject): String = runCatching {
        val l = p.getAsJsonObject("location")
        "${l["path"].asString}:${l["line"].asInt + 1}"
    }.getOrDefault("")

    // The suites opened, in order (closed at the end, innermost first), and the tests already reported.
    private val suites = mutableListOf<String>()
    private val started = mutableSetOf<String>()
    private val finished = mutableSetOf<String>()

    private val out = StringBuilder()
    private fun send(text: String) { out.append(text) }

    private fun hint(file: String?) = file?.takeIf { it.isNotEmpty() }?.let { " locationHint='file://${escape(it)}'" } ?: ""

    /** The suite of a program, and of the part of it a record or a case belongs to; opened once. */
    private fun parentOf(program: String, part: String?): String {
        val programNode = "p:$program"
        if (programNode !in suites) {
            send("##teamcity[testSuiteStarted nodeId='${escape(programNode)}' parentNodeId='0' name='${escape(program)}'${hint(places[program])}]\n")
            suites += programNode
        }
        if (part.isNullOrEmpty() || part == program) return programNode
        val partNode = "f:$part"
        if (partNode !in suites) {
            val name = part.removePrefix("$program/")
            send("##teamcity[testSuiteStarted nodeId='${escape(partNode)}' parentNodeId='${escape(programNode)}' name='${escape(name)}'${hint(places[part])}]\n")
            suites += partNode
        }
        return partNode
    }

    private fun start(node: String, parent: String, name: String, hint: String) {
        if (!started.add(node)) return
        send("##teamcity[testStarted nodeId='${escape(node)}' parentNodeId='${escape(parent)}' name='${escape(name)}'$hint]\n")
    }

    /** A number as a person writes it: `15`, `4.2`. */
    private fun number(d: Double): String = if (d % 1.0 == 0.0 && kotlin.math.abs(d) < 1e15) d.toLong().toString() else d.toString()

    /** A metric against its bound: `ready 4.2 s ≤ 15 s`. */
    private fun metrics(p: JsonObject): String {
        val bounds = p.getAsJsonArray("bounds")?.mapNotNull { it.takeIf { b -> b.isJsonObject }?.asJsonObject } ?: emptyList()
        return (p.getAsJsonArray("metrics") ?: return "").mapNotNull { m ->
            val o = m.takeIf { it.isJsonObject }?.asJsonObject ?: return@mapNotNull null
            val name = o["name"]?.asString ?: return@mapNotNull null
            val unit = o["unit"]?.asString?.takeIf { it != "1" }?.let { " $it" } ?: ""
            val b = bounds.firstOrNull { it["metric"]?.asString == name }
            val limit = b?.get("max")?.let { " ≤ ${number(it.asDouble)}$unit" } ?: b?.get("min")?.let { " ≥ ${number(it.asDouble)}$unit" } ?: ""
            "$name ${o["value"]?.asDouble?.let(::number) ?: ""}$unit$limit"
        }.joinToString("\n")
    }

    /** A case as it ended: its output, its verdict, its duration. */
    private fun outcome(node: String, p: JsonObject, output: String) {
        if (!finished.add(node)) return
        val text = listOf(output, metrics(p)).filter { it.isNotBlank() }.joinToString("\n")
        if (text.isNotEmpty()) send("##teamcity[testStdOut nodeId='${escape(node)}' out='${escape(text + "\n")}']\n")
        when (p["status"]?.asString) {
            "passed" -> {}
            "skipped" -> send("##teamcity[testIgnored nodeId='${escape(node)}' message='${escape(p["reason"]?.asString ?: "")}']\n")
            else -> {
                val failure = p.getAsJsonArray("failures")?.firstOrNull()?.takeIf { it.isJsonObject }?.asJsonObject
                val message = when {
                    p["unexpectedPass"]?.asBoolean == true -> t("The case passed, though it is expected to fail.")
                    else -> failure?.get("message")?.asString ?: p["message"]?.asString ?: ""
                }
                val details = listOf(place(p), p["message"]?.asString ?: "").filter { it.isNotBlank() }.joinToString("\n")
                val sides = if (failure != null && failure.has("expected") && failure.has("actual"))
                    " type='comparisonFailure' expected='${escape(failure["expected"].asString)}' actual='${escape(failure["actual"].asString)}'" else ""
                send("##teamcity[testFailed nodeId='${escape(node)}' message='${escape(message)}' details='${escape(details)}'$sides]\n")
            }
        }
        send("##teamcity[testFinished nodeId='${escape(node)}' duration='${p["durationMs"]?.asLong ?: 0}']\n")
    }

    /** A case of a program, under its part's suite, at its line. */
    private fun case(program: String, part: String?, p: JsonObject, output: String?) {
        val name = p["case"]?.asString ?: return
        val node = "c:$program\u001f$name"
        val parent = parentOf(program, part)
        val url = locate(p["root"]?.asString ?: "", program, name, places[part ?: program] ?: places[program])
        start(node, parent, name, url?.let { " locationHint='${escape(it)}'" } ?: "")
        if (output != null) outcome(node, p, output)
    }

    /** The service messages a test job's notification becomes. */
    fun messages(method: String, p: JsonObject): String {
        when (method) {
            // A case as the program writes it (test-case-stream, mcpp#917).
            "test/caseEvent" -> {
                val program = p["program"]?.asString ?: return ""
                case(program, p["part"]?.asString, p, if (p["event"]?.asString == "end") "" else null)
            }
            // A case of a test/runCases job, from its program's records.
            "test/caseResult" -> {
                val program = p["program"]?.asString ?: p["test"].asString
                case(program, p["part"]?.asString, p, p["output"]?.takeIf { it.isJsonPrimitive }?.asString ?: "")
            }
            "test/result" -> {
                val program = p["program"]?.asString ?: p["test"].asString
                val part = p["part"]?.asString
                val cases = p.getAsJsonArray("cases")?.mapNotNull { it.takeIf { c -> c.isJsonObject }?.asJsonObject } ?: emptyList()
                if (cases.isEmpty()) result(program, part, p)
                for (c in cases) case(program, part, c.deepCopy().apply { add("root", p["root"]) }, if (c["status"]?.asString == "failed") c["message"]?.asString ?: "" else "")
            }
        }
        return drain()
    }

    /** The job ended: every suite opened closes, innermost first. */
    fun close(): String {
        for (node in suites.asReversed()) send("##teamcity[testSuiteFinished nodeId='${escape(node)}']\n")
        suites.clear()
        return drain()
    }

    private fun drain(): String = out.toString().also { out.setLength(0) }

    /** A program, or a part of one, without cases: one test, judged by mcpp's status. */
    private fun result(program: String, part: String?, p: JsonObject) {
        val name = part ?: program
        val node = "t:$name"
        val parent = if (part != null) parentOf(program, null) else "0"
        start(node, parent, if (part != null) part.removePrefix("$program/") else program, hint(places[name]))
        val output = p["output"]?.takeIf { it.isJsonPrimitive }?.asString ?: ""
        val note = if (p["outputTruncated"]?.asBoolean == true) "\n(the output was longer; its head and tail are shown)\n" else ""
        if (output.isNotEmpty()) send("##teamcity[testStdOut nodeId='${escape(node)}' out='${escape(output + note)}']\n")
        val reason = p["reason"]?.asString?.ifBlank { null }
        when (p["status"]?.asString) {
            "passed" -> {}
            "skipped" -> send("##teamcity[testIgnored nodeId='${escape(node)}'${reason?.let { " message='${escape(it)}'" } ?: ""}]\n")
            else -> {
                val message = when {
                    p["timedOut"]?.asBoolean == true -> "The test timed out"
                    p["compileFailed"]?.asBoolean == true -> "The test did not compile"
                    else -> reason ?: "The test failed (${p["status"].asString})"
                }
                send("##teamcity[testFailed nodeId='${escape(node)}' message='${escape(message)}' details='${escape(place(p))}']\n")
            }
        }
        finished += node
        send("##teamcity[testFinished nodeId='${escape(node)}' duration='${p["durationMs"]?.asLong ?: 0}']\n")
    }
}

