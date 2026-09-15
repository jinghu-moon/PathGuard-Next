plugins {
    id("com.android.application")
}

android {
    namespace = "dev.pathguard.hideprobe"
    compileSdk = 36

    defaultConfig {
        applicationId = "dev.pathguard.hideprobe"
        minSdk = 31
        targetSdk = 36
        versionCode = 2
        versionName = "0.2-hidelab"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    flavorDimensions += "observer"
    productFlavors {
        create("target") {
            dimension = "observer"
            applicationIdSuffix = ".target"
            versionNameSuffix = "-target"
            buildConfigField("String", "OBSERVER_ROLE", "\"target\"")
        }
        create("control") {
            dimension = "observer"
            applicationIdSuffix = ".control"
            versionNameSuffix = "-control"
            buildConfigField("String", "OBSERVER_ROLE", "\"control\"")
        }
    }

    buildFeatures {
        buildConfig = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }

    buildTypes {
        getByName("debug") {
            isDebuggable = true
        }
        getByName("release") {
            isMinifyEnabled = false
        }
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
    }
}

val nativeOutput = rootProject.file("../../../../native/obj/local")
val generatedJniLibs = layout.projectDirectory.dir("src/main/jniLibs")

val stageNativeProbe = tasks.register<Copy>("stageNativeProbe") {
    from(nativeOutput) {
        include("**/libpathguard_hide_app_probe.so")
    }
    into(generatedJniLibs)
    doLast {
        check(fileTree(generatedJniLibs).matching {
            include("**/libpathguard_hide_app_probe.so")
        }.files.isNotEmpty()) {
            "Build pathguard_hide_app_probe with scripts/build-native.ps1 first"
        }
    }
}

tasks.named("preBuild") {
    dependsOn(stageNativeProbe)
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}
