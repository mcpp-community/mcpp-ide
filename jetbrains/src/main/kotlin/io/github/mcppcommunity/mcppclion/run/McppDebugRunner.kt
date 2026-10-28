// Debugging an "mcpp" run configuration under CLion's own debugger (GDB or LLDB, as the default
// toolchain names it). The backend builds the program and says how it starts; the debugger starts
// exactly that. A program that runs through a runner (an ok image under okrun) is debugged as the
// runner: okrun registers each image through the GDB JIT interface, which GDB reads by default.
package io.github.mcppcommunity.mcppclion.run

import com.intellij.execution.ExecutionException
import com.intellij.execution.configurations.RunProfile
import com.intellij.execution.configurations.RunProfileState
import com.intellij.execution.configurations.RunnerSettings
import com.intellij.execution.executors.DefaultDebugExecutor
import com.intellij.execution.filters.TextConsoleBuilderFactory
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.execution.runners.GenericProgramRunner
import com.intellij.execution.ui.RunContentDescriptor
import com.intellij.notification.NotificationType
import com.intellij.openapi.progress.ProgressManager
import com.intellij.xdebugger.XDebugProcess
import com.intellij.xdebugger.XDebugProcessStarter
import com.intellij.xdebugger.XDebugSession
import com.intellij.xdebugger.XDebuggerManager
import com.jetbrains.cidr.cpp.execution.debugger.backend.CLionGDBDriverConfiguration
import com.jetbrains.cidr.cpp.execution.debugger.backend.CLionLLDBDriverConfiguration
import com.jetbrains.cidr.cpp.toolchains.CPPToolchains
import com.jetbrains.cidr.execution.TrivialRunParameters
import com.jetbrains.cidr.execution.debugger.CidrLocalDebugProcess
import io.github.mcppcommunity.mcppclion.McppSession

class McppDebugRunner : GenericProgramRunner<RunnerSettings>() {
    override fun getRunnerId(): String = "McppDebugRunner"

    override fun canRun(executorId: String, profile: RunProfile): Boolean =
        executorId == DefaultDebugExecutor.EXECUTOR_ID && profile is McppRunConfiguration && profile.options.command == "run"

    override fun doExecute(state: RunProfileState, env: ExecutionEnvironment): RunContentDescriptor? {
        val c = env.runProfile as McppRunConfiguration
        val project = c.project
        val session = McppSession.get(project)
        val toolchain = CPPToolchains.getInstance().defaultToolchain
            ?: throw ExecutionException(session.t("No toolchain is set up in Settings | Build, Execution, Deployment | Toolchains; CLion's debugger comes from one."))
        val launch = ProgressManager.getInstance().runProcessWithProgressSynchronously<com.google.gson.JsonObject, Exception>(
            { launch(c).get() }, session.t("mcpp: building for the debugger…"), true, project)
        val lldb = toolchain.debuggerKind.isLLDB()
        if (!launch["via"].isJsonNull && lldb)
            session.notify(session.t("This program runs through {0}. LLDB reads its symbols once `settings set plugin.jit-loader.gdb.enable on` is in ~/.lldbinit; GDB reads them as it is.", launch["via"].asString), NotificationType.INFORMATION)
        val driver = if (lldb) CLionLLDBDriverConfiguration(project, toolchain) else CLionGDBDriverConfiguration(project, toolchain)
        val parameters = TrivialRunParameters(driver, commandLine(launch, pty = false))
        return XDebuggerManager.getInstance(project).startSession(env, object : XDebugProcessStarter() {
            override fun start(xs: XDebugSession): XDebugProcess =
                CidrLocalDebugProcess(parameters, xs, TextConsoleBuilderFactory.getInstance().createBuilder(project))
        }).runContentDescriptor
    }
}
