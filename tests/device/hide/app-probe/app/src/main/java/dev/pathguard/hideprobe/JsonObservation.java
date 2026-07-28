package dev.pathguard.hideprobe;

final class JsonObservation {
    private JsonObservation() {}

    static String render(
            String test,
            String surface,
            String path,
            long returnValue,
            int errno,
            boolean sideEffect,
            String status) {
        return "{\"schema\":1,\"kind\":\"observation\",\"test\":\""
                + escape(test)
                + "\",\"surface\":\""
                + escape(surface)
                + "\",\"path\":\""
                + escape(path)
                + "\",\"return_value\":"
                + returnValue
                + ",\"errno\":"
                + errno
                + ",\"side_effect\":"
                + sideEffect
                + ",\"status\":\""
                + escape(status)
                + "\"}";
    }

    static String escape(String value) {
        StringBuilder output = new StringBuilder(value.length());
        for (int index = 0; index < value.length(); ++index) {
            char ch = value.charAt(index);
            switch (ch) {
                case '"': output.append("\\\""); break;
                case '\\': output.append("\\\\"); break;
                case '\b': output.append("\\b"); break;
                case '\f': output.append("\\f"); break;
                case '\n': output.append("\\n"); break;
                case '\r': output.append("\\r"); break;
                case '\t': output.append("\\t"); break;
                default:
                    if (ch < 0x20) {
                        output.append(String.format("\\u%04x", (int) ch));
                    } else {
                        output.append(ch);
                    }
            }
        }
        return output.toString();
    }
}
