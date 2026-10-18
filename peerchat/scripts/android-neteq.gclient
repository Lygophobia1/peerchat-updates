# Copy as .gclient into a NEW isolated Linux dependency directory.
# WebRTC itself must be synced at the explicit revision recorded below.
solutions = [{
  "name": "src",
  "url": "https://webrtc.googlesource.com/src.git",
  "deps_file": "DEPS",
  "managed": False,
  "custom_vars": {
    "checkout_android": True,
    "checkout_ios": False,
  },
}]
target_os = ["android"]
# Required source revision: 6cf6bcb585b31414aac02377df2bc8fec770d0e8
