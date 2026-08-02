package dev.pathguard.providerhook;

import java.lang.invoke.MethodType;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Arrays;
import java.util.Objects;

public final class ProviderHooker {
    private static final String TAG = "PathGuardLsplant";
    private final int methodId;
    private final boolean targetStatic;
    private volatile Method backup;

    public ProviderHooker(int methodId, Method target) {
        this.methodId = methodId;
        this.targetStatic = Modifier.isStatic(
                Objects.requireNonNull(target, "target").getModifiers());
    }

    public void setBackup(Method backup) {
        this.backup = Objects.requireNonNull(backup, "backup");
    }

    public Object callback(Object[] args) throws Throwable {
        Method original = backup;
        if (original == null) {
            throw new IllegalStateException("backup unavailable for method " + methodId);
        }
        if (!targetStatic && (args.length == 0 || args[0] == null)) {
            throw new IllegalStateException("instance receiver unavailable for method " + methodId);
        }
        Object receiver = targetStatic ? null : args[0];
        Object[] parameters = targetStatic ? args : Arrays.copyOfRange(args, 1, args.length);
        try {
            return original.invoke(receiver, parameters);
        } catch (InvocationTargetException error) {
            throw error.getCause();
        }
    }

    public static Method resolve(
            String className,
            String methodName,
            String descriptor,
            ClassLoader classLoader) throws ReflectiveOperationException {
        ClassLoader contextLoader = Thread.currentThread().getContextClassLoader();
        ClassLoader systemLoader = ClassLoader.getSystemClassLoader();
        ClassLoader hookerLoader = ProviderHooker.class.getClassLoader();
        ClassLoader[] candidates = {
            classLoader,
            contextLoader,
            systemLoader,
            hookerLoader,
            classLoader == null ? null : classLoader.getParent(),
            hookerLoader == null ? null : hookerLoader.getParent(),
        };
        ReflectiveOperationException last = null;
        for (int index = 0; index < candidates.length; ++index) {
            ClassLoader candidate = candidates[index];
            if (duplicateLoader(candidates, index, candidate)) {
                continue;
            }
            try {
                Class<?> owner = Class.forName(className, false, candidate);
                ClassLoader descriptorLoader = owner.getClassLoader();
                MethodType type = MethodType.fromMethodDescriptorString(
                        descriptor, descriptorLoader);
                Method method = owner.getDeclaredMethod(
                        methodName, type.parameterArray());
                if (method.getReturnType() != type.returnType()) {
                    throw new NoSuchMethodException(
                            className + "." + methodName + descriptor);
                }
                method.setAccessible(true);
                return method;
            } catch (ReflectiveOperationException error) {
                last = error;
                logResolveFailure(
                        className, methodName, descriptor, candidate, error);
            } catch (LinkageError | SecurityException error) {
                logResolveFailure(
                        className, methodName, descriptor, candidate, error);
            }
        }
        if (last != null) {
            throw last;
        }
        throw new ClassNotFoundException(
                "no ClassLoader resolved " + className + "." + methodName + descriptor);
    }

    private static boolean duplicateLoader(
            ClassLoader[] candidates, int index, ClassLoader candidate) {
        for (int previous = 0; previous < index; ++previous) {
            if (candidates[previous] == candidate) {
                return true;
            }
        }
        return false;
    }

    private static void logResolveFailure(
            String className,
            String methodName,
            String descriptor,
            ClassLoader loader,
            Throwable error) {
        String loaderName = loader == null
                ? "bootstrap"
                : loader.getClass().getName();
        String message = "resolve failed: " + className + "." + methodName
                + descriptor + " loader=" + loaderName;
        try {
            Class<?> log = Class.forName("android.util.Log");
            Method logError = log.getMethod(
                    "e", String.class, String.class, Throwable.class);
            logError.invoke(null, TAG, message, error);
        } catch (ReflectiveOperationException | LinkageError ignored) {
            System.err.println(TAG + ": " + message + ": " + error);
        }
    }
}
