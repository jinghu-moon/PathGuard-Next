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
        ProviderHooker.clearDispatcher();
    }
}
