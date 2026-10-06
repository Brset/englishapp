plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "app.englishpron"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "app.englishpron"
        minSdk = 28
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DPRON_BUILD_TESTS=OFF", "-DANDROID_STL=c++_static")
                cppFlags += "-std=c++17"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    buildFeatures { compose = true }
    androidResources { noCompress += listOf("db", "sql", "dict") }
    packaging { jniLibs.useLegacyPackaging = false }
}

// --- Content assets: build/content.db (tools/build_content_db.py) + user_schema.sql -------------
val contentAssetsDir = layout.buildDirectory.dir("generated/contentAssets")
val copyContentAssets by tasks.registering(Copy::class) {
    val root = rootProject.projectDir.parentFile
    from(File(root, "build/content.db"))
    from(File(root, "content/db/user_schema.sql"))
    // optional dictionary / phoneme vocab, copied if present
    from(File(root, "build")) { include("cmudict.dict", "phoneme_vocab.json") }
    into(contentAssetsDir)
    doFirst {
        if (!File(root, "build/content.db").exists()) {
            throw GradleException("build/content.db not found: run `python tools/build_content_db.py` in the repo root first")
        }
    }
}
android.sourceSets["main"].assets.srcDir(contentAssetsDir)
tasks.configureEach {
    if (name.startsWith("merge") && name.endsWith("Assets")) dependsOn(copyContentAssets)
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.androidx.coroutines)
}
