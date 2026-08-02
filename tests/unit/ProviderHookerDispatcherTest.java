import dev.pathguard.providerhook.ProviderHooker;

import java.lang.reflect.Method;

public final class ProviderHookerDispatcherTest {
    public static final class Target {
        public static String echo(String value) {
            return "original:" + value;
        }

        public static int number(int value) {
            return value + 1;
        }

        public static String nullable() {
            return null;
        }

        public static void empty() {
        }
    }

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    public static void main(String[] args) throws Throwable {
        Method echo = Target.class.getDeclaredMethod("echo", String.class);
        ProviderHooker hooker = new ProviderHooker(1, echo);
        hooker.setBackup(echo);
        check("original:x".equals(hooker.callback(new Object[]{"x"})),
                "default dispatcher must pass through");

        ProviderHooker.setDispatcher((methodId, callbackArgs) ->
                ProviderHooker.DispatchResult.rewrite("rewritten:" + callbackArgs[0]));
        check("rewritten:x".equals(hooker.callback(new Object[]{"x"})),
                "compatible rewrite must be returned");

        ProviderHooker.setDispatcher((methodId, callbackArgs) ->
                ProviderHooker.DispatchResult.rewrite(17));
        check("original:x".equals(hooker.callback(new Object[]{"x"})),
                "incompatible rewrite must pass through");

        ProviderHooker.setDispatcher((methodId, callbackArgs) -> {
            throw new IllegalStateException("test dispatcher failure");
        });
        check("original:x".equals(hooker.callback(new Object[]{"x"})),
                "dispatcher failure must pass through");

        Method number = Target.class.getDeclaredMethod("number", int.class);
        ProviderHooker primitiveHooker = new ProviderHooker(2, number);
        primitiveHooker.setBackup(number);
        ProviderHooker.setDispatcher((methodId, callbackArgs) ->
                ProviderHooker.DispatchResult.rewrite(Integer.valueOf(42)));
        check(((Integer) primitiveHooker.callback(new Object[]{7})) == 42,
                "compatible primitive wrapper must be returned");

        ProviderHooker.setDispatcher((methodId, callbackArgs) ->
                ProviderHooker.DispatchResult.rewrite("wrong"));
        check(((Integer) primitiveHooker.callback(new Object[]{7})) == 8,
                "incompatible primitive wrapper must pass through");

        ProviderHooker.setDispatcher(new ProviderHooker.Dispatcher() {
            @Override
            public ProviderHooker.DispatchResult dispatch(
                    int methodId, Object[] callbackArgs) {
                return ProviderHooker.DispatchResult.pass();
            }

            @Override
            public ProviderHooker.DispatchResult afterDispatch(
                    int methodId, Object[] callbackArgs, Object originalResult) {
                return ProviderHooker.DispatchResult.rewrite(
                        "after:" + originalResult);
            }
        });
        check("after:original:x".equals(hooker.callback(new Object[]{"x"})),
                "compatible after-dispatch rewrite must be returned");

        ProviderHooker.setDispatcher(new ProviderHooker.Dispatcher() {
            @Override
            public ProviderHooker.DispatchResult dispatch(
                    int methodId, Object[] callbackArgs) {
                return ProviderHooker.DispatchResult.pass();
            }

            @Override
            public ProviderHooker.DispatchResult afterDispatch(
                    int methodId, Object[] callbackArgs, Object originalResult) {
                return ProviderHooker.DispatchResult.rewrite("wrong");
            }
        });
        check(((Integer) primitiveHooker.callback(new Object[]{7})) == 8,
                "incompatible after-dispatch rewrite must return original result");

        ProviderHooker.setDispatcher(new ProviderHooker.Dispatcher() {
            @Override
            public ProviderHooker.DispatchResult dispatch(
                    int methodId, Object[] callbackArgs) {
                return ProviderHooker.DispatchResult.pass();
            }

            @Override
            public ProviderHooker.DispatchResult afterDispatch(
                    int methodId, Object[] callbackArgs, Object originalResult) {
                throw new IllegalStateException("test after-dispatch failure");
            }
        });
        check("original:x".equals(hooker.callback(new Object[]{"x"})),
                "after-dispatch failure must return original result");

        Method nullable = Target.class.getDeclaredMethod("nullable");
        ProviderHooker nullableHooker = new ProviderHooker(3, nullable);
        nullableHooker.setBackup(nullable);
        ProviderHooker.setDispatcher(new ProviderHooker.Dispatcher() {
            @Override
            public ProviderHooker.DispatchResult dispatch(
                    int methodId, Object[] callbackArgs) {
                return ProviderHooker.DispatchResult.pass();
            }

            @Override
            public ProviderHooker.DispatchResult afterDispatch(
                    int methodId, Object[] callbackArgs, Object originalResult) {
                check(originalResult == null,
                        "nullable reference must reach after-dispatch as null");
                return ProviderHooker.DispatchResult.pass();
            }
        });
        check(nullableHooker.callback(new Object[0]) == null,
                "nullable reference must pass through");

        Method empty = Target.class.getDeclaredMethod("empty");
        ProviderHooker voidHooker = new ProviderHooker(4, empty);
        voidHooker.setBackup(empty);
        check(voidHooker.callback(new Object[0]) == null,
                "void result must pass through as null");
        ProviderHooker.clearDispatcher();
    }
}
