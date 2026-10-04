// Explicitly prepared, isolated upstream HPKE patch. Never replace signed stock
// BC/TLS jars. Missing/damaged inputs fail closed; normal builds never download.
val hookHash = "4939210CDE012EFC343C72351B82A37F1C51E301A7BA5DDDAF2FF39BF329F62C"
val hook = file("../../build/deps/bc-hpke-eacda831fbe15b272933e40841d8b0827b556fae/artifacts/$hookHash/peerchat-bc-hpke-1.86-external-dh-v1.jar")
require(hook.isFile) { "Prepare the pinned HPKE artifact with scripts/build-bc-hpke-hook.ps1 first" }
val actual = java.security.MessageDigest.getInstance("SHA-256").digest(hook.readBytes())
    .joinToString("") { "%02X".format(it.toInt() and 255) }
require(actual == hookHash) { "Pinned HPKE artifact hash mismatch" }
dependencies.add("implementation", files(hook))
