// The "mcpp" run configuration type: run, test or build a project, for a target and a profile. Run
// asks the backend how the program starts (it builds first) and runs it in a PTY console; build and
// test are backend jobs whose narration the console shows; a test run fills the test tree. Debug
// runs the program under CLion's own debugger (McppDebugRunner).
package io.github.mcppcommunity.mcppclion.run

import com.intellij.execution.Executor
import com.intellij.execution.configurations.ConfigurationFactory
import com.intellij.execution.configurations.ConfigurationTypeBase
import com.intellij.execution.configurations.RunConfiguration
import com.intellij.execution.configurations.RunConfigurationBase
import com.intellij.execution.configurations.RunConfigurationOptions
import com.intellij.execution.configurations.RunProfileState
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.openapi.components.BaseState
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.ComboBox
import com.intellij.ui.components.JBTextField
import com.intellij.util.ui.FormBuilder
import io.github.mcppcommunity.mcppclion.McppIcons
import javax.swing.JComponent

class McppRunConfigurationType : ConfigurationTypeBase(ID, "mcpp", "Run, test or build an mcpp project", McppIcons.Logo) {
    init {
        addFactory(Factory(this))
    }

    class Factory(type: McppRunConfigurationType) : ConfigurationFactory(type) {
        override fun getId(): String = ID
        override fun createTemplateConfiguration(project: Project): RunConfiguration = McppRunConfiguration(project, this, "mcpp")
        override fun getOptionsClass(): Class<out BaseState> = McppRunOptions::class.java
    }

    companion object {
        const val ID = "McppRunConfiguration"
    }
}

class McppRunOptions : RunConfigurationOptions() {
    private val commandProperty = string("run").provideDelegate(this, "command")
    private val targetProperty = string("").provideDelegate(this, "target")
    private val profileProperty = string("").provideDelegate(this, "profile")
    private val argsProperty = string("").provideDelegate(this, "args")
    private val filterProperty = string("").provideDelegate(this, "filter")
    private val binProperty = string("").provideDelegate(this, "bin")
    private val rootProperty = string("").provideDelegate(this, "root")
    private val frameworkProperty = string("").provideDelegate(this, "framework")
    private val casesProperty = string("").provideDelegate(this, "cases")

    var command: String get() = commandProperty.getValue(this) ?: "run"; set(v) = commandProperty.setValue(this, v)
    var target: String get() = targetProperty.getValue(this) ?: ""; set(v) = targetProperty.setValue(this, v)
    var profile: String get() = profileProperty.getValue(this) ?: ""; set(v) = profileProperty.setValue(this, v)
    var args: String get() = argsProperty.getValue(this) ?: ""; set(v) = argsProperty.setValue(this, v)
    var filter: String get() = filterProperty.getValue(this) ?: ""; set(v) = filterProperty.setValue(this, v)
    var bin: String get() = binProperty.getValue(this) ?: ""; set(v) = binProperty.setValue(this, v)
    var root: String get() = rootProperty.getValue(this) ?: ""; set(v) = rootProperty.setValue(this, v)
    /** The test framework whose cases `cases` names (design 2026-10-30 part 2 §7); empty: whole programs. */
    var framework: String get() = frameworkProperty.getValue(this) ?: ""; set(v) = frameworkProperty.setValue(this, v)
    /** The cases to run, one per line, by the framework's full names; empty: every case. */
    var cases: String get() = casesProperty.getValue(this) ?: ""; set(v) = casesProperty.setValue(this, v)
}

class McppRunConfiguration(project: Project, factory: ConfigurationFactory, name: String) :
    RunConfigurationBase<McppRunOptions>(project, factory, name) {

    public override fun getOptions(): McppRunOptions = super.getOptions() as McppRunOptions

    /** The project directory: the configuration's own, else the IDE project's. */
    val root: String get() = options.root.ifBlank { project.basePath ?: "" }

    /** The arguments the program (run) or the test binaries receive, split as a shell would not: on spaces. */
    val programArgs: List<String> get() = options.args.split(' ').filter { it.isNotBlank() }

    override fun getConfigurationEditor(): SettingsEditor<out RunConfiguration> = Editor()

    override fun getState(executor: Executor, environment: ExecutionEnvironment): RunProfileState = when (options.command) {
        "test" -> McppTestState(this, environment)
        "build" -> McppJobState(this, environment)
        else -> McppProgramState(this, environment)
    }

    class Editor : SettingsEditor<McppRunConfiguration>() {
        private val command = ComboBox(arrayOf("run", "test", "build"))
        private val target = JBTextField()
        private val profile = ComboBox(arrayOf("", "dev", "release"))
        private val args = JBTextField()
        private val filter = JBTextField()
        private val bin = JBTextField()
        private val framework = JBTextField()
        private val cases = com.intellij.ui.components.JBTextArea(3, 40)

        override fun resetEditorFrom(c: McppRunConfiguration) {
            command.selectedItem = c.options.command
            target.text = c.options.target
            profile.selectedItem = c.options.profile
            args.text = c.options.args
            filter.text = c.options.filter
            bin.text = c.options.bin
            framework.text = c.options.framework
            cases.text = c.options.cases
        }

        override fun applyEditorTo(c: McppRunConfiguration) {
            c.options.command = command.selectedItem as String
            c.options.target = target.text.trim()
            c.options.profile = profile.selectedItem as String
            c.options.args = args.text
            c.options.filter = filter.text.trim()
            c.options.bin = bin.text.trim()
            c.options.framework = framework.text.trim()
            c.options.cases = cases.text.lines().map { it.trimEnd() }.filter { it.isNotEmpty() }.joinToString("\n")
        }

        override fun createEditor(): JComponent = FormBuilder.createFormBuilder()
            .addLabeledComponent("Command:", command)
            .addLabeledComponent("Target (empty: the mcpp.build.target setting):", target)
            .addLabeledComponent("Profile (empty: the mcpp.build.profile setting):", profile)
            .addLabeledComponent("Executable (run):", bin)
            .addLabeledComponent("Test filter (test):", filter)
            .addLabeledComponent("Test framework (test cases):", framework)
            .addLabeledComponent("Cases, one per line (test cases):", cases)
            .addLabeledComponent("Arguments:", args)
            .panel
    }
}
