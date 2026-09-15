plugins {
    id("com.android.application") version "9.3.1" apply false
}

subprojects {
    tasks.configureEach {
        if (name == "preBuild") {
            dependsOn(rootProject.tasks.named("verifyHideLabToolchain"))
        }
    }
}

tasks.register("verifyHideLabToolchain") {
    doLast {
        val requiredJdk = 21
        check(JavaVersion.current().majorVersion == requiredJdk.toString()) {
            "HideLab requires JDK $requiredJdk, got ${JavaVersion.current().majorVersion}"
        }
        check(gradle.gradleVersion == "9.5.0") {
            "HideLab requires Gradle 9.5.0, got ${gradle.gradleVersion}"
        }
    }
}
