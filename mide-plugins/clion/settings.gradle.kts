// The Kotlin compiler needs the JDK build.gradle.kts asks for, not whichever one Gradle runs on:
// `xim:gradle` brings a newer JDK than the IntelliJ platform's. The resolver makes the toolchain
// declaration mean what it says.
plugins {
    id("org.gradle.toolchains.foojay-resolver-convention") version "1.0.0"
}

rootProject.name = "mcpp-clion"
