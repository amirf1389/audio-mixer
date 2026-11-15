# AmF X1 SDK: the JNI entry points are looked up by name from native code, keep them (and their classes) when the host app shrinks.
-keep class com.amfb.amixos.Engine { *; }
-keep class com.amfb.amixos.AudioLoop { *; }
-keep class com.amfb.amixos.AudioHal { *; }
-keep class com.amfb.amixos.BrandProfile { *; }
-keepclasseswithmembernames class * { native <methods>; }
