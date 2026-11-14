# JniBindings.cpp looks this class up by name and registers its native
# methods by name and signature in JNI_OnLoad.
-keep class com.locationconfidence.core.NativeCore {
    native <methods>;
}
