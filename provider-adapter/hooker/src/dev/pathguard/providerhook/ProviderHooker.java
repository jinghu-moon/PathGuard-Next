package dev.pathguard.providerhook;

import java.lang.invoke.MethodType;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Arrays;
import java.util.Objects;

public final class ProviderHooker {
    private static final String TAG = "PathGuardLsplant";
    private static volatile Dispatcher dispatcher;
    private final int methodId;
    private final boolean targetStatic;
    private final Class<?> returnType;
    private volatile Method backup;

    public interface Dispatcher {
        DispatchResult dispatch(int methodId, Object[] args) throws Throwable;

        default DispatchResult afterDispatch(
                int methodId, Object[] args, Object originalResult) throws Throwable {
            return DispatchResult.pass();
        }
    }

    public static final class DispatchResult {
        private static final DispatchResult PASS = new DispatchResult(false, null);
        private final boolean rewrite;
        private final Object value;

        private DispatchResult(boolean rewrite, Object value) {
            this.rewrite = rewrite;
            this.value = value;
        }

        public static DispatchResult pass() {
            return PASS;
        }

        public static DispatchResult rewrite(Object value) {
            return new DispatchResult(true, value);
        }
    }

    public static void setDispatcher(Dispatcher value) {
        dispatcher = value;
    }

    public static void clearDispatcher() {
        dispatcher = null;
    }

    public static void installNativeDispatcher() {
        dispatcher = new Dispatcher() {
            @Override
            public DispatchResult dispatch(int methodId, Object[] args) {
                return normalizeNativeResult(nativeDispatch(methodId, args));
            }

            @Override
            public DispatchResult afterDispatch(
                    int methodId, Object[] args, Object originalResult) {
                return normalizeNativeResult(
                        nativeAfterDispatch(methodId, args, originalResult));
            }
        };
    }

    private static native Object nativeDispatch(int methodId, Object[] args);
    private static native Object nativeAfterDispatch(
            int methodId, Object[] args, Object originalResult);

    private static DispatchResult normalizeNativeResult(Object nativeResult) {
        return nativeResult instanceof DispatchResult
                ? (DispatchResult) nativeResult
                : DispatchResult.pass();
    }

    public ProviderHooker(int methodId, Method target) {
        this.methodId = methodId;
        Method checkedTarget = Objects.requireNonNull(target, "target");
        this.targetStatic = Modifier.isStatic(checkedTarget.getModifiers());
        this.returnType = checkedTarget.getReturnType();
    }

    public void setBackup(Method backup) {
        this.backup = Objects.requireNonNull(backup, "backup");
    }

    public Object callback(Object[] args) throws Throwable {
        if (args == null) {
            throw new IllegalArgumentException("callback arguments unavailable for method "
                    + methodId);
        }
        Method original = backup;
        if (original == null) {
            throw new IllegalStateException("backup unavailable for method " + methodId);
        }
        if (!targetStatic && (args.length == 0 || args[0] == null)) {
            throw new IllegalStateException("instance receiver unavailable for method " + methodId);
        }
        Object receiver = targetStatic ? null : args[0];
        Object[] parameters = targetStatic ? args : Arrays.copyOfRange(args, 1, args.length);
        Dispatcher current = dispatcher;
        Object[] dispatchArguments = null;
        if (current != null) {
            dispatchArguments = Arrays.copyOf(args, args.length);
            try {
                DispatchResult decision = current.dispatch(
                        methodId, dispatchArguments);
                if (decision != null && decision.rewrite
                        && isCompatibleReturn(decision.value)) {
                    return decision.value;
                }
            } catch (Throwable error) {
                logDispatchFailure(error);
                current = null;
            }
        }
        Object originalResult;
        try {
            originalResult = original.invoke(receiver, parameters);
        } catch (InvocationTargetException error) {
            throw error.getCause();
        }
        if (current != null) {
            try {
                DispatchResult decision = current.afterDispatch(
                        methodId, dispatchArguments, originalResult);
                if (decision != null && decision.rewrite
                        && isCompatibleReturn(decision.value)) {
                    return decision.value;
                }
            } catch (Throwable error) {
                logDispatchFailure(error);
            }
        }
        return originalResult;
    }

    private boolean isCompatibleReturn(Object value) {
        if (!returnType.isPrimitive()) {
            return value == null || returnType.isInstance(value);
        }
        if (value == null) return false;
        if (returnType == boolean.class) return value instanceof Boolean;
        if (returnType == byte.class) return value instanceof Byte;
        if (returnType == char.class) return value instanceof Character;
        if (returnType == short.class) return value instanceof Short;
        if (returnType == int.class) return value instanceof Integer;
        if (returnType == long.class) return value instanceof Long;
        if (returnType == float.class) return value instanceof Float;
        if (returnType == double.class) return value instanceof Double;
        return false;
    }

    private void logDispatchFailure(Throwable error) {
        String message = "dispatcher failed for method " + methodId;
        try {
            Class<?> log = Class.forName("android.util.Log");
            Method logError = log.getMethod(
                    "e", String.class, String.class, Throwable.class);
            logError.invoke(null, TAG, message, error);
        } catch (ReflectiveOperationException | LinkageError ignored) {
            System.err.println(TAG + ": " + message + ": " + error);
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
