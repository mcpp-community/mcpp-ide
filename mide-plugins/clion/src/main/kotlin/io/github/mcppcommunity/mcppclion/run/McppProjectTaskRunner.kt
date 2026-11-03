// The IDE's Build (the hammer, Build | Build Project) of an mcpp project is `mcpp build` (mcpp design
// 2026-10-30 part 2 §6.1): the platform asks each task runner whether it builds the project's
// modules, and this one answers for a project whose directory holds an mcpp.toml. The build is a
// backend job, as every build of this plugin is; its narration goes to the mcpp notifications and
// its end to the platform's result.
package io.github.mcppcommunity.mcppclion.run

import com.google.gson.JsonObject
import com.intellij.notification.NotificationType
import com.intellij.openapi.project.Project
import com.intellij.task.ModuleBuildTask
import com.intellij.task.ProjectTask
import com.intellij.task.ProjectTaskContext
import com.intellij.task.ProjectTaskRunner
import com.intellij.task.TaskRunnerResults
import io.github.mcppcommunity.mcppclion.McppSession
import org.jetbrains.concurrency.AsyncPromise
import org.jetbrains.concurrency.Promise
import java.io.File

class McppProjectTaskRunner : ProjectTaskRunner() {
    override fun canRun(project: Project, task: ProjectTask): Boolean =
        task is ModuleBuildTask && project.basePath?.let { File(it, "mcpp.toml").isFile } == true

    // Abstract in 2026.1, which asks it of a task alone: the module's project answers.
    @Suppress("OVERRIDE_DEPRECATION")
    override fun canRun(task: ProjectTask): Boolean = (task as? ModuleBuildTask)?.module?.project?.let { canRun(it, task) } ?: false

    override fun run(project: Project, context: ProjectTaskContext, vararg tasks: ProjectTask): Promise<ProjectTaskRunner.Result> {
        val promise = AsyncPromise<ProjectTaskRunner.Result>()
        val session = McppSession.get(project)
        val root = session.root ?: project.basePath
        if (root == null) {
            promise.setResult(TaskRunnerResults.FAILURE)
            return promise
        }
        // The job's end can arrive before the reply that names its id: ends are kept until it is known.
        val lock = Any()
        var jobId: String? = null
        val early = mutableListOf<JsonObject>()
        var unsubscribe: (() -> Unit)? = null
        fun finish(o: JsonObject) {
            unsubscribe?.invoke()
            val status = o["status"]?.asString
            if (status != "ok") session.notify(session.t("mcpp {0}: {1}", "build", status ?: "failed"), NotificationType.ERROR)
            promise.setResult(when (status) {
                "ok" -> TaskRunnerResults.SUCCESS
                "cancelled", "superseded" -> TaskRunnerResults.ABORTED
                else -> TaskRunnerResults.FAILURE
            })
        }
        unsubscribe = session.on("job/finished") { p ->
            val o = p.asJsonObject
            val mine = synchronized(lock) {
                if (jobId == null) { early += o; false } else o["jobId"]?.asString == jobId
            }
            if (mine) finish(o)
        }
        session.request("job/start", JsonObject().apply {
            addProperty("kind", "build")
            addProperty("root", root)
            addProperty("color", false)
        }).whenComplete { r, e ->
            if (e != null) {
                unsubscribe?.invoke()
                session.notify(session.t("mcpp: {0}", (e.cause ?: e).message ?: ""), NotificationType.ERROR)
                promise.setResult(TaskRunnerResults.FAILURE)
                return@whenComplete
            }
            val id = r.asJsonObject["jobId"].asString
            val ended = synchronized(lock) { jobId = id; early.firstOrNull { it["jobId"]?.asString == id }.also { early.clear() } }
            if (ended != null) finish(ended)
        }
        return promise
    }
}
