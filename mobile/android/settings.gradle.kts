// OS999 for Android folding phones. Open this folder in Android Studio, or:
//   sh ../scripts/fetch-deps.sh  (from the repository root, once)
//   ./gradlew :app:assembleDebug
pluginManagement {
    repositories { google(); mavenCentral(); gradlePluginPortal() }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories { google(); mavenCentral() }
}
rootProject.name = "OS999"
include(":app")
