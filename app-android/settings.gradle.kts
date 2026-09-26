pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    // Mudamos de FAIL_ON_PROJECT_REPOS para PREFER_SETTINGS para evitar o bug de mutação
    repositoriesMode.set(RepositoriesMode.PREFER_SETTINGS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "SLACodec"
include(":app")
